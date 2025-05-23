/*
 * WPA Supplicant
 * Copyright (c) 2003-2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This file implements functions for registering and unregistering
 * %wpa_supplicant interfaces. In addition, this file contains number of
 * functions for managing network connections.
 */

#include "includes.h"
#ifdef CONFIG_MATCH_IFACE
#include <net/if.h>
#include <fnmatch.h>
#endif /* CONFIG_MATCH_IFACE */

#include "common.h"
#include "crypto/random.h"
#include "crypto/sha1.h"
#include "rsn_supp/wpa.h"
#include "eloop.h"
#include "config.h"
#include "l2_packet/l2_packet.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "ctrl_iface.h"
#include "common/version.h"
#include "common/wpa_ctrl.h"
#include "common/ieee802_11_defs.h"
#include "common/hw_features_common.h"
#include "wpas_glue.h"
#include "wps_supplicant.h"
#include "ibss_rsn.h"
#include "sme.h"
#include "ap.h"
#include "notify.h"
#include "bss.h"
#include "scan.h"
#include "offchannel.h"
#include "wpas_kay.h"
#include "wpas_pmk.h"

struct wpa_ssid* wpa_get_ssid( struct wpa_supplicant* wpa_s )
{
    if(wpa_s)
        return wpa_s->conf->ssid;
    return 0;
}

/* Configure default/group WEP keys for static WEP */
int wpa_set_wep_keys(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	int i, set = 0;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i] == 0)
			continue;

		set = 1;
		wpa_drv_set_key(wpa_s, WPA_ALG_WEP, NULL,
				i, i == ssid->wep_tx_keyidx, NULL, 0,
				ssid->wep_key[i], ssid->wep_key_len[i]);
	}

	return set;
}


int wpa_supplicant_set_wpa_none_key(struct wpa_supplicant *wpa_s,
				    struct wpa_ssid *ssid)
{
	u8 key[32];
	size_t keylen;
	enum wpa_alg alg;
	u8 seq[6] = { 0 };
	int ret;

	/* IBSS/WPA-None uses only one key (Group) for both receiving and
	 * sending unicast and multicast packets. */

	if (ssid->mode != WPAS_MODE_IBSS) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Invalid mode %d (not "
			"IBSS/ad-hoc) for WPA-None", ssid->mode);
		return -1;
	}

	if (!ssid->psk_set) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: No PSK configured for "
			"WPA-None");
		return -1;
	}

	switch (wpa_s->group_cipher) {
	case WPA_CIPHER_CCMP:
		os_memcpy(key, ssid->psk, 16);
		keylen = 16;
		alg = WPA_ALG_CCMP;
		break;
	case WPA_CIPHER_GCMP:
		os_memcpy(key, ssid->psk, 16);
		keylen = 16;
		alg = WPA_ALG_GCMP;
		break;
	case WPA_CIPHER_TKIP:
		/* WPA-None uses the same Michael MIC key for both TX and RX */
		os_memcpy(key, ssid->psk, 16 + 8);
		os_memcpy(key + 16 + 8, ssid->psk + 16, 8);
		keylen = 32;
		alg = WPA_ALG_TKIP;
		break;
	default:
		wpa_msg(wpa_s, MSG_INFO, "WPA: Invalid group cipher %d for "
			"WPA-None", wpa_s->group_cipher);
		return -1;
	}

	/* TODO: should actually remember the previously used seq#, both for TX
	 * and RX from each STA.. */

	ret = wpa_drv_set_key(wpa_s, alg, NULL, 0, 1, seq, 6, key, keylen);
	os_memset(key, 0, sizeof(key));
	return ret;
}

extern void wpa_scan_res_mark_fail(struct wpa_supplicant *wpa_s);
static void wpa_supplicant_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	const u8 *bssid __attribute__((unused)) = wpa_s->bssid;
	if (!is_zero_ether_addr(wpa_s->pending_bssid) &&
	    (wpa_s->wpa_state == WPA_AUTHENTICATING ||
	     wpa_s->wpa_state == WPA_ASSOCIATING))
		bssid = wpa_s->pending_bssid;
	wpa_dbg(wpa_s, MSG_INFO, "Authentication with " MACSTR " timed out.",
		MAC2STR(bssid));
	wpa_sm_notify_disassoc(wpa_s->wpa);
	wpa_supplicant_deauthenticate(wpa_s, WLAN_REASON_DEAUTH_LEAVING);
	wpa_scan_res_mark_fail(wpa_s);
	//wpa_s->reassociate = 1;

	/*
	 * If we timed out, the AP or the local radio may be busy.
	 * So, wait a second until scanning again.
	 */
	wpa_supplicant_req_scan(wpa_s, 1, 0);
}


/**
 * wpa_supplicant_req_auth_timeout - Schedule a timeout for authentication
 * @wpa_s: Pointer to wpa_supplicant data
 * @sec: Number of seconds after which to time out authentication
 * @usec: Number of microseconds after which to time out authentication
 *
 * This function is used to schedule a timeout for the current authentication
 * attempt.
 */
void wpa_supplicant_req_auth_timeout(struct wpa_supplicant *wpa_s,
				     int sec, int usec)
{
	if (wpa_s->conf->ap_scan == 0 &&
	    (wpa_s->drv_flags & WPA_DRIVER_FLAGS_WIRED))
		return;

	wpa_dbg(wpa_s, MSG_DEBUG, "Setting authentication timeout: %d sec "
		"%d usec", sec, usec);
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);
	eloop_register_timeout(sec, usec, wpa_supplicant_timeout, wpa_s, NULL);
}


/**
 * wpa_supplicant_cancel_auth_timeout - Cancel authentication timeout
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is used to cancel authentication timeout scheduled with
 * wpa_supplicant_req_auth_timeout() and it is called when authentication has
 * been completed.
 */
void wpa_supplicant_cancel_auth_timeout(struct wpa_supplicant *wpa_s)
{
	wpa_dbg(wpa_s, MSG_DEBUG, "Cancelling authentication timeout");
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);
}


/**
 * wpa_supplicant_initiate_eapol - Configure EAPOL state machine
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is used to configure EAPOL state machine based on the selected
 * authentication mode.
 */
void wpa_supplicant_initiate_eapol(struct wpa_supplicant *wpa_s)
{
#ifdef IEEE8021X_EAPOL
	struct eapol_config eapol_conf;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

#ifdef CONFIG_IBSS_RSN
	if (ssid->mode == WPAS_MODE_IBSS &&
	    wpa_s->key_mgmt != WPA_KEY_MGMT_NONE &&
	    wpa_s->key_mgmt != WPA_KEY_MGMT_WPA_NONE) {
		/*
		 * RSN IBSS authentication is per-STA and we can disable the
		 * per-BSSID EAPOL authentication.
		 */
		eapol_sm_notify_portControl(wpa_s->eapol, ForceAuthorized);
		eapol_sm_notify_eap_success(wpa_s->eapol, TRUE);
		eapol_sm_notify_eap_fail(wpa_s->eapol, FALSE);
		return;
	}
#endif /* CONFIG_IBSS_RSN */

	os_memset(&eapol_conf, 0, sizeof(eapol_conf));
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		eapol_conf.accept_802_1x_keys = 1;
		eapol_conf.required_keys = 0;
		if (ssid->eapol_flags & EAPOL_FLAG_REQUIRE_KEY_UNICAST) {
			eapol_conf.required_keys |= EAPOL_REQUIRE_KEY_UNICAST;
		}
		if (ssid->eapol_flags & EAPOL_FLAG_REQUIRE_KEY_BROADCAST) {
			eapol_conf.required_keys |=
				EAPOL_REQUIRE_KEY_BROADCAST;
		}

		if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_WIRED)
			eapol_conf.required_keys = 0;
	}
	eapol_conf.fast_reauth = wpa_s->conf->fast_reauth;
	eapol_conf.workaround = ssid->eap_workaround;
	eapol_conf.eap_disabled =
		!wpa_key_mgmt_wpa_ieee8021x(wpa_s->key_mgmt) &&
		wpa_s->key_mgmt != WPA_KEY_MGMT_IEEE8021X_NO_WPA &&
		wpa_s->key_mgmt != WPA_KEY_MGMT_WPS;
	eapol_conf.external_sim = wpa_s->conf->external_sim;

#ifdef CONFIG_WPS
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPS) {
		eapol_conf.wps |= EAPOL_LOCAL_WPS_IN_USE;
		if (wpa_s->current_bss) {
			struct wpabuf *ie;
			ie = wpa_bss_get_vendor_ie_multi(wpa_s->current_bss,
							 WPS_IE_VENDOR_TYPE);
			if (ie) {
				if (wps_is_20(ie))
					eapol_conf.wps |=
						EAPOL_PEER_IS_WPS20_AP;
				wpabuf_free(ie);
			}
		}
	}
#endif /* CONFIG_WPS */

	ieee802_1x_alloc_kay_sm(wpa_s, ssid);
#endif /* IEEE8021X_EAPOL */
}


/**
 * wpa_supplicant_set_non_wpa_policy - Set WPA parameters to non-WPA mode
 * @wpa_s: Pointer to wpa_supplicant data
 * @ssid: Configuration data for the network
 *
 * This function is used to configure WPA state machine and related parameters
 * to a mode where WPA is not enabled. This is called as part of the
 * authentication configuration when the selected network does not use WPA.
 */
void wpa_supplicant_set_non_wpa_policy(struct wpa_supplicant *wpa_s,
				       struct wpa_ssid *ssid)
{
	int i;

	if (ssid->key_mgmt & WPA_KEY_MGMT_WPS)
		wpa_s->key_mgmt = WPA_KEY_MGMT_WPS;
	else if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA)
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X_NO_WPA;
	else
		wpa_s->key_mgmt = WPA_KEY_MGMT_NONE;
	wpa_sm_set_ap_wpa_ie(wpa_s->wpa, NULL, 0);
	wpa_sm_set_ap_rsn_ie(wpa_s->wpa, NULL, 0);
	wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);
	wpa_s->pairwise_cipher = WPA_CIPHER_NONE;
	wpa_s->group_cipher = WPA_CIPHER_NONE;
	wpa_s->mgmt_group_cipher = 0;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i] > 5) {
			wpa_s->pairwise_cipher = WPA_CIPHER_WEP104;
			wpa_s->group_cipher = WPA_CIPHER_WEP104;
			break;
		} else if (ssid->wep_key_len[i] > 0) {
			wpa_s->pairwise_cipher = WPA_CIPHER_WEP40;
			wpa_s->group_cipher = WPA_CIPHER_WEP40;
			break;
		}
	}

	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_RSN_ENABLED, 0);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_KEY_MGMT, wpa_s->key_mgmt);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PAIRWISE,
			 wpa_s->pairwise_cipher);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_GROUP, wpa_s->group_cipher);
#ifdef CONFIG_IEEE80211W
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_MGMT_GROUP,
			 wpa_s->mgmt_group_cipher);
#endif /* CONFIG_IEEE80211W */

}


void free_hw_features(struct wpa_supplicant *wpa_s)
{
	int i;
	if (wpa_s->hw.modes == NULL)
		return;

	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		os_free(wpa_s->hw.modes[i].channels);
		os_free(wpa_s->hw.modes[i].rates);
	}

	os_free(wpa_s->hw.modes);
	wpa_s->hw.modes = NULL;
}

#if 0
static void free_bss_tmp_disallowed(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss_tmp_disallowed *bss, *prev;

	dl_list_for_each_safe(bss, prev, &wpa_s->bss_tmp_disallowed,
			      struct wpa_bss_tmp_disallowed, list) {
		dl_list_del(&bss->list);
		os_free(bss);
	}
}
#endif

static void wpa_supplicant_cleanup(struct wpa_supplicant *wpa_s)
{
	//int i;

	//wpa_sm_set_scard_ctx(wpa_s->wpa, NULL);
	//eapol_sm_register_scard_ctx(wpa_s->eapol, NULL);
	l2_packet_deinit(wpa_s->l2);
	wpa_s->l2 = NULL;
	//if (wpa_s->l2_br) {
		//l2_packet_deinit(wpa_s->l2_br);
		//wpa_s->l2_br = NULL;
	//}
#ifdef CONFIG_TESTING_OPTIONS
	l2_packet_deinit(wpa_s->l2_test);
	wpa_s->l2_test = NULL;
#endif /* CONFIG_TESTING_OPTIONS */

	/*if (wpa_s->conf != NULL) {
		struct wpa_ssid *ssid;
		for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)
			wpas_notify_network_removed(wpa_s, ssid);
	}*/

#ifdef CONFIG_TDLS
	wpa_tdls_deinit(wpa_s->wpa);
#endif /* CONFIG_TDLS */

	wpa_sm_deinit(wpa_s->wpa);
	wpa_s->wpa = NULL;

	wpa_bss_deinit(wpa_s);

	//wpa_supplicant_cancel_delayed_sched_scan(wpa_s);
	wpa_supplicant_cancel_scan(wpa_s);
	wpa_supplicant_cancel_auth_timeout(wpa_s);
	eloop_cancel_timeout(wpa_supplicant_stop_countermeasures, wpa_s, NULL);
#ifdef CONFIG_DELAYED_MIC_ERROR_REPORT
	eloop_cancel_timeout(wpa_supplicant_delayed_mic_error_report,
			     wpa_s, NULL);
#endif /* CONFIG_DELAYED_MIC_ERROR_REPORT */

	eloop_cancel_timeout(wpas_network_reenabled, wpa_s, NULL);

	//wpas_wps_deinit(wpa_s);

	wpabuf_free(wpa_s->pending_eapol_rx);
	wpa_s->pending_eapol_rx = NULL;

#ifdef CONFIG_IBSS_RSN
	ibss_rsn_deinit(wpa_s->ibss_rsn);
	wpa_s->ibss_rsn = NULL;
#endif /* CONFIG_IBSS_RSN */

	sme_deinit(wpa_s);

#ifdef CONFIG_AP
	wpa_supplicant_ap_deinit(wpa_s);
#endif /* CONFIG_AP */


#ifdef CONFIG_OFFCHANNEL
	offchannel_deinit(wpa_s);
#endif /* CONFIG_OFFCHANNEL */

	//wpa_supplicant_cancel_sched_scan(wpa_s);

	//os_free(wpa_s->next_scan_freqs);
	//wpa_s->next_scan_freqs = NULL;

	//os_free(wpa_s->manual_scan_freqs);
	//wpa_s->manual_scan_freqs = NULL;

	//os_free(wpa_s->manual_sched_scan_freqs);
	//wpa_s->manual_sched_scan_freqs = NULL;

	//wpa_s->gas = NULL;

	free_hw_features(wpa_s);

	//ieee802_1x_dealloc_kay_sm(wpa_s);

	//wnm_bss_keep_alive_deinit(wpa_s);
#ifdef CONFIG_WNM
	wnm_deallocate_memory(wpa_s);
#endif /* CONFIG_WNM */

	//ext_password_deinit(wpa_s->ext_pw);
	//wpa_s->ext_pw = NULL;

	//wpabuf_free(wpa_s->last_gas_resp);
	//wpa_s->last_gas_resp = NULL;
	//wpabuf_free(wpa_s->prev_gas_resp);
	//wpa_s->prev_gas_resp = NULL;

	os_free(wpa_s->last_scan_res);
	wpa_s->last_scan_res = NULL;

#ifdef CONFIG_HS20
	if (wpa_s->drv_priv)
		wpa_drv_configure_frame_filters(wpa_s, 0);
	hs20_deinit(wpa_s);
#endif /* CONFIG_HS20 */

#if 0
	for (i = 0; i < NUM_VENDOR_ELEM_FRAMES; i++) {
		wpabuf_free(wpa_s->vendor_elem[i]);
		wpa_s->vendor_elem[i] = NULL;
	}
#endif    

	//wmm_ac_notify_disassoc(wpa_s);

	//wpa_s->sched_scan_plans_num = 0;
	//os_free(wpa_s->sched_scan_plans);
	//wpa_s->sched_scan_plans = NULL;

#ifdef CONFIG_MBO
	wpa_s->non_pref_chan_num = 0;
	os_free(wpa_s->non_pref_chan);
	wpa_s->non_pref_chan = NULL;
#endif /* CONFIG_MBO */

	//free_bss_tmp_disallowed(wpa_s);

}


/**
 * wpa_clear_keys - Clear keys configured for the driver
 * @wpa_s: Pointer to wpa_supplicant data
 * @addr: Previously used BSSID or %NULL if not available
 *
 * This function clears the encryption keys that has been previously configured
 * for the driver.
 */
void wpa_clear_keys(struct wpa_supplicant *wpa_s, const u8 *addr)
{
	int i, max;

#ifdef CONFIG_IEEE80211W
	max = 6;
#else /* CONFIG_IEEE80211W */
	max = 4;
#endif /* CONFIG_IEEE80211W */

	/* MLME-DELETEKEYS.request */
	for (i = 0; i < max; i++) {
		if (wpa_s->keys_cleared & BIT(i))
			continue;
		wpa_drv_set_key(wpa_s, WPA_ALG_NONE, NULL, i, 0, NULL, 0,
				NULL, 0);
	}
	if (!(wpa_s->keys_cleared & BIT(0)) && addr &&
	    !is_zero_ether_addr(addr)) {
		wpa_drv_set_key(wpa_s, WPA_ALG_NONE, addr, 0, 0, NULL, 0, NULL,
				0);
		/* MLME-SETPROTECTION.request(None) */
		wpa_drv_mlme_setprotection(
			wpa_s, addr,
			MLME_SETPROTECTION_PROTECT_TYPE_NONE,
			MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);
	}
	wpa_s->keys_cleared = (u32) -1;
}


/**
 * wpa_supplicant_state_txt - Get the connection state name as a text string
 * @state: State (wpa_state; WPA_*)
 * Returns: The state name as a printable text string
 */
const char * wpa_supplicant_state_txt(enum wpa_states state)
{
	switch (state) {
	case WPA_DISCONNECTED:
		return "DISCON";
	case WPA_INACTIVE:
		return "INACT";
	//case WPA_INTERFACE_DISABLED:
	//	return "INTERFACE_DISABLED";
	case WPA_SCANNING:
		return "SCAN";
	case WPA_AUTHENTICATING:
		return "AUTH";
	case WPA_ASSOCIATING:
		return "ASSOC";
	case WPA_ASSOCIATED:
		return "ASSOCIATED";
	case WPA_4WAY_HANDSHAKE:
		return "HANDSHAKE";
	case WPA_GROUP_HANDSHAKE:
		return "GROUP_HANDSHAKE";
	case WPA_COMPLETED:
		return "COMPLETED";
	default:
		return "UNKNOWN";
	}
}

/**
 * wpa_supplicant_set_state - Set current connection state
 * @wpa_s: Pointer to wpa_supplicant data
 * @state: The new connection state
 *
 * This function is called whenever the connection state changes, e.g.,
 * association is completed for WPA/WPA2 4-Way Handshake is started.
 */
void wpa_supplicant_set_state(struct wpa_supplicant *wpa_s,
			      enum wpa_states state)
{
	enum wpa_states old_state = wpa_s->wpa_state;

	os_printf(LM_APP, LL_INFO, "wpa state: %s %s->%s.\n",
        wpa_s->ifname,
		wpa_supplicant_state_txt(wpa_s->wpa_state),
		wpa_supplicant_state_txt(state));

	//if (state == WPA_INTERFACE_DISABLED) {
		/* Assure normal scan when interface is restored */
	//	wpa_s->normal_scans = 0;
	//}

	if (state == WPA_COMPLETED) {
		wpas_connect_work_done(wpa_s);
		/* Reinitialize normal_scan counter */
		wpa_s->normal_scans = 0;
	}

	if (state != WPA_SCANNING)
		wpa_supplicant_notify_scanning(wpa_s, 0);

	if (state == WPA_COMPLETED && wpa_s->new_connection) {
		struct wpa_ssid *ssid __attribute__((unused)) = wpa_s->current_ssid;
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
		wpa_dbg(wpa_s, MSG_INFO, WPA_EVENT_CONNECTED "- Connection to "
			MACSTR " completed [id=%d id_str=%s]",
			MAC2STR(wpa_s->bssid),
			ssid ? ssid->id : -1,
			/*ssid && ssid->id_str ? ssid->id_str :*/ "");
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
		//wpas_clear_temp_disabled(wpa_s, ssid, 1);
		//wpa_s->extra_blacklist_count = 0;
		wpa_s->new_connection = 0;
		//wpa_drv_set_operstate(wpa_s, 1);
#ifndef IEEE8021X_EAPOL
		wpa_drv_set_supp_port(wpa_s, 1);
#endif /* IEEE8021X_EAPOL */
		//wpa_s->after_wps = 0;
		//wpa_s->known_wps_freq = 0;

		//sme_sched_obss_scan(wpa_s, 1);
	} else if (state == WPA_DISCONNECTED || state == WPA_ASSOCIATING ||
		   state == WPA_ASSOCIATED) {
		wpa_s->new_connection = 1;
		//wpa_drv_set_operstate(wpa_s, 0);
#ifndef IEEE8021X_EAPOL
		wpa_drv_set_supp_port(wpa_s, 0);
#endif /* IEEE8021X_EAPOL */
		//sme_sched_obss_scan(wpa_s, 0);
	}
	wpa_s->wpa_state = state;

	//if (old_state >= WPA_ASSOCIATED && wpa_s->wpa_state < WPA_ASSOCIATED)
		//wmm_ac_notify_disassoc(wpa_s);

	if (wpa_s->wpa_state != old_state) {
		wpas_notify_state_changed(wpa_s, wpa_s->wpa_state, old_state);


		//if (wpa_s->wpa_state == WPA_COMPLETED ||
		//    old_state == WPA_COMPLETED)
		//	wpas_notify_auth_changed(wpa_s);
	}
    
    if (WPA_INACTIVE == state)
        wpa_drv_wpa_states_inactive(wpa_s);
}


void wpa_supplicant_terminate_proc(struct wpa_global *global)
{
	int pending = 0;
#ifdef CONFIG_WPS
	struct wpa_supplicant *wpa_s = global->ifaces;
	while (wpa_s) {
		struct wpa_supplicant *next = wpa_s->next;
		if (wpas_wps_terminate_pending(wpa_s) == 1)
			pending = 1;
		wpa_s = next;
	}
#endif /* CONFIG_WPS */
	if (pending)
		return;
	eloop_terminate();
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpa_supplicant_terminate(int sig, void *signal_ctx)
{
	struct wpa_global *global = signal_ctx;
	wpa_supplicant_terminate_proc(global);
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


void wpa_supplicant_clear_status(struct wpa_supplicant *wpa_s)
{
	enum wpa_states old_state = wpa_s->wpa_state;

	wpa_s->pairwise_cipher = 0;
	wpa_s->group_cipher = 0;
	wpa_s->mgmt_group_cipher = 0;
	wpa_s->key_mgmt = 0;
	if (wpa_s->wpa_state != WPA_INTERFACE_DISABLED)
		wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);

	if (wpa_s->wpa_state != old_state)
		wpas_notify_state_changed(wpa_s, wpa_s->wpa_state, old_state);
}



static int wpa_supplicant_suites_from_ai(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid,
					 struct wpa_ie_data *ie)
{
	int ret = wpa_sm_parse_own_wpa_ie(wpa_s->wpa, ie);
	if (ret) {
		if (ret == -2) {
			wpa_msg(wpa_s, MSG_INFO, "WPA: Failed to parse WPA IE "
				"from association info");
		}
		return -1;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Using WPA IE from AssocReq to set "
		"cipher suites");
	if (!(ie->group_cipher & ssid->group_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled group "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->group_cipher, ssid->group_cipher);
		return -1;
	}
	if (!(ie->pairwise_cipher & ssid->pairwise_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled pairwise "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->pairwise_cipher, ssid->pairwise_cipher);
		return -1;
	}
	if (!(ie->key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled key "
			"management 0x%x (mask 0x%x) - reject",
			ie->key_mgmt, ssid->key_mgmt);
		return -1;
	}

#ifdef CONFIG_IEEE80211W
	if (!(ie->capabilities & WPA_CAPABILITY_MFPC) &&
	    wpas_get_ssid_pmf(wpa_s, ssid) == MGMT_FRAME_PROTECTION_REQUIRED) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver associated with an AP "
			"that does not support management frame protection - "
			"reject");
		return -1;
	}
#endif /* CONFIG_IEEE80211W */

	return 0;
}


/**
 * wpa_supplicant_set_suites - Set authentication and encryption parameters
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Scan results for the selected BSS, or %NULL if not available
 * @ssid: Configuration data for the selected network
 * @wpa_ie: Buffer for the WPA/RSN IE
 * @wpa_ie_len: Maximum wpa_ie buffer size on input. This is changed to be the
 * used buffer length in case the functions returns success.
 * Returns: 0 on success or -1 on failure
 *
 * This function is used to configure authentication and encryption parameters
 * based on the network configuration and scan result for the selected BSS (if
 * available).
 */
int wpa_supplicant_set_suites(struct wpa_supplicant *wpa_s,
			      struct wpa_bss *bss, struct wpa_ssid *ssid,
			      u8 *wpa_ie, size_t *wpa_ie_len)
{
	struct wpa_ie_data ie;
	int sel, proto;
	const u8 *bss_wpa, *bss_rsn, *bss_osen;

	if (bss) {
		bss_wpa = wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE);
		bss_rsn = wpa_bss_get_ie(bss, WLAN_EID_RSN);
		bss_osen = wpa_bss_get_vendor_ie(bss, OSEN_IE_VENDOR_TYPE);
	} else
		bss_wpa = bss_rsn = bss_osen = NULL;

	if (bss_rsn && (ssid->proto & WPA_PROTO_RSN) &&
	    wpa_parse_wpa_ie(bss_rsn, 2 + bss_rsn[1], &ie) == 0 &&
	    (ie.group_cipher & ssid->group_cipher) &&
	    (ie.pairwise_cipher & ssid->pairwise_cipher) &&
	    (ie.key_mgmt & ssid->key_mgmt)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "RSN: using IEEE 802.11i/D9.0");
		proto = WPA_PROTO_RSN;
	} else if (bss_wpa && (ssid->proto & WPA_PROTO_WPA) &&
		   wpa_parse_wpa_ie(bss_wpa, 2 + bss_wpa[1], &ie) == 0 &&
		   (ie.group_cipher & ssid->group_cipher) &&
		   (ie.pairwise_cipher & ssid->pairwise_cipher) &&
		   (ie.key_mgmt & ssid->key_mgmt)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using IEEE 802.11i/D3.0");
		proto = WPA_PROTO_WPA;
#ifdef CONFIG_HS20
	} else if (bss_osen && (ssid->proto & WPA_PROTO_OSEN)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "HS 2.0: using OSEN");
		/* TODO: parse OSEN element */
		os_memset(&ie, 0, sizeof(ie));
		ie.group_cipher = WPA_CIPHER_CCMP;
		ie.pairwise_cipher = WPA_CIPHER_CCMP;
		ie.key_mgmt = WPA_KEY_MGMT_OSEN;
		proto = WPA_PROTO_OSEN;
#endif /* CONFIG_HS20 */
	} else if (bss) {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to select WPA/RSN");
        #if 0
		wpa_dbg(wpa_s, MSG_DEBUG,
			"WPA: ssid proto=0x%x pairwise_cipher=0x%x group_cipher=0x%x key_mgmt=0x%x",
			ssid->proto, ssid->pairwise_cipher, ssid->group_cipher,
			ssid->key_mgmt);
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: BSS " MACSTR " ssid='%s'%s%s%s",
			MAC2STR(bss->bssid),
			wpa_ssid_txt(bss->ssid, bss->ssid_len),
			bss_wpa ? " WPA" : "",
			bss_rsn ? " RSN" : "",
			bss_osen ? " OSEN" : "");
		if (bss_rsn) {
			wpa_hexdump(MSG_DEBUG, "RSN", bss_rsn, 2 + bss_rsn[1]);
			if (wpa_parse_wpa_ie(bss_rsn, 2 + bss_rsn[1], &ie)) {
				wpa_dbg(wpa_s, MSG_DEBUG,
					"Could not parse RSN element");
			} else {
				wpa_dbg(wpa_s, MSG_DEBUG,
					"RSN: pairwise_cipher=0x%x group_cipher=0x%x key_mgmt=0x%x",
					ie.pairwise_cipher, ie.group_cipher,
					ie.key_mgmt);
			}
		}
		if (bss_wpa) {
			wpa_hexdump(MSG_DEBUG, "WPA", bss_wpa, 2 + bss_wpa[1]);
			if (wpa_parse_wpa_ie(bss_wpa, 2 + bss_wpa[1], &ie)) {
				wpa_dbg(wpa_s, MSG_DEBUG,
					"Could not parse WPA element");
			} else {
				wpa_dbg(wpa_s, MSG_DEBUG,
					"WPA: pairwise_cipher=0x%x group_cipher=0x%x key_mgmt=0x%x",
					ie.pairwise_cipher, ie.group_cipher,
					ie.key_mgmt);
			}
		}
        #endif
		return -1;
	} else {
		if (ssid->proto & WPA_PROTO_OSEN)
			proto = WPA_PROTO_OSEN;
		else if (ssid->proto & WPA_PROTO_RSN)
			proto = WPA_PROTO_RSN;
		else
			proto = WPA_PROTO_WPA;
		if (wpa_supplicant_suites_from_ai(wpa_s, ssid, &ie) < 0) {
			os_memset(&ie, 0, sizeof(ie));
			ie.group_cipher = ssid->group_cipher;
			ie.pairwise_cipher = ssid->pairwise_cipher;
			ie.key_mgmt = ssid->key_mgmt;
#ifdef CONFIG_IEEE80211W
			ie.mgmt_group_cipher =
				ssid->ieee80211w != NO_MGMT_FRAME_PROTECTION ?
				WPA_CIPHER_AES_128_CMAC : 0;
#endif /* CONFIG_IEEE80211W */
			wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Set cipher suites "
				"based on configuration");
		} else
			proto = ie.proto;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Selected cipher suites: group %d "
		"pairwise %d key_mgmt %d proto %d",
		ie.group_cipher, ie.pairwise_cipher, ie.key_mgmt, proto);
#ifdef CONFIG_IEEE80211W
	if (ssid->ieee80211w) {
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Selected mgmt group cipher %d",
			ie.mgmt_group_cipher);
	}
#endif /* CONFIG_IEEE80211W */

	wpa_s->wpa_proto = proto;
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PROTO, proto);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_RSN_ENABLED,
			 !!(ssid->proto & (WPA_PROTO_RSN | WPA_PROTO_OSEN)));

	if (bss || !wpa_s->ap_ies_from_associnfo) {
		if (wpa_sm_set_ap_wpa_ie(wpa_s->wpa, bss_wpa,
					 bss_wpa ? 2 + bss_wpa[1] : 0) ||
		    wpa_sm_set_ap_rsn_ie(wpa_s->wpa, bss_rsn,
					 bss_rsn ? 2 + bss_rsn[1] : 0))
			return -1;
	}

#ifdef CONFIG_NO_WPA
	wpa_s->group_cipher = WPA_CIPHER_NONE;
	wpa_s->pairwise_cipher = WPA_CIPHER_NONE;
#else /* CONFIG_NO_WPA */
	sel = ie.group_cipher & ssid->group_cipher;
	wpa_s->group_cipher = wpa_pick_group_cipher(sel);
	if (wpa_s->group_cipher < 0) {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to select group "
			"cipher");
		return -1;
	}
	wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using GTK %s",
		wpa_cipher_txt(wpa_s->group_cipher));

	sel = ie.pairwise_cipher & ssid->pairwise_cipher;
	wpa_s->pairwise_cipher = wpa_pick_pairwise_cipher(sel, 1);
	if (wpa_s->pairwise_cipher < 0) {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to select pairwise "
			"cipher");
		return -1;
	}
	wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using PTK %s",
		wpa_cipher_txt(wpa_s->pairwise_cipher));
#endif /* CONFIG_NO_WPA */

	sel = ie.key_mgmt & ssid->key_mgmt;
#ifdef CONFIG_SAE
	if (!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_SAE))
		sel &= ~(WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE);
#endif /* CONFIG_SAE */
	if (0) {
#ifdef CONFIG_SUITEB192
	} else if (sel & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X_SUITE_B_192;
		wpa_dbg(wpa_s, MSG_DEBUG,
			"WPA: using KEY_MGMT 802.1X with Suite B (192-bit)");
#endif /* CONFIG_SUITEB192 */
#ifdef CONFIG_SUITEB
	} else if (sel & WPA_KEY_MGMT_IEEE8021X_SUITE_B) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X_SUITE_B;
		wpa_dbg(wpa_s, MSG_DEBUG,
			"WPA: using KEY_MGMT 802.1X with Suite B");
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_IEEE80211R
	} else if (sel & WPA_KEY_MGMT_FT_IEEE8021X) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_FT_IEEE8021X;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT FT/802.1X");
	} else if (sel & WPA_KEY_MGMT_FT_PSK) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_FT_PSK;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT FT/PSK");
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_SAE
	} else if (sel & WPA_KEY_MGMT_SAE) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_SAE;
		wpa_dbg(wpa_s, MSG_DEBUG, "RSN: using KEY_MGMT SAE");
	} else if (sel & WPA_KEY_MGMT_FT_SAE) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_FT_SAE;
		wpa_dbg(wpa_s, MSG_DEBUG, "RSN: using KEY_MGMT FT/SAE");
#endif /* CONFIG_SAE */
#ifdef CONFIG_IEEE80211W
	} else if (sel & WPA_KEY_MGMT_IEEE8021X_SHA256) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X_SHA256;
		wpa_dbg(wpa_s, MSG_DEBUG,
			"WPA: using KEY_MGMT 802.1X with SHA256");
	} else if (sel & WPA_KEY_MGMT_PSK_SHA256) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_PSK_SHA256;
		wpa_dbg(wpa_s, MSG_DEBUG,
			"WPA: using KEY_MGMT PSK with SHA256");
#endif /* CONFIG_IEEE80211W */
	} else if (sel & WPA_KEY_MGMT_IEEE8021X) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT 802.1X");
	} else if (sel & WPA_KEY_MGMT_PSK) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_PSK;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT WPA-PSK");
	} else if (sel & WPA_KEY_MGMT_WPA_NONE) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_WPA_NONE;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT WPA-NONE");
#ifdef CONFIG_HS20
	} else if (sel & WPA_KEY_MGMT_OSEN) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_OSEN;
		wpa_dbg(wpa_s, MSG_DEBUG, "HS 2.0: using KEY_MGMT OSEN");
#endif /* CONFIG_HS20 */
	} else {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to select "
			"authenticated key management type");
		return -1;
	}

	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_KEY_MGMT, wpa_s->key_mgmt);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PAIRWISE,
			 wpa_s->pairwise_cipher);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_GROUP, wpa_s->group_cipher);

#ifdef CONFIG_IEEE80211W
	sel = ie.mgmt_group_cipher;
	if (wpas_get_ssid_pmf(wpa_s, ssid) == NO_MGMT_FRAME_PROTECTION ||
	    !(ie.capabilities & WPA_CAPABILITY_MFPC))
		sel = 0;
	if (sel & WPA_CIPHER_AES_128_CMAC) {
		wpa_s->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using MGMT group cipher "
			"AES-128-CMAC");
	} else if (sel & WPA_CIPHER_BIP_GMAC_128) {
		wpa_s->mgmt_group_cipher = WPA_CIPHER_BIP_GMAC_128;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using MGMT group cipher "
			"BIP-GMAC-128");
	} else if (sel & WPA_CIPHER_BIP_GMAC_256) {
		wpa_s->mgmt_group_cipher = WPA_CIPHER_BIP_GMAC_256;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using MGMT group cipher "
			"BIP-GMAC-256");
	} else if (sel & WPA_CIPHER_BIP_CMAC_256) {
		wpa_s->mgmt_group_cipher = WPA_CIPHER_BIP_CMAC_256;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: using MGMT group cipher "
			"BIP-CMAC-256");
	} else {
		wpa_s->mgmt_group_cipher = 0;
		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: not using MGMT group cipher");
	}
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_MGMT_GROUP,
			 wpa_s->mgmt_group_cipher);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_MFP,
			 wpas_get_ssid_pmf(wpa_s, ssid));
#endif /* CONFIG_IEEE80211W */
    
    wpas_pmk_info_refresh(wpa_s, ssid);

	if (wpa_sm_set_assoc_wpa_ie_default(wpa_s->wpa, wpa_ie, wpa_ie_len)) {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to generate WPA IE");
		return -1;
	}

	if (wpa_key_mgmt_wpa_psk(ssid->key_mgmt)) {
		int psk_set = 0;
        if(wpa_key_mgmt_sae(ssid->key_mgmt))
            psk_set = 1;
		if (ssid->psk_set) {
			wpa_sm_set_pmk(wpa_s->wpa, ssid->psk, PMK_LEN, NULL,
				       NULL);
			psk_set = 1;
		}
#ifndef CONFIG_NO_PBKDF2
		if (bss && ssid->bssid_set && ssid->ssid_len == 0 &&
		    ssid->passphrase) {
			u8 psk[PMK_LEN];
		        pbkdf2_sha1(ssid->passphrase, bss->ssid, bss->ssid_len,
				    4096, psk, PMK_LEN);
		        wpa_hexdump_key(MSG_MSGDUMP, "PSK (from passphrase)",
					psk, PMK_LEN);
			wpa_sm_set_pmk(wpa_s->wpa, psk, PMK_LEN, NULL, NULL);
			psk_set = 1;
			os_memset(psk, 0, sizeof(psk));
		}
#endif /* CONFIG_NO_PBKDF2 */
#ifdef CONFIG_EXT_PASSWORD
		if (ssid->ext_psk) {
			struct wpabuf *pw = ext_password_get(wpa_s->ext_pw,
							     ssid->ext_psk);
			char pw_str[64 + 1];
			u8 psk[PMK_LEN];

			if (pw == NULL) {
				wpa_msg(wpa_s, MSG_INFO, "EXT PW: No PSK "
					"found from external storage");
				return -1;
			}

			if (wpabuf_len(pw) < 8 || wpabuf_len(pw) > 64) {
				wpa_msg(wpa_s, MSG_INFO, "EXT PW: Unexpected "
					"PSK length %d in external storage",
					(int) wpabuf_len(pw));
				ext_password_free(pw);
				return -1;
			}

			os_memcpy(pw_str, wpabuf_head(pw), wpabuf_len(pw));
			pw_str[wpabuf_len(pw)] = '\0';

#ifndef CONFIG_NO_PBKDF2
			if (wpabuf_len(pw) >= 8 && wpabuf_len(pw) < 64 && bss)
			{
				pbkdf2_sha1(pw_str, bss->ssid, bss->ssid_len,
					    4096, psk, PMK_LEN);
				os_memset(pw_str, 0, sizeof(pw_str));
				wpa_hexdump_key(MSG_MSGDUMP, "PSK (from "
						"external passphrase)",
						psk, PMK_LEN);
				wpa_sm_set_pmk(wpa_s->wpa, psk, PMK_LEN, NULL,
					       NULL);
				psk_set = 1;
				os_memset(psk, 0, sizeof(psk));
			} else
#endif /* CONFIG_NO_PBKDF2 */
			if (wpabuf_len(pw) == 2 * PMK_LEN) {
				if (hexstr2bin(pw_str, psk, PMK_LEN) < 0) {
					wpa_msg(wpa_s, MSG_INFO, "EXT PW: "
						"Invalid PSK hex string");
					os_memset(pw_str, 0, sizeof(pw_str));
					ext_password_free(pw);
					return -1;
				}
				wpa_sm_set_pmk(wpa_s->wpa, psk, PMK_LEN, NULL,
					       NULL);
				psk_set = 1;
				os_memset(psk, 0, sizeof(psk));
			} else {
				wpa_msg(wpa_s, MSG_INFO, "EXT PW: No suitable "
					"PSK available");
				os_memset(pw_str, 0, sizeof(pw_str));
				ext_password_free(pw);
				return -1;
			}

			os_memset(pw_str, 0, sizeof(pw_str));
			ext_password_free(pw);
		}
#endif /* CONFIG_EXT_PASSWORD */

		if (!psk_set) {
			wpa_msg(wpa_s, MSG_INFO,
				"No PSK available for association");
			return -1;
		}
	} else
		wpa_sm_set_pmk_from_pmksa(wpa_s->wpa);

	return 0;
}


static void wpas_ext_capab_byte(struct wpa_supplicant *wpa_s, u8 *pos, int idx)
{
	*pos = 0x00;

	switch (idx) {
	case 0: /* Bits 0-7 */
		break;
	case 1: /* Bits 8-15 */
		break;
	case 2: /* Bits 16-23 */
#ifdef CONFIG_WNM
		*pos |= 0x02; /* Bit 17 - WNM-Sleep Mode */
		*pos |= 0x08; /* Bit 19 - BSS Transition */
#endif /* CONFIG_WNM */
		break;
	case 3: /* Bits 24-31 */
#ifdef CONFIG_WNM
		*pos |= 0x02; /* Bit 25 - SSID List */
#endif /* CONFIG_WNM */
#ifdef CONFIG_INTERWORKING
		if (wpa_s->conf->interworking)
			*pos |= 0x80; /* Bit 31 - Interworking */
#endif /* CONFIG_INTERWORKING */
		break;
	case 4: /* Bits 32-39 */
#ifdef CONFIG_INTERWORKING
		if (wpa_s->drv_flags / WPA_DRIVER_FLAGS_QOS_MAPPING)
			*pos |= 0x01; /* Bit 32 - QoS Map */
#endif /* CONFIG_INTERWORKING */
		break;
	case 5: /* Bits 40-47 */
#ifdef CONFIG_HS20
		if (wpa_s->conf->hs20)
			*pos |= 0x40; /* Bit 46 - WNM-Notification */
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
		*pos |= 0x40; /* Bit 46 - WNM-Notification */
#endif /* CONFIG_MBO */
		break;
	case 6: /* Bits 48-55 */
		break;
	case 7: /* Bits 56-63 */
		break;
	case 8: /* Bits 64-71 */
		//if (wpa_s->conf->ftm_responder)
			//*pos |= 0x40; /* Bit 70 - FTM responder */
		//if (wpa_s->conf->ftm_initiator)
			//*pos |= 0x80; /* Bit 71 - FTM initiator */
		break;
	}
}


int wpas_build_ext_capab(struct wpa_supplicant *wpa_s, u8 *buf, size_t buflen)
{
	u8 *pos = buf;
	u8 len = 6, i;

	//if (len < 9 &&
	   // (wpa_s->conf->ftm_initiator || wpa_s->conf->ftm_responder))
		//len = 9;
	//if (len < wpa_s->extended_capa_len)
	//	len = wpa_s->extended_capa_len;
	if (buflen < (size_t) len + 2) {
		wpa_printf(MSG_INFO,
			   "Not enough room for building extended capabilities element");
		return -1;
	}

	*pos++ = WLAN_EID_EXT_CAPAB;
	*pos++ = len;
	for (i = 0; i < len; i++, pos++) {
		wpas_ext_capab_byte(wpa_s, pos, i);

		/*if (i < wpa_s->extended_capa_len) {
			*pos &= ~wpa_s->extended_capa_mask[i];
			*pos |= wpa_s->extended_capa[i];
		}*/
	}

	while (len > 0 && buf[1 + len] == 0) {
		len--;
		buf[1] = len;
	}
	if (len == 0)
		return 0;

	return 2 + len;
}


static int wpas_valid_bss(struct wpa_supplicant *wpa_s,
			  struct wpa_bss *test_bss)
{
	struct wpa_bss *bss;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (bss == test_bss)
			return 1;
	}

	return 0;
}


static int wpas_valid_ssid(struct wpa_supplicant *wpa_s,
			   struct wpa_ssid *test_ssid)
{
	struct wpa_ssid *ssid;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid == test_ssid)
			return 1;
	}

	return 0;
}


int wpas_valid_bss_ssid(struct wpa_supplicant *wpa_s, struct wpa_bss *test_bss,
			struct wpa_ssid *test_ssid)
{
	if (test_bss && !wpas_valid_bss(wpa_s, test_bss))
		return 0;

	return test_ssid == NULL || wpas_valid_ssid(wpa_s, test_ssid);
}


void wpas_connect_work_free(struct wpa_connect_work *cwork)
{
	if (cwork == NULL)
		return;
	os_free(cwork);
}


void wpas_connect_work_done(struct wpa_supplicant *wpa_s)
{
	struct wpa_connect_work *cwork;
	struct wpa_radio_work *work = wpa_s->connect_work;

	if (!work)
		return;

	wpa_s->connect_work = NULL;
	cwork = work->ctx;
	work->ctx = NULL;
	wpas_connect_work_free(cwork);
	radio_work_done(work);
}


static void wpas_start_assoc_cb(struct wpa_radio_work *work, int deinit);

/**
 * wpa_supplicant_associate - Request association
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Scan results for the selected BSS, or %NULL if not available
 * @ssid: Configuration data for the selected network
 *
 * This function is used to request %wpa_supplicant to associate with a BSS.
 */
void wpa_supplicant_associate(struct wpa_supplicant *wpa_s,
			      struct wpa_bss *bss, struct wpa_ssid *ssid)
{
	struct wpa_connect_work *cwork;

	wpa_s->own_disconnect_req = 0;

	/*
	 * If we are starting a new connection, any previously pending EAPOL
	 * RX cannot be valid anymore.
	 */
	wpabuf_free(wpa_s->pending_eapol_rx);
	wpa_s->pending_eapol_rx = NULL;

#if 0
	wmm_ac_clear_saved_tspecs(wpa_s);
	wpa_s->reassoc_same_bss = 0;
	wpa_s->reassoc_same_ess = 0;

	if (wpa_s->last_ssid == ssid) {
	    wpa_dbg(wpa_s, MSG_DEBUG, "Re-association to the same ESS");
		wpa_s->reassoc_same_ess = 1;
		if (wpa_s->current_bss && wpa_s->current_bss == bss) {
		    wmm_ac_save_tspecs(wpa_s);
			wpa_s->reassoc_same_bss = 1;
		}
	}
	wpa_s->last_ssid = ssid;
#endif

#ifdef CONFIG_IBSS_RSN
	ibss_rsn_deinit(wpa_s->ibss_rsn);
	wpa_s->ibss_rsn = NULL;
#endif /* CONFIG_IBSS_RSN */

	if (ssid->mode == WPAS_MODE_AP/* || ssid->mode == WPAS_MODE_P2P_GO ||
	    ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION*/) {
#ifdef CONFIG_AP
		if (wpa_supplicant_create_ap(wpa_s, ssid) < 0) {
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
			return;
		}
		wpa_s->current_bss = bss;
#else /* CONFIG_AP */
		wpa_msg(wpa_s, MSG_ERROR, "AP mode support not included in "
			"the build");
#endif /* CONFIG_AP */
		return;
	}


#ifndef CONFIG_ECR6600
    sme_authenticate(wpa_s, bss, ssid);
    return;
#else
	if ((wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME) &&
	    ssid->mode == IEEE80211_MODE_INFRA) {
		sme_authenticate(wpa_s, bss, ssid);
		return;
	}

	if (wpa_s->connect_work) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Reject wpa_supplicant_associate() call since connect_work exist");
		return;
	}

	if (radio_work_pending(wpa_s, "connect")) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Reject wpa_supplicant_associate() call since pending work exist");
		return;
	}

	wpas_abort_ongoing_scan(wpa_s);

	cwork = os_zalloc(sizeof(*cwork));
	if (cwork == NULL)
		return;

	cwork->bss = bss;
	cwork->ssid = ssid;

	if (radio_add_work(wpa_s, bss ? bss->freq : 0, "connect", 1,
			   wpas_start_assoc_cb, cwork) < 0) {
		os_free(cwork);
	}
#endif    
}


static int bss_is_ibss(struct wpa_bss *bss)
{
	return (bss->caps & (IEEE80211_CAP_ESS | IEEE80211_CAP_IBSS)) ==
		IEEE80211_CAP_IBSS;
}


static int drv_supports_vht(struct wpa_supplicant *wpa_s,
			    const struct wpa_ssid *ssid)
{
	enum hostapd_hw_mode hw_mode;
	struct hostapd_hw_modes *mode = NULL;
	u8 channel;
	int i;

#ifdef CONFIG_HT_OVERRIDES
	if (ssid->disable_ht)
		return 0;
#endif /* CONFIG_HT_OVERRIDES */

	hw_mode = ieee80211_freq_to_chan(ssid->frequency, &channel);
	if (hw_mode == NUM_HOSTAPD_MODES)
		return 0;
	for (i = 0; wpa_s->hw.modes && i < wpa_s->hw.num_modes; i++) {
		if (wpa_s->hw.modes[i].mode == hw_mode) {
			mode = &wpa_s->hw.modes[i];
			break;
		}
	}

	if (!mode)
		return 0;

	return mode->vht_capab != 0;
}


void ibss_mesh_setup_freq(struct wpa_supplicant *wpa_s,
			  const struct wpa_ssid *ssid,
			  struct hostapd_freq_params *freq)
{
	enum hostapd_hw_mode hw_mode;
	struct hostapd_hw_modes *mode = NULL;
	int ht40plus[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157,
			   184, 192 };
	int vht80[] = { 36, 52, 100, 116, 132, 149 };
	struct hostapd_channel_data *pri_chan = NULL, *sec_chan = NULL;
	u8 channel;
	int i, chan_idx, ht40 = -1, res, obss_scan = 1;
	unsigned int j, k;
	struct hostapd_freq_params vht_freq;
	int chwidth, seg0, seg1;
	u32 vht_caps = 0;

	freq->freq = ssid->frequency;

	for (j = 0; j < wpa_s->last_scan_res_used; j++) {
		struct wpa_bss *bss = wpa_s->last_scan_res[j];

		if (ssid->mode != WPAS_MODE_IBSS)
			break;

		/* Don't adjust control freq in case of fixed_freq */
		if (ssid->fixed_freq)
			break;

		if (!bss_is_ibss(bss))
			continue;

		if (ssid->ssid_len == bss->ssid_len &&
		    os_memcmp(ssid->ssid, bss->ssid, bss->ssid_len) == 0) {
			wpa_printf(MSG_DEBUG,
				   "IBSS already found in scan results, adjust control freq: %d",
				   bss->freq);
			freq->freq = bss->freq;
			obss_scan = 0;
			break;
		}
	}

	/* For IBSS check HT_IBSS flag */
	if (ssid->mode == WPAS_MODE_IBSS &&
	    !(wpa_s->drv_flags & WPA_DRIVER_FLAGS_HT_IBSS))
		return;

	if (wpa_s->group_cipher == WPA_CIPHER_WEP40 ||
	    wpa_s->group_cipher == WPA_CIPHER_WEP104 ||
	    wpa_s->pairwise_cipher == WPA_CIPHER_TKIP) {
		wpa_printf(MSG_DEBUG,
			   "IBSS: WEP/TKIP detected, do not try to enable HT");
		return;
	}

	hw_mode = ieee80211_freq_to_chan(freq->freq, &channel);
	for (i = 0; wpa_s->hw.modes && i < wpa_s->hw.num_modes; i++) {
		if (wpa_s->hw.modes[i].mode == hw_mode) {
			mode = &wpa_s->hw.modes[i];
			break;
		}
	}

	if (!mode)
		return;

#ifdef CONFIG_HT_OVERRIDES
	if (ssid->disable_ht) {
		freq->ht_enabled = 0;
		return;
	}
#endif /* CONFIG_HT_OVERRIDES */

	freq->ht_enabled = ht_supported(mode);
	if (!freq->ht_enabled)
		return;

	/* Setup higher BW only for 5 GHz */
	if (mode->mode != HOSTAPD_MODE_IEEE80211A)
		return;

	for (chan_idx = 0; chan_idx < mode->num_channels; chan_idx++) {
		pri_chan = &mode->channels[chan_idx];
		if (pri_chan->chan == channel)
			break;
		pri_chan = NULL;
	}
	if (!pri_chan)
		return;

	/* Check primary channel flags */
	if (pri_chan->flag & (HOSTAPD_CHAN_DISABLED | HOSTAPD_CHAN_NO_IR))
		return;

#ifdef CONFIG_HT_OVERRIDES
	if (ssid->disable_ht40)
		return;
#endif /* CONFIG_HT_OVERRIDES */

	/* Check/setup HT40+/HT40- */
	for (j = 0; j < ARRAY_SIZE(ht40plus); j++) {
		if (ht40plus[j] == channel) {
			ht40 = 1;
			break;
		}
	}

	/* Find secondary channel */
	for (i = 0; i < mode->num_channels; i++) {
		sec_chan = &mode->channels[i];
		if (sec_chan->chan == channel + ht40 * 4)
			break;
		sec_chan = NULL;
	}
	if (!sec_chan)
		return;

	/* Check secondary channel flags */
	if (sec_chan->flag & (HOSTAPD_CHAN_DISABLED | HOSTAPD_CHAN_NO_IR))
		return;

	freq->channel = pri_chan->chan;

	if (ht40 == -1) {
		if (!(pri_chan->flag & HOSTAPD_CHAN_HT40MINUS))
			return;
	} else {
		if (!(pri_chan->flag & HOSTAPD_CHAN_HT40PLUS))
			return;
	}
	freq->sec_channel_offset = ht40;

	if (obss_scan) {
		struct wpa_scan_results *scan_res;

		scan_res = wpa_supplicant_get_scan_results(wpa_s, NULL, 0);
		if (scan_res == NULL) {
			/* Back to HT20 */
			freq->sec_channel_offset = 0;
			return;
		}

		res = check_40mhz_5g(mode, scan_res, pri_chan->chan,
				     sec_chan->chan);
		switch (res) {
		case 0:
			/* Back to HT20 */
			freq->sec_channel_offset = 0;
			break;
		case 1:
			/* Configuration allowed */
			break;
		case 2:
			/* Switch pri/sec channels */
			freq->freq = hw_get_freq(mode, sec_chan->chan);
			freq->sec_channel_offset = -freq->sec_channel_offset;
			freq->channel = sec_chan->chan;
			break;
		default:
			freq->sec_channel_offset = 0;
			break;
		}

		wpa_scan_results_free(scan_res);
	}

	wpa_printf(MSG_DEBUG,
		   "IBSS/mesh: setup freq channel %d, sec_channel_offset %d",
		   freq->channel, freq->sec_channel_offset);

	if (!drv_supports_vht(wpa_s, ssid))
		return;

	/* For IBSS check VHT_IBSS flag */
	if (ssid->mode == WPAS_MODE_IBSS &&
	    !(wpa_s->drv_flags & WPA_DRIVER_FLAGS_VHT_IBSS))
		return;

	vht_freq = *freq;

	vht_freq.vht_enabled = vht_supported(mode);
	if (!vht_freq.vht_enabled)
		return;

	/* setup center_freq1, bandwidth */
	for (j = 0; j < ARRAY_SIZE(vht80); j++) {
		if (freq->channel >= vht80[j] &&
		    freq->channel < vht80[j] + 16)
			break;
	}

	if (j == ARRAY_SIZE(vht80))
		return;

	for (i = vht80[j]; i < vht80[j] + 16; i += 4) {
		struct hostapd_channel_data *chan;

		chan = hw_get_channel_chan(mode, i, NULL);
		if (!chan)
			return;

		/* Back to HT configuration if channel not usable */
		if (chan->flag & (HOSTAPD_CHAN_DISABLED | HOSTAPD_CHAN_NO_IR))
			return;
	}

	chwidth = VHT_CHANWIDTH_80MHZ;
	seg0 = vht80[j] + 6;
	seg1 = 0;

	if (ssid->max_oper_chwidth == VHT_CHANWIDTH_80P80MHZ) {
		/* setup center_freq2, bandwidth */
		for (k = 0; k < ARRAY_SIZE(vht80); k++) {
			/* Only accept 80 MHz segments separated by a gap */
			if (j == k || abs(vht80[j] - vht80[k]) == 16)
				continue;
			for (i = vht80[k]; i < vht80[k] + 16; i += 4) {
				struct hostapd_channel_data *chan;

				chan = hw_get_channel_chan(mode, i, NULL);
				if (!chan)
					continue;

				if (chan->flag & (HOSTAPD_CHAN_DISABLED |
						  HOSTAPD_CHAN_NO_IR |
						  HOSTAPD_CHAN_RADAR))
					continue;

				/* Found a suitable second segment for 80+80 */
				chwidth = VHT_CHANWIDTH_80P80MHZ;
				vht_caps |=
					VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
				seg1 = vht80[k] + 6;
			}

			if (chwidth == VHT_CHANWIDTH_80P80MHZ)
				break;
		}
	} else if (ssid->max_oper_chwidth == VHT_CHANWIDTH_160MHZ) {
		if (freq->freq == 5180) {
			chwidth = VHT_CHANWIDTH_160MHZ;
			vht_caps |= VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
			seg0 = 50;
		} else if (freq->freq == 5520) {
			chwidth = VHT_CHANWIDTH_160MHZ;
			vht_caps |= VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
			seg0 = 114;
		}
	}

	if (hostapd_set_freq_params(&vht_freq, mode->mode, freq->freq,
				    freq->channel, freq->ht_enabled,
				    vht_freq.vht_enabled,
				    freq->sec_channel_offset,
				    chwidth, seg0, seg1, vht_caps) != 0)
		return;

	*freq = vht_freq;

	wpa_printf(MSG_DEBUG, "IBSS: VHT setup freq cf1 %d, cf2 %d, bw %d",
		   freq->center_freq1, freq->center_freq2, freq->bandwidth);
}


static void wpas_start_assoc_cb(struct wpa_radio_work *work, int deinit)
{
	struct wpa_connect_work *cwork = work->ctx;
	struct wpa_bss *bss = cwork->bss;
	struct wpa_ssid *ssid = cwork->ssid;
	struct wpa_supplicant *wpa_s = work->wpa_s;
	u8 wpa_ie[200];
	size_t wpa_ie_len;
	//int use_crypt, ret, i, bssid_changed;
	int use_crypt, ret, i;
	int algs = WPA_AUTH_ALG_OPEN;
	unsigned int cipher_pairwise, cipher_group;
	struct wpa_driver_associate_params params;
	int wep_keys_set = 0;
	int assoc_failed = 0;
	//struct wpa_ssid *old_ssid;
	u8 prev_bssid[ETH_ALEN];
#ifdef CONFIG_HT_OVERRIDES
	struct ieee80211_ht_capabilities htcaps;
	struct ieee80211_ht_capabilities htcaps_mask;
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
       struct ieee80211_vht_capabilities vhtcaps;
       struct ieee80211_vht_capabilities vhtcaps_mask;
#endif /* CONFIG_VHT_OVERRIDES */
#ifdef CONFIG_MBO
	const u8 *mbo = NULL;
#endif /* CONFIG_MBO */

	if (deinit) {
		if (work->started) {
			wpa_s->connect_work = NULL;

			/* cancel possible auth. timeout */
			eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s,
					     NULL);
		}
		wpas_connect_work_free(cwork);
		return;
	}

	wpa_s->connect_work = work;

	if (cwork->bss_removed || !wpas_valid_bss_ssid(wpa_s, bss, ssid) ||
	    wpas_network_disabled(wpa_s, ssid)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "BSS/SSID entry for association not valid anymore - drop connection attempt");
		wpas_connect_work_done(wpa_s);
		return;
	}

	os_memcpy(prev_bssid, wpa_s->bssid, ETH_ALEN);
	os_memset(&params, 0, sizeof(params));
	//wpa_s->reassociate = 0;
	wpa_s->eap_expected_failure = 0;
	if (bss &&
	    (!wpas_driver_bss_selection(wpa_s) || wpas_wps_searching(wpa_s))) {
#ifdef CONFIG_IEEE80211R
		const u8 *ie, *md = NULL;
#endif /* CONFIG_IEEE80211R */
		wpa_msg(wpa_s, MSG_INFO, "Trying to associate with " MACSTR
			" (SSID='%s' freq=%d MHz)", MAC2STR(bss->bssid),
			wpa_ssid_txt(bss->ssid, bss->ssid_len), bss->freq);
		//bssid_changed = !is_zero_ether_addr(wpa_s->bssid);
		os_memset(wpa_s->bssid, 0, ETH_ALEN);
		os_memcpy(wpa_s->pending_bssid, bss->bssid, ETH_ALEN);
		//if (bssid_changed)
			//wpas_notify_bssid_changed(wpa_s);
#ifdef CONFIG_IEEE80211R
		ie = wpa_bss_get_ie(bss, WLAN_EID_MOBILITY_DOMAIN);
		if (ie && ie[1] >= MOBILITY_DOMAIN_ID_LEN)
			md = ie + 2;
		wpa_sm_set_ft_params(wpa_s->wpa, ie, ie ? 2 + ie[1] : 0);
		if (md) {
			/* Prepare for the next transition */
			wpa_ft_prepare_auth_request(wpa_s->wpa, ie);
		}
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_WPS
	} else if ((ssid->ssid == NULL || ssid->ssid_len == 0) &&
		   wpa_s->conf->ap_scan == 2 &&
		   (ssid->key_mgmt & WPA_KEY_MGMT_WPS)) {
		/* Use ap_scan==1 style network selection to find the network
		 */
		wpas_connect_work_done(wpa_s);
		wpa_s->scan_req = MANUAL_SCAN_REQ;
		wpa_s->reassociate = 1;
		wpa_supplicant_req_scan(wpa_s, 0, 0);
		return;
#endif /* CONFIG_WPS */
	} else {
		wpa_msg(wpa_s, MSG_INFO, "Trying to associate with SSID '%s'",
			wpa_ssid_txt(ssid->ssid, ssid->ssid_len));
		if (bss)
			os_memcpy(wpa_s->pending_bssid, bss->bssid, ETH_ALEN);
		else
			os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
	}

	//wpa_supplicant_cancel_sched_scan(wpa_s);

	wpa_supplicant_cancel_scan(wpa_s);

	/* Starting new association, so clear the possibly used WPA IE from the
	 * previous association. */
	wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);

#ifdef IEEE8021X_EAPOL
	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if (ssid->leap) {
			if (ssid->non_leap == 0)
				algs = WPA_AUTH_ALG_LEAP;
			else
				algs |= WPA_AUTH_ALG_LEAP;
		}
	}
#endif /* IEEE8021X_EAPOL */
	wpa_dbg(wpa_s, MSG_DEBUG, "Automatic auth_alg selection: 0x%x", algs);
	if (ssid->auth_alg) {
		algs = ssid->auth_alg;
		wpa_dbg(wpa_s, MSG_DEBUG, "Overriding auth_alg selection: "
			"0x%x", algs);
	}

	if (bss && (wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE) ||
		    wpa_bss_get_ie(bss, WLAN_EID_RSN)) &&
	    wpa_key_mgmt_wpa(ssid->key_mgmt)) {
		//int try_opportunistic;
		//try_opportunistic = (ssid->proactive_key_caching < 0 ?
				     //wpa_s->conf->okc :
				     //ssid->proactive_key_caching) &&
			//(ssid->proto & WPA_PROTO_RSN);
		wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_set_suites(wpa_s, bss, ssid,
					      wpa_ie, &wpa_ie_len)) {
			wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to set WPA "
				"key management and encryption suites");
			wpas_connect_work_done(wpa_s);
			return;
		}
	} else if ((ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) && bss &&
		   wpa_key_mgmt_wpa_ieee8021x(ssid->key_mgmt)) {
		/*
		 * Both WPA and non-WPA IEEE 802.1X enabled in configuration -
		 * use non-WPA since the scan results did not indicate that the
		 * AP is using WPA or WPA2.
		 */
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		wpa_ie_len = 0;
		wpa_s->wpa_proto = 0;
	} else if (wpa_key_mgmt_wpa_any(ssid->key_mgmt)) {
		wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_set_suites(wpa_s, NULL, ssid,
					      wpa_ie, &wpa_ie_len)) {
			wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to set WPA "
				"key management and encryption suites (no "
				"scan results)");
			wpas_connect_work_done(wpa_s);
			return;
		}
#ifdef CONFIG_WPS
	} else if (ssid->key_mgmt & WPA_KEY_MGMT_WPS) {
		struct wpabuf *wps_ie;
		wps_ie = wps_build_assoc_req_ie(wpas_wps_get_req_type(ssid));
		if (wps_ie && wpabuf_len(wps_ie) <= sizeof(wpa_ie)) {
			wpa_ie_len = wpabuf_len(wps_ie);
			os_memcpy(wpa_ie, wpabuf_head(wps_ie), wpa_ie_len);
		} else
			wpa_ie_len = 0;
		wpabuf_free(wps_ie);
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		if (!bss || (bss->caps & IEEE80211_CAP_PRIVACY))
			params.wps = WPS_MODE_PRIVACY;
		else
			params.wps = WPS_MODE_OPEN;
		wpa_s->wpa_proto = 0;
#endif /* CONFIG_WPS */
	} else {
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		wpa_ie_len = 0;
		wpa_s->wpa_proto = 0;
	}

#ifdef CONFIG_P2P
	if (wpa_s->global->p2p) {
		u8 *pos;
		size_t len;
		int res;
		pos = wpa_ie + wpa_ie_len;
		len = sizeof(wpa_ie) - wpa_ie_len;
		res = wpas_p2p_assoc_req_ie(wpa_s, bss, pos, len,
					    ssid->p2p_group);
		if (res >= 0)
			wpa_ie_len += res;
	}

	wpa_s->cross_connect_disallowed = 0;
	if (bss) {
		struct wpabuf *p2p;
		p2p = wpa_bss_get_vendor_ie_multi(bss, P2P_IE_VENDOR_TYPE);
		if (p2p) {
			wpa_s->cross_connect_disallowed =
				p2p_get_cross_connect_disallowed(p2p);
			wpabuf_free(p2p);
			wpa_dbg(wpa_s, MSG_DEBUG, "P2P: WLAN AP %s cross "
				"connection",
				wpa_s->cross_connect_disallowed ?
				"disallows" : "allows");
		}
	}

	os_memset(wpa_s->p2p_ip_addr_info, 0, sizeof(wpa_s->p2p_ip_addr_info));
#endif /* CONFIG_P2P */

#ifdef CONFIG_MBO
	if (bss) {
		mbo = wpa_bss_get_vendor_ie(bss, MBO_IE_VENDOR_TYPE);
		if (mbo) {
			int len;

			len = wpas_mbo_supp_op_class_ie(wpa_s, bss->freq,
							wpa_ie + wpa_ie_len,
							sizeof(wpa_ie) -
							wpa_ie_len);
			if (len > 0)
				wpa_ie_len += len;
		}
	}
#endif /* CONFIG_MBO */
#ifdef CONFIG_SAE
		if (wpa_s->key_mgmt & (WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE))
		{
			algs = WPA_AUTH_ALG_SAE;
			wpa_dbg(wpa_s, MSG_DEBUG, "Overriding auth_alg selection: "
				"0x%x", algs);
		}
#endif /* CONFIG_SAE */

	/*
	 * Workaround: Add Extended Capabilities element only if the AP
	 * included this element in Beacon/Probe Response frames. Some older
	 * APs seem to have interoperability issues if this element is
	 * included, so while the standard may require us to include the
	 * element in all cases, it is justifiable to skip it to avoid
	 * interoperability issues.
	 */
	//wpa_drv_get_ext_capa(wpa_s, WPA_IF_STATION);

	if (!bss || wpa_bss_get_ie(bss, WLAN_EID_EXT_CAPAB)) {
		u8 ext_capab[18];
		int ext_capab_len;
		ext_capab_len = wpas_build_ext_capab(wpa_s, ext_capab,
						     sizeof(ext_capab));
		if (ext_capab_len > 0) {
			u8 *pos = wpa_ie;
			if (wpa_ie_len > 0 && pos[0] == WLAN_EID_RSN)
				pos += 2 + pos[1];
			os_memmove(pos + ext_capab_len, pos,
				   wpa_ie_len - (pos - wpa_ie));
			wpa_ie_len += ext_capab_len;
			os_memcpy(pos, ext_capab, ext_capab_len);
		}
	}

#ifdef CONFIG_HS20
	if (is_hs20_network(wpa_s, ssid, bss)) {
		struct wpabuf *hs20;

		hs20 = wpabuf_alloc(20);
		if (hs20) {
			int pps_mo_id = hs20_get_pps_mo_id(wpa_s, ssid);
			size_t len;

			wpas_hs20_add_indication(hs20, pps_mo_id);
			len = sizeof(wpa_ie) - wpa_ie_len;
			if (wpabuf_len(hs20) <= len) {
				os_memcpy(wpa_ie + wpa_ie_len,
					  wpabuf_head(hs20), wpabuf_len(hs20));
				wpa_ie_len += wpabuf_len(hs20);
			}
			wpabuf_free(hs20);

			hs20_configure_frame_filters(wpa_s);
		}
	}
#endif /* CONFIG_HS20 */

#if 0
	if (wpa_s->vendor_elem[VENDOR_ELEM_ASSOC_REQ]) {
		struct wpabuf *buf = wpa_s->vendor_elem[VENDOR_ELEM_ASSOC_REQ];
		size_t len;

		len = sizeof(wpa_ie) - wpa_ie_len;
		if (wpabuf_len(buf) <= len) {
			os_memcpy(wpa_ie + wpa_ie_len,
				  wpabuf_head(buf), wpabuf_len(buf));
			wpa_ie_len += wpabuf_len(buf);
		}
	}
#endif
#ifdef CONFIG_FST
	if (wpa_s->fst_ies) {
		int fst_ies_len = wpabuf_len(wpa_s->fst_ies);

		if (wpa_ie_len + fst_ies_len <= sizeof(wpa_ie)) {
			os_memcpy(wpa_ie + wpa_ie_len,
				  wpabuf_head(wpa_s->fst_ies), fst_ies_len);
			wpa_ie_len += fst_ies_len;
		}
	}
#endif /* CONFIG_FST */

#ifdef CONFIG_MBO
	if (mbo) {
		int len;

		len = wpas_mbo_ie(wpa_s, wpa_ie + wpa_ie_len,
				  sizeof(wpa_ie) - wpa_ie_len);
		if (len >= 0)
			wpa_ie_len += len;
	}
#endif /* CONFIG_MBO */

	wpa_clear_keys(wpa_s, bss ? bss->bssid : NULL);
	use_crypt = 1;
	cipher_pairwise = wpa_s->pairwise_cipher;
	cipher_group = wpa_s->group_cipher;
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE)
			use_crypt = 0;
		if (wpa_set_wep_keys(wpa_s, ssid)) {
			use_crypt = 1;
			wep_keys_set = 1;
		}
	}
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPS)
		use_crypt = 0;

#ifdef IEEE8021X_EAPOL
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if ((ssid->eapol_flags &
		     (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
		      EAPOL_FLAG_REQUIRE_KEY_BROADCAST)) == 0 &&
		    !wep_keys_set) {
			use_crypt = 0;
		} else {
			/* Assume that dynamic WEP-104 keys will be used and
			 * set cipher suites in order for drivers to expect
			 * encryption. */
			cipher_pairwise = cipher_group = WPA_CIPHER_WEP104;
		}
	}
#endif /* IEEE8021X_EAPOL */

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/* Set the key before (and later after) association */
		wpa_supplicant_set_wpa_none_key(wpa_s, ssid);
	}

	wpa_supplicant_set_state(wpa_s, WPA_ASSOCIATING);
	if (bss) {
		params.ssid = bss->ssid;
		params.ssid_len = bss->ssid_len;
		if (!wpas_driver_bss_selection(wpa_s) || ssid->bssid_set) {
			wpa_printf(MSG_DEBUG, "Limit connection to BSSID "
				   MACSTR " freq=%u MHz based on scan results "
				   "(bssid_set=%d)",
				   MAC2STR(bss->bssid), bss->freq,
				   ssid->bssid_set);
			params.bssid = bss->bssid;
			params.freq.freq = bss->freq;
		}
		params.bssid_hint = bss->bssid;
		params.freq_hint = bss->freq;
		//params.pbss = bss_is_pbss(bss);
	} else {
		params.ssid = ssid->ssid;
		params.ssid_len = ssid->ssid_len;
		//params.pbss = (ssid->pbss != 2) ? ssid->pbss : 0;
	}

#if 0
	/* Initial frequency for IBSS/mesh */
	if ((ssid->mode == WPAS_MODE_IBSS || ssid->mode == WPAS_MODE_MESH) &&
	    ssid->frequency > 0 && params.freq.freq == 0)
		ibss_mesh_setup_freq(wpa_s, ssid, &params.freq);

	if (ssid->mode == WPAS_MODE_IBSS) {
		params.fixed_freq = ssid->fixed_freq;
		if (ssid->beacon_int)
			params.beacon_int = ssid->beacon_int;
		else
			params.beacon_int = wpa_s->conf->beacon_int;
	}
#endif
	params.wpa_ie = wpa_ie;
	params.wpa_ie_len = wpa_ie_len;
	params.pairwise_suite = cipher_pairwise;
	params.group_suite = cipher_group;
	params.key_mgmt_suite = wpa_s->key_mgmt;
	params.wpa_proto = wpa_s->wpa_proto;
	params.auth_alg = algs;
	params.mode = ssid->mode;
	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i])
			params.wep_key[i] = ssid->wep_key[i];
		params.wep_key_len[i] = ssid->wep_key_len[i];
	}
	params.wep_tx_keyidx = ssid->wep_tx_keyidx;

#if 0
	if ((wpa_s->drv_flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE) &&
	    (params.key_mgmt_suite == WPA_KEY_MGMT_PSK ||
	     params.key_mgmt_suite == WPA_KEY_MGMT_FT_PSK)) {
		params.passphrase = ssid->passphrase;
		if (ssid->psk_set)
			params.psk = ssid->psk;
	}

	if (wpa_s->conf->key_mgmt_offload) {
		if (params.key_mgmt_suite == WPA_KEY_MGMT_IEEE8021X ||
		    params.key_mgmt_suite == WPA_KEY_MGMT_IEEE8021X_SHA256 ||
		    params.key_mgmt_suite == WPA_KEY_MGMT_IEEE8021X_SUITE_B ||
		    params.key_mgmt_suite == WPA_KEY_MGMT_IEEE8021X_SUITE_B_192)
			params.req_key_mgmt_offload = 0;
		else
			params.req_key_mgmt_offload = 1;

		if ((params.key_mgmt_suite == WPA_KEY_MGMT_PSK ||
		     params.key_mgmt_suite == WPA_KEY_MGMT_PSK_SHA256 ||
		     params.key_mgmt_suite == WPA_KEY_MGMT_FT_PSK) &&
		    ssid->psk_set)
			params.psk = ssid->psk;
	}
#endif

	params.drop_unencrypted = use_crypt;

#ifdef CONFIG_IEEE80211W
	params.mgmt_frame_protection = wpas_get_ssid_pmf(wpa_s, ssid);
	if (params.mgmt_frame_protection != NO_MGMT_FRAME_PROTECTION && bss) {
		const u8 *rsn = wpa_bss_get_ie(bss, WLAN_EID_RSN);
		struct wpa_ie_data ie;
		if (rsn && wpa_parse_wpa_ie(rsn, 2 + rsn[1], &ie) == 0 &&
		    ie.capabilities &
		    (WPA_CAPABILITY_MFPC | WPA_CAPABILITY_MFPR)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Selected AP supports "
				"MFP: require MFP");
			params.mgmt_frame_protection =
				MGMT_FRAME_PROTECTION_REQUIRED;
		}
	}
#endif /* CONFIG_IEEE80211W */

	params.uapsd = -1;

#ifdef CONFIG_HT_OVERRIDES
	os_memset(&htcaps, 0, sizeof(htcaps));
	os_memset(&htcaps_mask, 0, sizeof(htcaps_mask));
	params.htcaps = (u8 *) &htcaps;
	params.htcaps_mask = (u8 *) &htcaps_mask;
	wpa_supplicant_apply_ht_overrides(wpa_s, ssid, &params);
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	os_memset(&vhtcaps, 0, sizeof(vhtcaps));
	os_memset(&vhtcaps_mask, 0, sizeof(vhtcaps_mask));
	params.vhtcaps = &vhtcaps;
	params.vhtcaps_mask = &vhtcaps_mask;
	wpa_supplicant_apply_vht_overrides(wpa_s, ssid, &params);
#endif /* CONFIG_VHT_OVERRIDES */

#ifdef CONFIG_P2P
	/*
	 * If multi-channel concurrency is not supported, check for any
	 * frequency conflict. In case of any frequency conflict, remove the
	 * least prioritized connection.
	 */
	if (wpa_s->num_multichan_concurrent < 2) {
		int freq, num;
		num = get_shared_radio_freqs(wpa_s, &freq, 1);
		if (num > 0 && freq > 0 && freq != params.freq.freq) {
			wpa_printf(MSG_DEBUG,
				   "Assoc conflicting freq found (%d != %d)",
				   freq, params.freq.freq);
			if (wpas_p2p_handle_frequency_conflicts(
				    wpa_s, params.freq.freq, ssid) < 0) {
				wpas_connect_work_done(wpa_s);
				return;
			}
		}
	}
#endif /* CONFIG_P2P */

	/*if (wpa_s->reassoc_same_ess && !is_zero_ether_addr(prev_bssid) &&
	    wpa_s->current_ssid)
		params.prev_bssid = prev_bssid;
    */

	ret = wpa_drv_associate(wpa_s, &params);
	if (ret < 0) {
		wpa_msg(wpa_s, MSG_INFO, "Association request to the driver "
			"failed");
		if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SANE_ERROR_CODES) {
			/*
			 * The driver is known to mean what is saying, so we
			 * can stop right here; the association will not
			 * succeed.
			 */
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
			os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
			return;
		}
		/* try to continue anyway; new association will be tried again
		 * after timeout */
		assoc_failed = 1;
	}

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/* Set the key after the association just in case association
		 * cleared the previously configured key. */
		wpa_supplicant_set_wpa_none_key(wpa_s, ssid);
		/* No need to timeout authentication since there is no key
		 * management. */
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
#ifdef CONFIG_IBSS_RSN
	} else if (ssid->mode == WPAS_MODE_IBSS &&
		   wpa_s->key_mgmt != WPA_KEY_MGMT_NONE &&
		   wpa_s->key_mgmt != WPA_KEY_MGMT_WPA_NONE) {
		/*
		 * RSN IBSS authentication is per-STA and we can disable the
		 * per-BSSID authentication.
		 */
		wpa_supplicant_cancel_auth_timeout(wpa_s);
#endif /* CONFIG_IBSS_RSN */
	} else {
		/* Timeout for IEEE 802.11 authentication and association */
		int timeout = 60;

		if (assoc_failed) {
			/* give IBSS a bit more time */
			timeout = ssid->mode == WPAS_MODE_IBSS ? 10 : 5;
		} else if (wpa_s->conf->ap_scan == 1) {
			/* give IBSS a bit more time */
			timeout = ssid->mode == WPAS_MODE_IBSS ? 20 : 10;
		}
        timeout = 10;  //eswin wpas opt
		wpa_supplicant_req_auth_timeout(wpa_s, timeout, 0);
	}

	if (wep_keys_set &&
	    (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC)) {
		/* Set static WEP keys again */
		wpa_set_wep_keys(wpa_s, ssid);
	}

	//old_ssid = wpa_s->current_ssid;
	wpa_s->current_ssid = ssid;

	if (!wpas_driver_bss_selection(wpa_s) || ssid->bssid_set) {
		wpa_s->current_bss = bss;
#ifdef CONFIG_HS20
		hs20_configure_frame_filters(wpa_s);
#endif /* CONFIG_HS20 */
	}

	wpa_supplicant_rsn_supp_set_config(wpa_s, wpa_s->current_ssid);
	//wpa_supplicant_initiate_eapol(wpa_s);
	//if (old_ssid != wpa_s->current_ssid)
	//	wpas_notify_network_changed(wpa_s);
}


static void wpa_supplicant_clear_connection(struct wpa_supplicant *wpa_s,
					    const u8 *addr)
{
	struct wpa_ssid *old_ssid;

	wpas_connect_work_done(wpa_s);
	wpa_clear_keys(wpa_s, addr);
	old_ssid = wpa_s->current_ssid;
	wpa_supplicant_mark_disassoc(wpa_s);
	wpa_sm_set_config(wpa_s->wpa, NULL);
	if (old_ssid != wpa_s->current_ssid)
		wpas_notify_network_changed(wpa_s);
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);
}


/**
 * wpa_supplicant_deauthenticate - Deauthenticate the current connection
 * @wpa_s: Pointer to wpa_supplicant data
 * @reason_code: IEEE 802.11 reason code for the deauthenticate frame
 *
 * This function is used to request %wpa_supplicant to deauthenticate from the
 * current AP.
 */
void wpa_supplicant_deauthenticate(struct wpa_supplicant *wpa_s,
				   int reason_code)
{
	u8 *addr = NULL;
	union wpa_event_data event;
	int zero_addr = 0;

	wpa_dbg(wpa_s, MSG_DEBUG, "Request to deauthenticate - bssid=" MACSTR
		" pending_bssid=" MACSTR " reason=%d state=%d",
		MAC2STR(wpa_s->bssid), MAC2STR(wpa_s->pending_bssid),
		reason_code, wpa_s->wpa_state);

	if (!is_zero_ether_addr(wpa_s->pending_bssid) &&
	    (wpa_s->wpa_state == WPA_AUTHENTICATING ||
	     wpa_s->wpa_state == WPA_ASSOCIATING))
		addr = wpa_s->pending_bssid;
	else if (!is_zero_ether_addr(wpa_s->bssid))
		addr = wpa_s->bssid;
	else if (wpa_s->wpa_state == WPA_ASSOCIATING) {
		/*
		 * When using driver-based BSS selection, we may not know the
		 * BSSID with which we are currently trying to associate. We
		 * need to notify the driver of this disconnection even in such
		 * a case, so use the all zeros address here.
		 */
		addr = wpa_s->bssid;
		zero_addr = 1;
	}

#ifdef CONFIG_TDLS
	wpa_tdls_teardown_peers(wpa_s->wpa);
#endif /* CONFIG_TDLS */

#ifdef CONFIG_MESH
	if (wpa_s->ifmsh) {
		wpa_msg(wpa_s, MSG_INFO, MESH_GROUP_REMOVED "%s",
			wpa_s->ifname);
		wpa_supplicant_leave_mesh(wpa_s);
	}
#endif /* CONFIG_MESH */

	if (addr) {
		wpa_drv_deauthenticate(wpa_s, addr, reason_code);
		os_memset(&event, 0, sizeof(event));
		event.deauth_info.reason_code = (u16) reason_code;
		event.deauth_info.locally_generated = 1;
		wpa_supplicant_event(wpa_s, EVENT_DEAUTH, &event);
		if (zero_addr)
			addr = NULL;
	}

	wpa_supplicant_clear_connection(wpa_s, addr);
}

static void wpa_supplicant_enable_one_network(struct wpa_supplicant *wpa_s,
					      struct wpa_ssid *ssid)
{
	if (!ssid || !ssid->disabled || ssid->disabled == 2)
		return;

	ssid->disabled = 0;
	//wpas_clear_temp_disabled(wpa_s, ssid, 1);
	//wpas_notify_network_enabled_changed(wpa_s, ssid);

	/*
	 * Try to reassociate since there is no current configuration and a new
	 * network was made available.
	 */
	//if (!wpa_s->current_ssid && !wpa_s->disconnected)
	//	wpa_s->reassociate = 1;
}


/**
 * wpa_supplicant_add_network - Add a new network
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: The new network configuration or %NULL if operation failed
 *
 * This function performs the following operations:
 * 1. Adds a new network.
 * 2. Send network addition notification.
 * 3. Marks the network disabled.
 * 4. Set network default parameters.
 */
struct wpa_ssid * wpa_supplicant_add_network(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;

	ssid = wpa_config_add_network(wpa_s->conf);
	if (!ssid)
		return NULL;
	//wpas_notify_network_added(wpa_s, ssid);
	ssid->disabled = 1;
	wpa_config_set_network_defaults(ssid);

	return ssid;
}


/**
 * wpa_supplicant_remove_network - Remove a configured network based on id
 * @wpa_s: wpa_supplicant structure for a network interface
 * @id: Unique network id to search for
 * Returns: 0 on success, or -1 if the network was not found, -2 if the network
 * could not be removed
 *
 * This function performs the following operations:
 * 1. Removes the network.
 * 2. Send network removal notification.
 * 3. Update internal state machines.
 * 4. Stop any running sched scans.
 */
int wpa_supplicant_remove_network(struct wpa_supplicant *wpa_s, int id)
{
	struct wpa_ssid *ssid;
	//int was_disabled;

	ssid = wpa_config_get_network(wpa_s->conf, id);
	if (!ssid)
		return -1;
	//wpas_notify_network_removed(wpa_s, ssid);

	//if (wpa_s->last_ssid == ssid)
	//	wpa_s->last_ssid = NULL;

	if (ssid == wpa_s->current_ssid || !wpa_s->current_ssid) {
#ifdef CONFIG_SME
		wpa_s->sme.prev_bssid_set = 0;
#endif /* CONFIG_SME */
	}

	if (ssid == wpa_s->current_ssid) {
		wpa_sm_set_config(wpa_s->wpa, NULL);

		if (wpa_s->wpa_state >= WPA_AUTHENTICATING)
			wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
	}

	//was_disabled = ssid->disabled;

	if (wpa_config_remove_network(wpa_s->conf, id) < 0)
		return -2;

/*	if (!was_disabled && wpa_s->sched_scanning) {
		wpa_printf(MSG_DEBUG,
			   "Stop ongoing sched_scan to remove network from filters");
		//wpa_supplicant_cancel_sched_scan(wpa_s);
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
*/
	return 0;
}


/**
 * wpa_supplicant_enable_network - Mark a configured network as enabled
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network or %NULL
 *
 * Enables the specified network or all networks if no network specified.
 */
void wpa_supplicant_enable_network(struct wpa_supplicant *wpa_s,
				   struct wpa_ssid *ssid)
{
	if (ssid == NULL) {
		for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)
			wpa_supplicant_enable_one_network(wpa_s, ssid);
	} else
		wpa_supplicant_enable_one_network(wpa_s, ssid);

	if (/*wpa_s->reassociate && */!wpa_s->disconnected &&
	    (!wpa_s->current_ssid ||
	     wpa_s->wpa_state == WPA_DISCONNECTED ||
	     wpa_s->wpa_state == WPA_SCANNING)) {
		/*if (wpa_s->sched_scanning) {
			wpa_printf(MSG_DEBUG, "Stop ongoing sched_scan to add "
				   "new network to scan filters");
			wpa_supplicant_cancel_sched_scan(wpa_s);
		}*/

		if (wpa_supplicant_fast_associate(wpa_s) != 1) {
			wpa_s->scan_req = NORMAL_SCAN_REQ;
			wpa_supplicant_req_scan(wpa_s, 0, 0);
		}
	}
}


/**
 * wpa_supplicant_disable_network - Mark a configured network as disabled
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network or %NULL
 *
 * Disables the specified network or all networks if no network specified.
 */
void wpa_supplicant_disable_network(struct wpa_supplicant *wpa_s,
				    struct wpa_ssid *ssid)
{
	struct wpa_ssid *other_ssid;
	int was_disabled;

	if (ssid == NULL) {
		//if (wpa_s->sched_scanning)
		//	wpa_supplicant_cancel_sched_scan(wpa_s);

		for (other_ssid = wpa_s->conf->ssid; other_ssid;
		     other_ssid = other_ssid->next) {
			was_disabled = other_ssid->disabled;
			if (was_disabled == 2)
				continue; /* do not change persistent P2P group
					   * data */

			other_ssid->disabled = 1;

			//if (was_disabled != other_ssid->disabled)
			//	wpas_notify_network_enabled_changed(
			//		wpa_s, other_ssid);
		}
		if (wpa_s->current_ssid)
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_DEAUTH_LEAVING);
	} else if (ssid->disabled != 2) {
		if (ssid == wpa_s->current_ssid)
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_DEAUTH_LEAVING);

		was_disabled = ssid->disabled;

		ssid->disabled = 1;

		if (was_disabled != ssid->disabled) {
			//wpas_notify_network_enabled_changed(wpa_s, ssid);
			/*if (wpa_s->sched_scanning) {
				wpa_printf(MSG_DEBUG, "Stop ongoing sched_scan "
					   "to remove network from filters");
				//wpa_supplicant_cancel_sched_scan(wpa_s);
				wpa_supplicant_req_scan(wpa_s, 0, 0);
			}*/
		}
	}
}


/**
 * wpa_supplicant_select_network - Attempt association with a network
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network or %NULL for any network
 */
void wpa_supplicant_select_network(struct wpa_supplicant *wpa_s,
				   struct wpa_ssid *ssid)
{

	//struct wpa_ssid *other_ssid;
	int disconnected = 0;

	if (ssid && ssid != wpa_s->current_ssid && wpa_s->current_ssid) {
		if (wpa_s->wpa_state >= WPA_AUTHENTICATING)
			wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(
			wpa_s, WLAN_REASON_DEAUTH_LEAVING);
		disconnected = 1;
	}

	//if (ssid)
	//	wpas_clear_temp_disabled(wpa_s, ssid, 1);

	/*
	 * Mark all other networks disabled or mark all networks enabled if no
	 * network specified.
	 */
	#if 0 
	for (other_ssid = wpa_s->conf->ssid; other_ssid;
	     other_ssid = other_ssid->next) {
		int was_disabled = other_ssid->disabled;
		if (was_disabled == 2)
			continue; /* do not change persistent P2P group data */

		other_ssid->disabled = ssid ? (ssid->id != other_ssid->id) : 0;
		//if (was_disabled && !other_ssid->disabled)
		//	wpas_clear_temp_disabled(wpa_s, other_ssid, 0);

		//if (was_disabled != other_ssid->disabled)
		//	wpas_notify_network_enabled_changed(wpa_s, other_ssid);
	}
    #else
    wpa_s->conf->ssid->disabled = 0;
    #endif

	if (ssid && ssid == wpa_s->current_ssid && wpa_s->current_ssid &&
	    wpa_s->wpa_state >= WPA_AUTHENTICATING) {
		/* We are already associated with the selected network */
		wpa_printf(MSG_DEBUG, "Already associated with the "
			   "selected network - do nothing");
		return;
	}

	if (ssid) {
		wpa_s->current_ssid = ssid;
		wpa_s->connect_without_scan =
			(ssid->mode == WPAS_MODE_MESH) ? ssid : NULL;

		/*
		 * Don't optimize next scan freqs since a new ESS has been
		 * selected.
		 */
		//os_free(wpa_s->next_scan_freqs);
		//wpa_s->next_scan_freqs = NULL;
	} else {
		wpa_s->connect_without_scan = NULL;
	}

	wpa_s->disconnected = 0;
	//wpa_s->reassociate = 1;

	if (wpa_s->connect_without_scan ||
	    wpa_supplicant_fast_associate(wpa_s) != 1) {
		wpa_s->scan_req = NORMAL_SCAN_REQ;
		wpas_scan_reset_sched_scan(wpa_s);
		wpa_supplicant_req_scan(wpa_s, 0, disconnected ? 100000 : 0);
	}

	//if (ssid)
	//	wpas_notify_network_selected(wpa_s, ssid);
}


/**
 * wpas_set_pkcs11_engine_and_module_path - Set PKCS #11 engine and module path
 * @wpa_s: wpa_supplicant structure for a network interface
 * @pkcs11_engine_path: PKCS #11 engine path or NULL
 * @pkcs11_module_path: PKCS #11 module path or NULL
 * Returns: 0 on success; -1 on failure
 *
 * Sets the PKCS #11 engine and module path. Both have to be NULL or a valid
 * path. If resetting the EAPOL state machine with the new PKCS #11 engine and
 * module path fails the paths will be reset to the default value (NULL).
 */
int wpas_set_pkcs11_engine_and_module_path(struct wpa_supplicant *wpa_s,
					   const char *pkcs11_engine_path,
					   const char *pkcs11_module_path)
{
#if 0
	char *pkcs11_engine_path_copy = NULL;
	char *pkcs11_module_path_copy = NULL;

	if (pkcs11_engine_path != NULL) {
		pkcs11_engine_path_copy = os_strdup(pkcs11_engine_path);
		if (pkcs11_engine_path_copy == NULL)
			return -1;
	}
	if (pkcs11_module_path != NULL) {
		pkcs11_module_path_copy = os_strdup(pkcs11_module_path);
		if (pkcs11_module_path_copy == NULL) {
			os_free(pkcs11_engine_path_copy);
			return -1;
		}
	}

	os_free(wpa_s->conf->pkcs11_engine_path);
	os_free(wpa_s->conf->pkcs11_module_path);
	wpa_s->conf->pkcs11_engine_path = pkcs11_engine_path_copy;
	wpa_s->conf->pkcs11_module_path = pkcs11_module_path_copy;
#endif
	return 0;
}


/**
 * wpa_supplicant_set_ap_scan - Set AP scan mode for interface
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ap_scan: AP scan mode
 * Returns: 0 if succeed or -1 if ap_scan has an invalid value
 *
 */
int wpa_supplicant_set_ap_scan(struct wpa_supplicant *wpa_s, int ap_scan)
{

	int old_ap_scan;

	if (ap_scan < 0 || ap_scan > 2)
		return -1;

	if (ap_scan == 2 && os_strcmp(wpa_s->driver->name, "nl80211") == 0) {
		wpa_printf(MSG_INFO,
			   "Note: nl80211 driver interface is not designed to be used with ap_scan=2; this can result in connection failures");
	}

#ifdef ANDROID
	if (ap_scan == 2 && ap_scan != wpa_s->conf->ap_scan &&
	    wpa_s->wpa_state >= WPA_ASSOCIATING &&
	    wpa_s->wpa_state < WPA_COMPLETED) {
		wpa_printf_error(MSG_ERROR, "ap_scan = %d (%d) rejected while "
			   "associating", wpa_s->conf->ap_scan, ap_scan);
		return 0;
	}
#endif /* ANDROID */

	old_ap_scan = wpa_s->conf->ap_scan;
	wpa_s->conf->ap_scan = ap_scan;

	if (old_ap_scan != wpa_s->conf->ap_scan)
		wpas_notify_ap_scan_changed(wpa_s);

	return 0;
}


/**
 * wpa_supplicant_set_bss_expiration_age - Set BSS entry expiration age
 * @wpa_s: wpa_supplicant structure for a network interface
 * @expire_age: Expiration age in seconds
 * Returns: 0 if succeed or -1 if expire_age has an invalid value
 *
 */
int wpa_supplicant_set_bss_expiration_age(struct wpa_supplicant *wpa_s,
					  unsigned int bss_expire_age)
{
	if (bss_expire_age < 10) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid bss expiration age %u",
			bss_expire_age);
		return -1;
	}
	wpa_msg(wpa_s, MSG_DEBUG, "Setting bss expiration age: %d sec",
		bss_expire_age);
	wpa_s->conf->bss_expiration_age = bss_expire_age;

	return 0;
}


/**
 * wpa_supplicant_set_bss_expiration_count - Set BSS entry expiration scan count
 * @wpa_s: wpa_supplicant structure for a network interface
 * @expire_count: number of scans after which an unseen BSS is reclaimed
 * Returns: 0 if succeed or -1 if expire_count has an invalid value
 *
 */
int wpa_supplicant_set_bss_expiration_count(struct wpa_supplicant *wpa_s,
					    unsigned int bss_expire_count)
{
	if (bss_expire_count < 1) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid bss expiration count %u",
			bss_expire_count);
		return -1;
	}
	wpa_msg(wpa_s, MSG_DEBUG, "Setting bss expiration scan count: %u",
		bss_expire_count);
	wpa_s->conf->bss_expiration_scan_count = bss_expire_count;

	return 0;
}


/**
 * wpa_supplicant_set_scan_interval - Set scan interval
 * @wpa_s: wpa_supplicant structure for a network interface
 * @scan_interval: scan interval in seconds
 * Returns: 0 if succeed or -1 if scan_interval has an invalid value
 *
 */
int wpa_supplicant_set_scan_interval(struct wpa_supplicant *wpa_s,
				     int scan_interval)
{
	if (scan_interval < 0) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid scan interval %d",
			scan_interval);
		return -1;
	}
	wpa_msg(wpa_s, MSG_DEBUG, "Setting scan interval: %d sec",
		scan_interval);
	wpa_supplicant_update_scan_int(wpa_s, scan_interval);

	return 0;
}


/**
 * wpa_supplicant_get_ssid - Get a pointer to the current network structure
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: A pointer to the current network structure or %NULL on failure
 */
struct wpa_ssid * wpa_supplicant_get_ssid(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *entry;
	u8 ssid[SSID_MAX_LEN];
	int res;
	size_t ssid_len;
	u8 bssid[ETH_ALEN];
	int wired;

	res = wpa_drv_get_ssid(wpa_s, ssid);
	if (res < 0) {
		wpa_msg(wpa_s, MSG_WARNING, "Could not read SSID from "
			"driver");
		return NULL;
	}
	ssid_len = res;

	if (wpa_drv_get_bssid(wpa_s, bssid) < 0) {
		wpa_msg(wpa_s, MSG_WARNING, "Could not read BSSID from "
			"driver");
		return NULL;
	}

	wired = wpa_s->conf->ap_scan == 0 &&
		(wpa_s->drv_flags & WPA_DRIVER_FLAGS_WIRED);

	entry = wpa_s->conf->ssid;
	while (entry) {
		if (!wpas_network_disabled(wpa_s, entry) &&
		    ((ssid_len == entry->ssid_len &&
		      os_memcmp(ssid, entry->ssid, ssid_len) == 0) || wired) &&
		    (!entry->bssid_set ||
		     os_memcmp(bssid, entry->bssid, ETH_ALEN) == 0))
			return entry;
#ifdef CONFIG_WPS
		if (!wpas_network_disabled(wpa_s, entry) &&
		    (entry->key_mgmt & WPA_KEY_MGMT_WPS) &&
		    (entry->ssid == NULL || entry->ssid_len == 0) &&
		    (!entry->bssid_set ||
		     os_memcmp(bssid, entry->bssid, ETH_ALEN) == 0))
			return entry;
#endif /* CONFIG_WPS */

		if (!wpas_network_disabled(wpa_s, entry) && entry->bssid_set &&
		    entry->ssid_len == 0 &&
		    os_memcmp(bssid, entry->bssid, ETH_ALEN) == 0)
			return entry;

		entry = entry->next;
	}

	return NULL;
}


static int select_driver(struct wpa_supplicant *wpa_s, int i)
{
	struct wpa_global *global = wpa_s->global;

	if (wpa_drivers[i]->global_init && global->drv_priv[i] == NULL) {
		global->drv_priv[i] = wpa_drivers[i]->global_init(global);
		if (global->drv_priv[i] == NULL) {
			wpa_printf_error(MSG_ERROR, "Failed to initialize driver "
				   "'%s'", wpa_drivers[i]->name);
			return -1;
		}
	}

	wpa_s->driver = wpa_drivers[i];
	wpa_s->global_drv_priv = global->drv_priv[i];

	return 0;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int wpa_supplicant_set_driver(struct wpa_supplicant *wpa_s,
				     const char *name)
{
	int i;
	//size_t len;
	//const char *pos, *driver = name;

	if (wpa_s == NULL)
		return -1;

	if (wpa_drivers[0] == NULL) {
		wpa_msg(wpa_s, MSG_ERROR, "No driver interfaces build into "
			"wpa_supplicant");
		return -1;
	}

	/* default to first driver in the list */
	return select_driver(wpa_s, 0);
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


/**
 * wpa_supplicant_rx_eapol - Deliver a received EAPOL frame to wpa_supplicant
 * @ctx: Context pointer (wpa_s); this is the ctx variable registered
 *	with struct wpa_driver_ops::init()
 * @src_addr: Source address of the EAPOL frame
 * @buf: EAPOL data starting from the EAPOL header (i.e., no Ethernet header)
 * @len: Length of the EAPOL data
 *
 * This function is called for each received EAPOL frame. Most driver
 * interfaces rely on more generic OS mechanism for receiving frames through
 * l2_packet, but if such a mechanism is not available, the driver wrapper may
 * take care of received EAPOL frames and deliver them to the core supplicant
 * code by calling this function.
 */
void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr,
			     const u8 *buf, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_dbg(wpa_s, MSG_DEBUG, "RX EAPOL from " MACSTR, MAC2STR(src_addr));
	wpa_hexdump(MSG_MSGDUMP, "RX EAPOL", buf, len);


	if (wpa_s->wpa_state < WPA_ASSOCIATED ||
	    (wpa_s->last_eapol_matches_bssid &&
#ifdef CONFIG_AP
	     !wpa_s->ap_iface &&
#endif /* CONFIG_AP */
	     os_memcmp(src_addr, wpa_s->bssid, ETH_ALEN) != 0)) {
		/*
		 * There is possible race condition between receiving the
		 * association event and the EAPOL frame since they are coming
		 * through different paths from the driver. In order to avoid
		 * issues in trying to process the EAPOL frame before receiving
		 * association information, lets queue it for processing until
		 * the association event is received. This may also be needed in
		 * driver-based roaming case, so also use src_addr != BSSID as a
		 * trigger if we have previously confirmed that the
		 * Authenticator uses BSSID as the src_addr (which is not the
		 * case with wired IEEE 802.1X).
		 */
		wpa_dbg(wpa_s, MSG_DEBUG, "Not associated - Delay processing "
			"of received EAPOL frame (state=%d bssid=" MACSTR ")",
			wpa_s->wpa_state,
			MAC2STR(wpa_s->bssid));
		wpabuf_free(wpa_s->pending_eapol_rx);
		wpa_s->pending_eapol_rx = wpabuf_alloc_copy(buf, len);
		if (wpa_s->pending_eapol_rx) {
			os_get_reltime(&wpa_s->pending_eapol_rx_time);
			os_memcpy(wpa_s->pending_eapol_rx_src, src_addr,
				  ETH_ALEN);
		}
		return;
	}

	wpa_s->last_eapol_matches_bssid =
		os_memcmp(src_addr, wpa_s->bssid, ETH_ALEN) == 0;

#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_supplicant_ap_rx_eapol(wpa_s, src_addr, buf, len);
		return;
	}
#endif /* CONFIG_AP */

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Ignored received EAPOL frame since "
			"no key management is configured");
		return;
	}

	if (wpa_s->eapol_received == 0 &&
	    (!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE) ||
	     !wpa_key_mgmt_wpa_psk(wpa_s->key_mgmt) ||
	     wpa_s->wpa_state != WPA_COMPLETED) &&
	    (wpa_s->current_ssid == NULL ||
	     wpa_s->current_ssid->mode != IEEE80211_MODE_IBSS)) {
		/* Timeout for completing IEEE 802.1X and WPA authentication */
		int timeout = 10;

        #if 0
		if (wpa_key_mgmt_wpa_ieee8021x(wpa_s->key_mgmt) ||
		    wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA ||
		    wpa_s->key_mgmt == WPA_KEY_MGMT_WPS) {
			/* Use longer timeout for IEEE 802.1X/EAP */
			timeout = 70;
		}
        #endif

#ifdef CONFIG_WPS
		if (wpa_s->current_ssid && wpa_s->current_bss &&
		    (wpa_s->current_ssid->key_mgmt & WPA_KEY_MGMT_WPS) &&
		    eap_is_wps_pin_enrollee(&wpa_s->current_ssid->eap)) {
			/*
			 * Use shorter timeout if going through WPS AP iteration
			 * for PIN config method with an AP that does not
			 * advertise Selected Registrar.
			 */
			struct wpabuf *wps_ie;

			wps_ie = wpa_bss_get_vendor_ie_multi(
				wpa_s->current_bss, WPS_IE_VENDOR_TYPE);
			if (wps_ie &&
			    !wps_is_addr_authorized(wps_ie, wpa_s->own_addr, 1))
				timeout = 10;
			wpabuf_free(wps_ie);
		}
#endif /* CONFIG_WPS */

		wpa_supplicant_req_auth_timeout(wpa_s, timeout, 0);
	}
	wpa_s->eapol_received++;

	if (wpa_s->countermeasures) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Countermeasures - dropped "
			"EAPOL packet");
		return;
	}

	/* Source address of the incoming EAPOL frame could be compared to the
	 * current BSSID. However, it is possible that a centralized
	 * Authenticator could be using another MAC address than the BSSID of
	 * an AP, so just allow any address to be used for now. The replies are
	 * still sent to the current BSSID (if available), though. */

	//os_memcpy(wpa_s->last_eapol_src, src_addr, ETH_ALEN);
	//wpa_drv_poll(wpa_s);
	//if (!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE))
		wpa_sm_rx_eapol(wpa_s->wpa, src_addr, buf, len);
}

void wpa_update_mac_addr(int vif, unsigned char * mac)
{
    extern struct wpa_supplicant *wpa_get_ctrl_iface(int vif);
    struct wpa_supplicant *wpa_s;

    if (vif > 1 || vif < 0)
        return;
    wpa_s = wpa_get_ctrl_iface(vif);
    wpa_drv_set_mac_addr(wpa_s, mac);
    os_memcpy(wpa_s->own_addr, mac, ETH_ALEN);
//    l2_packet_set_own_addr(wpa_s->l2, wpa_s->own_addr);
    wpa_sm_set_own_addr(wpa_s->wpa, wpa_s->own_addr);
}

int wpa_supplicant_update_mac_addr(struct wpa_supplicant *wpa_s)
{
	if (/*(!wpa_s->p2p_mgmt ||
	     !(wpa_s->drv_flags & WPA_DRIVER_FLAGS_DEDICATED_P2P_DEVICE)) &&*/
	    !(wpa_s->drv_flags & WPA_DRIVER_FLAGS_P2P_DEDICATED_INTERFACE)) {
		l2_packet_deinit(wpa_s->l2);
		wpa_s->l2 = l2_packet_init(wpa_s->ifname,
					   wpa_drv_get_mac_addr(wpa_s),
					   ETH_P_EAPOL,
					   wpa_supplicant_rx_eapol, wpa_s, 0);
		if (wpa_s->l2 == NULL)
			return -1;
	} /*else {
		const u8 *addr = wpa_drv_get_mac_addr(wpa_s);
		if (addr)
			os_memcpy(wpa_s->own_addr, addr, ETH_ALEN);
	}*/

	if (wpa_s->l2 && l2_packet_get_own_addr(wpa_s->l2, wpa_s->own_addr)) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to get own L2 address");
		return -1;
	}

	wpa_sm_set_own_addr(wpa_s->wpa, wpa_s->own_addr);

	return 0;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpa_supplicant_rx_eapol_bridge(void *ctx, const u8 *src_addr,
					   const u8 *buf, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	const struct l2_ethhdr *eth;

	if (len < sizeof(*eth))
		return;
	eth = (const struct l2_ethhdr *) buf;

	if (os_memcmp(eth->h_dest, wpa_s->own_addr, ETH_ALEN) != 0 &&
	    !(eth->h_dest[0] & 0x01)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "RX EAPOL from " MACSTR " to " MACSTR
			" (bridge - not for this interface - ignore)",
			MAC2STR(src_addr), MAC2STR(eth->h_dest));
		return;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "RX EAPOL from " MACSTR " to " MACSTR
		" (bridge)", MAC2STR(src_addr), MAC2STR(eth->h_dest));
	wpa_supplicant_rx_eapol(wpa_s, src_addr, buf + sizeof(*eth),
				len - sizeof(*eth));
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


/**
 * wpa_supplicant_driver_init - Initialize driver interface parameters
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: 0 on success, -1 on failure
 *
 * This function is called to initialize driver interface parameters.
 * wpa_drv_init() must have been called before this function to initialize the
 * driver interface.
 */
int wpa_supplicant_driver_init(struct wpa_supplicant *wpa_s)
{
	//static int interface_count = 0;

	if (wpa_supplicant_update_mac_addr(wpa_s) < 0)
		return -1;

	wpa_dbg(wpa_s, MSG_DEBUG, "Own MAC address: " MACSTR,
		MAC2STR(wpa_s->own_addr));
	wpa_sm_set_own_addr(wpa_s->wpa, wpa_s->own_addr);

	wpa_clear_keys(wpa_s, NULL);

	/* Make sure that TKIP countermeasures are not left enabled (could
	 * happen if wpa_supplicant is killed during countermeasures. */
	//wpa_drv_set_countermeasures(wpa_s, 0);

	wpa_dbg(wpa_s, MSG_DEBUG, "RSN: flushing PMKID list in the driver");
	wpa_drv_flush_pmkid(wpa_s);

	//wpa_s->prev_scan_ssid = WILDCARD_SSID_SCAN;
	//wpa_s->prev_scan_wildcard = 0;

#if 0
	if (wpa_supplicant_enabled_networks(wpa_s)) {
		/*if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED) {
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
			interface_count = 0;
		}*/
#ifndef ANDROID
#if 0
		if (/*!wpa_s->p2p_mgmt &&*/
		    wpa_supplicant_delayed_sched_scan(wpa_s,
						      interface_count % 3,
						      100000))
#endif						      
			wpa_supplicant_req_scan(wpa_s, interface_count % 3,
						100000);
#endif /* ANDROID */
		interface_count++;
	} else
#endif	
		wpa_supplicant_set_state(wpa_s, WPA_INACTIVE);

	return 0;
}


static struct wpa_supplicant *
wpa_supplicant_alloc(struct wpa_supplicant *parent)
{
	struct wpa_supplicant *wpa_s;

	wpa_s = os_zalloc(sizeof(*wpa_s));
	if (wpa_s == NULL)
		return NULL;
	wpa_s->scan_req = INITIAL_SCAN_REQ;
	wpa_s->scan_interval = 5;
	wpa_s->new_connection = 1;
	//wpa_s->sched_scanning = 0;

	//dl_list_init(&wpa_s->bss_tmp_disallowed);

	return wpa_s;
}


#ifdef CONFIG_HT_OVERRIDES

static int wpa_set_htcap_mcs(struct wpa_supplicant *wpa_s,
			     struct ieee80211_ht_capabilities *htcaps,
			     struct ieee80211_ht_capabilities *htcaps_mask,
			     const char *ht_mcs)
{
	/* parse ht_mcs into hex array */
	int i;
	const char *tmp = ht_mcs;
	char *end = NULL;

	/* If ht_mcs is null, do not set anything */
	if (!ht_mcs)
		return 0;

	/* This is what we are setting in the kernel */
	os_memset(&htcaps->supported_mcs_set, 0, IEEE80211_HT_MCS_MASK_LEN);

	wpa_msg(wpa_s, MSG_DEBUG, "set_htcap, ht_mcs -:%s:-", ht_mcs);

	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		errno = 0;
		long v = strtol(tmp, &end, 16);
		if (errno == 0) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"htcap value[%i]: %ld end: %p  tmp: %p",
				i, v, end, tmp);
			if (end == tmp)
				break;

			htcaps->supported_mcs_set[i] = v;
			tmp = end;
		} else {
			wpa_msg(wpa_s, MSG_ERROR,
				"Failed to parse ht-mcs: %s, error: %s\n",
				ht_mcs, strerror(errno));
			return -1;
		}
	}

	/*
	 * If we were able to parse any values, then set mask for the MCS set.
	 */
	if (i) {
		os_memset(&htcaps_mask->supported_mcs_set, 0xff,
			  IEEE80211_HT_MCS_MASK_LEN - 1);
		/* skip the 3 reserved bits */
		htcaps_mask->supported_mcs_set[IEEE80211_HT_MCS_MASK_LEN - 1] =
			0x1f;
	}

	return 0;
}


static int wpa_disable_max_amsdu(struct wpa_supplicant *wpa_s,
				 struct ieee80211_ht_capabilities *htcaps,
				 struct ieee80211_ht_capabilities *htcaps_mask,
				 int disabled)
{
	le16 msk;

	wpa_msg(wpa_s, MSG_DEBUG, "set_disable_max_amsdu: %d", disabled);

	if (disabled == -1)
		return 0;

	msk = host_to_le16(HT_CAP_INFO_MAX_AMSDU_SIZE);
	htcaps_mask->ht_capabilities_info |= msk;
	if (disabled)
		htcaps->ht_capabilities_info &= msk;
	else
		htcaps->ht_capabilities_info |= msk;

	return 0;
}


static int wpa_set_ampdu_factor(struct wpa_supplicant *wpa_s,
				struct ieee80211_ht_capabilities *htcaps,
				struct ieee80211_ht_capabilities *htcaps_mask,
				int factor)
{
	wpa_msg(wpa_s, MSG_DEBUG, "set_ampdu_factor: %d", factor);

	if (factor == -1)
		return 0;

	if (factor < 0 || factor > 3) {
		wpa_msg(wpa_s, MSG_ERROR, "ampdu_factor: %d out of range. "
			"Must be 0-3 or -1", factor);
		return -EINVAL;
	}

	htcaps_mask->a_mpdu_params |= 0x3; /* 2 bits for factor */
	htcaps->a_mpdu_params &= ~0x3;
	htcaps->a_mpdu_params |= factor & 0x3;

	return 0;
}


static int wpa_set_ampdu_density(struct wpa_supplicant *wpa_s,
				 struct ieee80211_ht_capabilities *htcaps,
				 struct ieee80211_ht_capabilities *htcaps_mask,
				 int density)
{
	wpa_msg(wpa_s, MSG_DEBUG, "set_ampdu_density: %d", density);

	if (density == -1)
		return 0;

	if (density < 0 || density > 7) {
		wpa_msg(wpa_s, MSG_ERROR,
			"ampdu_density: %d out of range. Must be 0-7 or -1.",
			density);
		return -EINVAL;
	}

	htcaps_mask->a_mpdu_params |= 0x1C;
	htcaps->a_mpdu_params &= ~(0x1C);
	htcaps->a_mpdu_params |= (density << 2) & 0x1C;

	return 0;
}


static int wpa_set_disable_ht40(struct wpa_supplicant *wpa_s,
				struct ieee80211_ht_capabilities *htcaps,
				struct ieee80211_ht_capabilities *htcaps_mask,
				int disabled)
{
	/* Masking these out disables HT40 */
	le16 msk = host_to_le16(HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET |
				HT_CAP_INFO_SHORT_GI40MHZ);

	wpa_msg(wpa_s, MSG_DEBUG, "set_disable_ht40: %d", disabled);

	if (disabled)
		htcaps->ht_capabilities_info &= ~msk;
	else
		htcaps->ht_capabilities_info |= msk;

	htcaps_mask->ht_capabilities_info |= msk;

	return 0;
}


static int wpa_set_disable_sgi(struct wpa_supplicant *wpa_s,
			       struct ieee80211_ht_capabilities *htcaps,
			       struct ieee80211_ht_capabilities *htcaps_mask,
			       int disabled)
{
	/* Masking these out disables SGI */
	le16 msk = host_to_le16(HT_CAP_INFO_SHORT_GI20MHZ |
				HT_CAP_INFO_SHORT_GI40MHZ);

	wpa_msg(wpa_s, MSG_DEBUG, "set_disable_sgi: %d", disabled);

	if (disabled)
		htcaps->ht_capabilities_info &= ~msk;
	else
		htcaps->ht_capabilities_info |= msk;

	htcaps_mask->ht_capabilities_info |= msk;

	return 0;
}


static int wpa_set_disable_ldpc(struct wpa_supplicant *wpa_s,
			       struct ieee80211_ht_capabilities *htcaps,
			       struct ieee80211_ht_capabilities *htcaps_mask,
			       int disabled)
{
	/* Masking these out disables LDPC */
	le16 msk = host_to_le16(HT_CAP_INFO_LDPC_CODING_CAP);

	wpa_msg(wpa_s, MSG_DEBUG, "set_disable_ldpc: %d", disabled);

	if (disabled)
		htcaps->ht_capabilities_info &= ~msk;
	else
		htcaps->ht_capabilities_info |= msk;

	htcaps_mask->ht_capabilities_info |= msk;

	return 0;
}


void wpa_supplicant_apply_ht_overrides(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
	struct wpa_driver_associate_params *params)
{
	struct ieee80211_ht_capabilities *htcaps;
	struct ieee80211_ht_capabilities *htcaps_mask;

	if (!ssid)
		return;

	params->disable_ht = ssid->disable_ht;
	if (!params->htcaps || !params->htcaps_mask)
		return;

	htcaps = (struct ieee80211_ht_capabilities *) params->htcaps;
	htcaps_mask = (struct ieee80211_ht_capabilities *) params->htcaps_mask;
	wpa_set_htcap_mcs(wpa_s, htcaps, htcaps_mask, ssid->ht_mcs);
	wpa_disable_max_amsdu(wpa_s, htcaps, htcaps_mask,
			      ssid->disable_max_amsdu);
	wpa_set_ampdu_factor(wpa_s, htcaps, htcaps_mask, ssid->ampdu_factor);
	wpa_set_ampdu_density(wpa_s, htcaps, htcaps_mask, ssid->ampdu_density);
	wpa_set_disable_sgi(wpa_s, htcaps, htcaps_mask, ssid->disable_sgi);
	wpa_set_disable_ht40(wpa_s, htcaps, htcaps_mask, ssid->disable_ht40);
	wpa_set_disable_ldpc(wpa_s, htcaps, htcaps_mask, ssid->disable_ldpc);

	if (ssid->ht40_intolerant) {
		le16 bit = host_to_le16(HT_CAP_INFO_40MHZ_INTOLERANT);
		htcaps->ht_capabilities_info |= bit;
		htcaps_mask->ht_capabilities_info |= bit;
	}
}

#endif /* CONFIG_HT_OVERRIDES */


#ifdef CONFIG_VHT_OVERRIDES
void wpa_supplicant_apply_vht_overrides(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
	struct wpa_driver_associate_params *params)
{
	struct ieee80211_vht_capabilities *vhtcaps;
	struct ieee80211_vht_capabilities *vhtcaps_mask;

	if (!ssid)
		return;

	params->disable_vht = ssid->disable_vht;

	vhtcaps = (void *) params->vhtcaps;
	vhtcaps_mask = (void *) params->vhtcaps_mask;

	if (!vhtcaps || !vhtcaps_mask)
		return;

	vhtcaps->vht_capabilities_info = host_to_le32(ssid->vht_capa);
	vhtcaps_mask->vht_capabilities_info = host_to_le32(ssid->vht_capa_mask);

#ifdef CONFIG_HT_OVERRIDES
	/* if max ampdu is <= 3, we have to make the HT cap the same */
	if (ssid->vht_capa_mask & VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MAX) {
		int max_ampdu;

		max_ampdu = (ssid->vht_capa &
			     VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MAX) >>
			VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MAX_SHIFT;

		max_ampdu = max_ampdu < 3 ? max_ampdu : 3;
		wpa_set_ampdu_factor(wpa_s,
				     (void *) params->htcaps,
				     (void *) params->htcaps_mask,
				     max_ampdu);
	}
#endif /* CONFIG_HT_OVERRIDES */

#define OVERRIDE_MCS(i)							\
	if (ssid->vht_tx_mcs_nss_ ##i >= 0) {				\
		vhtcaps_mask->vht_supported_mcs_set.tx_map |=		\
			host_to_le16(3 << 2 * (i - 1));			\
		vhtcaps->vht_supported_mcs_set.tx_map |=		\
			host_to_le16(ssid->vht_tx_mcs_nss_ ##i <<	\
				     2 * (i - 1));			\
	}								\
	if (ssid->vht_rx_mcs_nss_ ##i >= 0) {				\
		vhtcaps_mask->vht_supported_mcs_set.rx_map |=		\
			host_to_le16(3 << 2 * (i - 1));			\
		vhtcaps->vht_supported_mcs_set.rx_map |=		\
			host_to_le16(ssid->vht_rx_mcs_nss_ ##i <<	\
				     2 * (i - 1));			\
	}

	OVERRIDE_MCS(1);
	OVERRIDE_MCS(2);
	OVERRIDE_MCS(3);
	OVERRIDE_MCS(4);
	OVERRIDE_MCS(5);
	OVERRIDE_MCS(6);
	OVERRIDE_MCS(7);
	OVERRIDE_MCS(8);
}
#endif /* CONFIG_VHT_OVERRIDES */


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int pcsc_reader_init(struct wpa_supplicant *wpa_s)
{
#ifdef PCSC_FUNCS
	size_t len;

	if (!wpa_s->conf->pcsc_reader)
		return 0;

	wpa_s->scard = scard_init(wpa_s->conf->pcsc_reader);
	if (!wpa_s->scard)
		return 1;

	if (wpa_s->conf->pcsc_pin &&
	    scard_set_pin(wpa_s->scard, wpa_s->conf->pcsc_pin) < 0) {
		scard_deinit(wpa_s->scard);
		wpa_s->scard = NULL;
		wpa_msg(wpa_s, MSG_ERROR, "PC/SC PIN validation failed");
		return -1;
	}

	len = sizeof(wpa_s->imsi) - 1;
	if (scard_get_imsi(wpa_s->scard, wpa_s->imsi, &len)) {
		scard_deinit(wpa_s->scard);
		wpa_s->scard = NULL;
		wpa_msg(wpa_s, MSG_ERROR, "Could not read IMSI");
		return -1;
	}
	wpa_s->imsi[len] = '\0';

	wpa_s->mnc_len = scard_get_mnc_len(wpa_s->scard);

	wpa_printf(MSG_DEBUG, "SCARD: IMSI %s (MNC length %d)",
		   wpa_s->imsi, wpa_s->mnc_len);

	wpa_sm_set_scard_ctx(wpa_s->wpa, wpa_s->scard);
	eapol_sm_register_scard_ctx(wpa_s->eapol, wpa_s->scard);
#endif /* PCSC_FUNCS */

	return 0;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


int wpas_init_ext_pw(struct wpa_supplicant *wpa_s)
{
#if 0
	char *val, *pos;

	ext_password_deinit(wpa_s->ext_pw);
	wpa_s->ext_pw = NULL;

	if (!wpa_s->conf->ext_password_backend)
		return 0;

	val = os_strdup(wpa_s->conf->ext_password_backend);
	if (val == NULL)
		return -1;
	pos = os_strchr(val, ':');
	if (pos)
		*pos++ = '\0';

	wpa_printf(MSG_DEBUG, "EXT PW: Initialize backend '%s'", val);

	wpa_s->ext_pw = ext_password_init(val, pos);
	os_free(val);
	if (wpa_s->ext_pw == NULL) {
		wpa_printf(MSG_DEBUG, "EXT PW: Failed to initialize backend");
		return -1;
	}
#endif
	return 0;
}


#ifdef CONFIG_FST

static const u8 * wpas_fst_get_bssid_cb(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;

	return (is_zero_ether_addr(wpa_s->bssid) ||
		wpa_s->wpa_state != WPA_COMPLETED) ? NULL : wpa_s->bssid;
}


static void wpas_fst_get_channel_info_cb(void *ctx,
					 enum hostapd_hw_mode *hw_mode,
					 u8 *channel)
{
	struct wpa_supplicant *wpa_s = ctx;

	if (wpa_s->current_bss) {
		*hw_mode = ieee80211_freq_to_chan(wpa_s->current_bss->freq,
						  channel);
	} else if (wpa_s->hw.num_modes) {
		*hw_mode = wpa_s->hw.modes[0].mode;
	} else {
		WPA_ASSERT(0);
		*hw_mode = 0;
	}
}


static int wpas_fst_get_hw_modes(void *ctx, struct hostapd_hw_modes **modes)
{
	struct wpa_supplicant *wpa_s = ctx;

	*modes = wpa_s->hw.modes;
	return wpa_s->hw.num_modes;
}


static void wpas_fst_set_ies_cb(void *ctx, const struct wpabuf *fst_ies)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_hexdump_buf(MSG_DEBUG, "FST: Set IEs", fst_ies);
	wpa_s->fst_ies = fst_ies;
}


static int wpas_fst_send_action_cb(void *ctx, const u8 *da, struct wpabuf *data)
{
	struct wpa_supplicant *wpa_s = ctx;

	WPA_ASSERT(os_memcmp(wpa_s->bssid, da, ETH_ALEN) == 0);
	return wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
					  wpa_s->own_addr, wpa_s->bssid,
					  wpabuf_head(data), wpabuf_len(data),
				   0);
}


static const struct wpabuf * wpas_fst_get_mb_ie_cb(void *ctx, const u8 *addr)
{
	struct wpa_supplicant *wpa_s = ctx;

	WPA_ASSERT(os_memcmp(wpa_s->bssid, addr, ETH_ALEN) == 0);
	return wpa_s->received_mb_ies;
}


static void wpas_fst_update_mb_ie_cb(void *ctx, const u8 *addr,
				     const u8 *buf, size_t size)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct mb_ies_info info;

	WPA_ASSERT(os_memcmp(wpa_s->bssid, addr, ETH_ALEN) == 0);

	if (!mb_ies_info_by_ies(&info, buf, size)) {
		wpabuf_free(wpa_s->received_mb_ies);
		wpa_s->received_mb_ies = mb_ies_by_info(&info);
	}
}


static const u8 * wpas_fst_get_peer_first(void *ctx,
					  struct fst_get_peer_ctx **get_ctx,
					  Boolean mb_only)
{
	struct wpa_supplicant *wpa_s = ctx;

	*get_ctx = NULL;
	if (!is_zero_ether_addr(wpa_s->bssid))
		return (wpa_s->received_mb_ies || !mb_only) ?
			wpa_s->bssid : NULL;
	return NULL;
}


static const u8 * wpas_fst_get_peer_next(void *ctx,
					 struct fst_get_peer_ctx **get_ctx,
					 Boolean mb_only)
{
	return NULL;
}

void fst_wpa_supplicant_fill_iface_obj(struct wpa_supplicant *wpa_s,
				       struct fst_wpa_obj *iface_obj)
{
	iface_obj->ctx              = wpa_s;
	iface_obj->get_bssid        = wpas_fst_get_bssid_cb;
	iface_obj->get_channel_info = wpas_fst_get_channel_info_cb;
	iface_obj->get_hw_modes     = wpas_fst_get_hw_modes;
	iface_obj->set_ies          = wpas_fst_set_ies_cb;
	iface_obj->send_action      = wpas_fst_send_action_cb;
	iface_obj->get_mb_ie        = wpas_fst_get_mb_ie_cb;
	iface_obj->update_mb_ie     = wpas_fst_update_mb_ie_cb;
	iface_obj->get_peer_first   = wpas_fst_get_peer_first;
	iface_obj->get_peer_next    = wpas_fst_get_peer_next;
}
#endif /* CONFIG_FST */


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int wpas_set_wowlan_triggers(struct wpa_supplicant *wpa_s,
				    const struct wpa_driver_capa *capa)
{
	//struct wowlan_triggers *triggers;
	int ret = 0;

#if 0
	if (!wpa_s->conf->wowlan_triggers)
		return 0;

	triggers = wpa_get_wowlan_triggers(wpa_s->conf->wowlan_triggers, capa);
	if (triggers) {
		ret = wpa_drv_wowlan(wpa_s, triggers);
		os_free(triggers);
	}
#endif    
	return ret;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


enum wpa_radio_work_band wpas_freq_to_band(int freq)
{
	if (freq < 3000)
		return BAND_2_4_GHZ;
	if (freq > 50000)
		return BAND_60_GHZ;
	return BAND_5_GHZ;
}

#if 0
unsigned int wpas_get_bands(struct wpa_supplicant *wpa_s, const int *freqs)
{
	int i;
	unsigned int band = 0;

	if (freqs) {
		/* freqs are specified for the radio work */
		for (i = 0; freqs[i]; i++)
			band |= wpas_freq_to_band(freqs[i]);
	} else {
		/*
		 * freqs are not specified, implies all
		 * the supported freqs by HW
		 */
		for (i = 0; i < wpa_s->hw.num_modes; i++) {
			if (wpa_s->hw.modes[i].num_channels != 0) {
				if (wpa_s->hw.modes[i].mode ==
				    HOSTAPD_MODE_IEEE80211B ||
				    wpa_s->hw.modes[i].mode ==
				    HOSTAPD_MODE_IEEE80211G)
					band |= BAND_2_4_GHZ;
				else if (wpa_s->hw.modes[i].mode ==
					 HOSTAPD_MODE_IEEE80211A)
					band |= BAND_5_GHZ;
				else if (wpa_s->hw.modes[i].mode ==
					 HOSTAPD_MODE_IEEE80211AD)
					band |= BAND_60_GHZ;
				else if (wpa_s->hw.modes[i].mode ==
					 HOSTAPD_MODE_IEEE80211ANY)
					band = BAND_2_4_GHZ | BAND_5_GHZ |
						BAND_60_GHZ;
			}
		}
	}

	return band;
}
#endif

static struct wpa_radio * radio_add_interface(struct wpa_supplicant *wpa_s,
					      const char *rn)
{
	struct wpa_supplicant *iface = wpa_s->global->ifaces;
	struct wpa_radio *radio;

	while (rn && iface) {
		radio = iface->radio;
		if (radio && os_strcmp(rn, radio->name) == 0) {
			wpa_printf(MSG_DEBUG, "Add interface %s to existing radio %s",
				   wpa_s->ifname, rn);
			dl_list_add(&radio->ifaces, &wpa_s->radio_list);
			return radio;
		}

		iface = iface->next;
	}

	wpa_printf(MSG_DEBUG, "Add interface %s to a new radio %s",
		   wpa_s->ifname, rn ? rn : "N/A");
	radio = os_zalloc(sizeof(*radio));
	if (radio == NULL)
		return NULL;

	if (rn)
		os_strlcpy(radio->name, rn, sizeof(radio->name));
	dl_list_init(&radio->ifaces);
	dl_list_init(&radio->work);
	dl_list_add(&radio->ifaces, &wpa_s->radio_list);

	return radio;
}


static void radio_work_free(struct wpa_radio_work *work)
{
	if (work->wpa_s->scan_work == work) {
		/* This should not really happen. */
		wpa_dbg(work->wpa_s, MSG_INFO, "Freeing radio work '%s'@%p (started=%d) that is marked as scan_work",
			work->type, work, work->started);
		work->wpa_s->scan_work = NULL;
	}

#ifdef CONFIG_P2P
	if (work->wpa_s->p2p_scan_work == work) {
		/* This should not really happen. */
		wpa_dbg(work->wpa_s, MSG_INFO, "Freeing radio work '%s'@%p (started=%d) that is marked as p2p_scan_work",
			work->type, work, work->started);
		work->wpa_s->p2p_scan_work = NULL;
	}
#endif /* CONFIG_P2P */

	if (work->started) {
		work->wpa_s->radio->num_active_works--;
		wpa_dbg(work->wpa_s, MSG_DEBUG,
			"radio_work_free('%s'@%p: num_active_works --> %u",
			work->type, work,
			work->wpa_s->radio->num_active_works);
	}

	dl_list_del(&work->list);
	os_free(work);
}


static struct wpa_radio_work * radio_work_get_next_work(struct wpa_radio *radio)
{
	struct wpa_radio_work *active_work = NULL;
	struct wpa_radio_work *tmp;

	/* Get the active work to know the type and band. */
	dl_list_for_each(tmp, &radio->work, struct wpa_radio_work, list) {
		if (tmp->started) {
			active_work = tmp;
			break;
		}
	}

	if (!active_work) {
		/* No active work, start one */
		radio->num_active_works = 0;
		dl_list_for_each(tmp, &radio->work, struct wpa_radio_work,
				 list) {
			if (os_strcmp(tmp->type, "scan") == 0 &&
			    radio->external_scan_running &&
			    (tmp->wpa_s->clear_driver_scan_cache))
				continue;
			return tmp;
		}
		return NULL;
	}

	if (os_strcmp(active_work->type, "sme-connect") == 0 ||
	    os_strcmp(active_work->type, "connect") == 0) {
		/*
		 * If the active work is either connect or sme-connect,
		 * do not parallelize them with other radio works.
		 */
		wpa_dbg(active_work->wpa_s, MSG_DEBUG,
			"Do not parallelize radio work with %s",
			active_work->type);
		return NULL;
	}

	dl_list_for_each(tmp, &radio->work, struct wpa_radio_work, list) {
		if (tmp->started)
			continue;

		/*
		 * If connect or sme-connect are enqueued, parallelize only
		 * those operations ahead of them in the queue.
		 */
		if (os_strcmp(tmp->type, "connect") == 0 ||
		    os_strcmp(tmp->type, "sme-connect") == 0)
			break;

		/*
		 * Check that the radio works are distinct and
		 * on different bands.
		 */
		if (os_strcmp(active_work->type, tmp->type) != 0 &&
		    (active_work->bands != tmp->bands)) {
			/*
			 * If a scan has to be scheduled through nl80211 scan
			 * interface and if an external scan is already running,
			 * do not schedule the scan since it is likely to get
			 * rejected by kernel.
			 */
			if (os_strcmp(tmp->type, "scan") == 0 &&
			    radio->external_scan_running &&
			    (tmp->wpa_s->clear_driver_scan_cache))
				continue;

			wpa_dbg(active_work->wpa_s, MSG_DEBUG,
				"active_work:%s new_work:%s",
				active_work->type, tmp->type);
			return tmp;
		}
	}

	/* Did not find a radio work to schedule in parallel. */
	return NULL;
}


static void radio_start_next_work(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_radio *radio = eloop_ctx;
	struct wpa_radio_work *work;
	struct os_reltime now, diff;
	struct wpa_supplicant *wpa_s;

	work = dl_list_first(&radio->work, struct wpa_radio_work, list);
	if (work == NULL) {
		radio->num_active_works = 0;
		return;
	}

	wpa_s = dl_list_first(&radio->ifaces, struct wpa_supplicant,
			      radio_list);

	if (!(wpa_s &&
	      wpa_s->drv_flags & WPA_DRIVER_FLAGS_OFFCHANNEL_SIMULTANEOUS)) {
		if (work->started)
			return; /* already started and still in progress */

		if (wpa_s && wpa_s->radio->external_scan_running) {
			wpa_printf(MSG_DEBUG, "Delay radio work start until externally triggered scan completes");
			return;
		}
	} else {
		work = NULL;
		if (radio->num_active_works < MAX_ACTIVE_WORKS) {
			/* get the work to schedule next */
			work = radio_work_get_next_work(radio);
		}
		if (!work)
			return;
	}

	wpa_s = work->wpa_s;
	os_get_reltime(&now);
	os_reltime_sub(&now, &work->time, &diff);
	wpa_dbg(wpa_s, MSG_DEBUG,
		"Starting radio work '%s'@%p after %ld.%06ld second wait",
		work->type, work, diff.sec, diff.usec);
	work->started = 1;
	work->time = now;
	radio->num_active_works++;

	work->cb(work, 0);

	if ((wpa_s->drv_flags & WPA_DRIVER_FLAGS_OFFCHANNEL_SIMULTANEOUS) &&
	    radio->num_active_works < MAX_ACTIVE_WORKS)
		radio_work_check_next(wpa_s);
}


/*
 * This function removes both started and pending radio works running on
 * the provided interface's radio.
 * Prior to the removal of the radio work, its callback (cb) is called with
 * deinit set to be 1. Each work's callback is responsible for clearing its
 * internal data and restoring to a correct state.
 * @wpa_s: wpa_supplicant data
 * @type: type of works to be removed
 * @remove_all: 1 to remove all the works on this radio, 0 to remove only
 * this interface's works.
 */
void radio_remove_works(struct wpa_supplicant *wpa_s,
			const char *type, int remove_all)
{
	struct wpa_radio_work *work, *tmp;
	struct wpa_radio *radio = wpa_s->radio;

	dl_list_for_each_safe(work, tmp, &radio->work, struct wpa_radio_work,
			      list) {
		if (type && os_strcmp(type, work->type) != 0)
			continue;

		/* skip other ifaces' works */
		if (!remove_all && work->wpa_s != wpa_s)
			continue;

		wpa_dbg(wpa_s, MSG_DEBUG, "Remove radio work '%s'@%p%s",
			work->type, work, work->started ? " (started)" : "");
		work->cb(work, 1);
		radio_work_free(work);
	}

	/* in case we removed the started work */
	radio_work_check_next(wpa_s);
}


static void radio_remove_interface(struct wpa_supplicant *wpa_s)
{
	struct wpa_radio *radio = wpa_s->radio;

	if (!radio)
		return;

	wpa_printf(MSG_DEBUG, "Remove interface %s from radio %s",
		   wpa_s->ifname, radio->name);
	dl_list_del(&wpa_s->radio_list);
	radio_remove_works(wpa_s, NULL, 0);
	wpa_s->radio = NULL;
	if (!dl_list_empty(&radio->ifaces))
		return; /* Interfaces remain for this radio */

	wpa_printf(MSG_DEBUG, "Remove radio %s", radio->name);
	eloop_cancel_timeout(radio_start_next_work, radio, NULL);
	os_free(radio);
}


void radio_work_check_next(struct wpa_supplicant *wpa_s)
{
	struct wpa_radio *radio = wpa_s->radio;

	if (dl_list_empty(&radio->work))
		return;
	if (wpa_s->ext_work_in_progress) {
		wpa_printf(MSG_DEBUG,
			   "External radio work in progress - delay start of pending item");
		return;
	}
	eloop_cancel_timeout(radio_start_next_work, radio, NULL);
	eloop_register_timeout(0, 0, radio_start_next_work, radio, NULL);
}


/**
 * radio_add_work - Add a radio work item
 * @wpa_s: Pointer to wpa_supplicant data
 * @freq: Frequency of the offchannel operation in MHz or 0
 * @type: Unique identifier for each type of work
 * @next: Force as the next work to be executed
 * @cb: Callback function for indicating when radio is available
 * @ctx: Context pointer for the work (work->ctx in cb())
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to request time for an operation that requires
 * exclusive radio control. Once the radio is available, the registered callback
 * function will be called. radio_work_done() must be called once the exclusive
 * radio operation has been completed, so that the radio is freed for other
 * operations. The special case of deinit=1 is used to free the context data
 * during interface removal. That does not allow the callback function to start
 * the radio operation, i.e., it must free any resources allocated for the radio
 * work and return.
 *
 * The @freq parameter can be used to indicate a single channel on which the
 * offchannel operation will occur. This may allow multiple radio work
 * operations to be performed in parallel if they apply for the same channel.
 * Setting this to 0 indicates that the work item may use multiple channels or
 * requires exclusive control of the radio.
 */
int radio_add_work(struct wpa_supplicant *wpa_s, unsigned int freq,
		   const char *type, int next,
		   void (*cb)(struct wpa_radio_work *work, int deinit),
		   void *ctx)
{
	struct wpa_radio *radio = wpa_s->radio;
	struct wpa_radio_work *work;
	int was_empty;

	work = os_zalloc(sizeof(*work));
	if (work == NULL)
		return -1;
	wpa_dbg(wpa_s, MSG_DEBUG, "Add radio work '%s'@%p", type, work);
	os_get_reltime(&work->time);
	work->freq = freq;
	work->type = type;
	work->wpa_s = wpa_s;
	work->cb = cb;
	work->ctx = ctx;

#if 0
	if (freq)
		work->bands = wpas_freq_to_band(freq);
	else if (os_strcmp(type, "scan") == 0 ||
		 os_strcmp(type, "p2p-scan") == 0)
		work->bands = wpas_get_bands(wpa_s,
					     ((struct wpa_driver_scan_params *)
					      ctx)->freqs);
	else
		work->bands = wpas_get_bands(wpa_s, NULL);
#else
        work->bands = 0;
#endif
	was_empty = dl_list_empty(&wpa_s->radio->work);
	if (next)
		dl_list_add(&wpa_s->radio->work, &work->list);
	else
		dl_list_add_tail(&wpa_s->radio->work, &work->list);
	if (was_empty) {
		wpa_dbg(wpa_s, MSG_DEBUG, "First radio work item in the queue - schedule start immediately");
		radio_work_check_next(wpa_s);
	} else if ((wpa_s->drv_flags & WPA_DRIVER_FLAGS_OFFCHANNEL_SIMULTANEOUS)
		   && radio->num_active_works < MAX_ACTIVE_WORKS) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"Try to schedule a radio work (num_active_works=%u)",
			radio->num_active_works);
		radio_work_check_next(wpa_s);
	}

	return 0;
}


/**
 * radio_work_done - Indicate that a radio work item has been completed
 * @work: Completed work
 *
 * This function is called once the callback function registered with
 * radio_add_work() has completed its work.
 */
void radio_work_done(struct wpa_radio_work *work)
{
	struct wpa_supplicant *wpa_s = work->wpa_s;
	struct os_reltime now, diff;
	unsigned int started = work->started;

	os_get_reltime(&now);
	os_reltime_sub(&now, &work->time, &diff);
	wpa_dbg(wpa_s, MSG_DEBUG, "Radio work '%s'@%p %s in %ld.%06ld seconds",
		work->type, work, started ? "done" : "canceled",
		diff.sec, diff.usec);
	radio_work_free(work);
	if (started)
		radio_work_check_next(wpa_s);
}


struct wpa_radio_work *
radio_work_pending(struct wpa_supplicant *wpa_s, const char *type)
{
	struct wpa_radio_work *work;
	struct wpa_radio *radio = wpa_s->radio;

	dl_list_for_each(work, &radio->work, struct wpa_radio_work, list) {
		if (work->wpa_s == wpa_s && os_strcmp(work->type, type) == 0)
			return work;
	}

	return NULL;
}


static int wpas_init_driver(struct wpa_supplicant *wpa_s,
			    struct wpa_interface *iface)
{
	//const char *ifname, *rn;
	const char *rn;

#if 0
	if (wpa_supplicant_set_driver(wpa_s, NULL) < 0)
		return -1;
#else
    if (select_driver(wpa_s, 0) < 0)
        return -1;
#endif
	wpa_s->drv_priv = wpa_drv_init(wpa_s, wpa_s->ifname);
	if (wpa_s->drv_priv == NULL) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to initialize driver "
			"interface");
		return -1;
	}
    #if 0
	if (wpa_drv_set_param(wpa_s, wpa_s->conf->driver_param) < 0) {
		wpa_msg(wpa_s, MSG_ERROR, "Driver interface rejected "
			"driver_param '%s'", wpa_s->conf->driver_param);
		return -1;
	}
    #endif

#if 0
	ifname = wpa_drv_get_ifname(wpa_s);
	if (ifname && os_strcmp(ifname, wpa_s->ifname) != 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Driver interface replaced "
			"interface name with '%s'", ifname);
		os_strlcpy(wpa_s->ifname, ifname, sizeof(wpa_s->ifname));
	}

	rn = wpa_driver_get_radio_name(wpa_s);
	if (rn && rn[0] == '\0')
		rn = NULL;
#else
    rn = "nrc";
#endif

	wpa_s->radio = radio_add_interface(wpa_s, rn);
	if (wpa_s->radio == NULL)
		return -1;

	return 0;
}


static int wpa_supplicant_init_iface(struct wpa_supplicant *wpa_s,
				     struct wpa_interface *iface)
{
	struct wpa_driver_capa capa;
	int capa_res;

	wpa_printf(MSG_DEBUG, "Initializing interface '%s' conf '%s' driver "
		   "'%s' ctrl_interface '%s' bridge '%s'", iface->ifname,
		   "N/A",
		   "default",
		   "N/A",
		   "N/A");

	wpa_s->conf = wpa_config_alloc_empty(NULL);

#if 0
	if (iface->ifname == NULL) {
		wpa_printf_error(MSG_ERROR, "\nInterface name is required.");
		return -1;
	}
	if (os_strlen(iface->ifname) >= sizeof(wpa_s->ifname)) {
		wpa_printf_error(MSG_ERROR, "\nToo long interface name '%s'.",
			   iface->ifname);
		return -1;
	}
#endif    
	os_strlcpy(wpa_s->ifname, iface->ifname, sizeof(wpa_s->ifname));

	/* Initialize driver interface and register driver event handler before
	 * L2 receive handler so that association events are processed before
	 * EAPOL-Key packets if both become available for the same select()
	 * call. */
	if (wpas_init_driver(wpa_s, iface) < 0)
		return -1;

	if (wpa_supplicant_init_wpa(wpa_s) < 0)
		return -1;

	wpa_sm_set_ifname(wpa_s->wpa, wpa_s->ifname, NULL);
	//wpa_sm_set_fast_reauth(wpa_s->wpa, wpa_s->conf->fast_reauth);

#if 0
	if (wpa_s->conf->dot11RSNAConfigPMKLifetime &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_PMK_LIFETIME,
			     wpa_s->conf->dot11RSNAConfigPMKLifetime)) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid WPA parameter value for "
			"dot11RSNAConfigPMKLifetime");
		return -1;
	}

	if (wpa_s->conf->dot11RSNAConfigPMKReauthThreshold &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_PMK_REAUTH_THRESHOLD,
			     wpa_s->conf->dot11RSNAConfigPMKReauthThreshold)) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid WPA parameter value for "
			"dot11RSNAConfigPMKReauthThreshold");
		return -1;
	}

	if (wpa_s->conf->dot11RSNAConfigSATimeout &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_SA_TIMEOUT,
			     wpa_s->conf->dot11RSNAConfigSATimeout)) {
		wpa_msg(wpa_s, MSG_ERROR, "Invalid WPA parameter value for "
			"dot11RSNAConfigSATimeout");
		return -1;
	}
#endif    

	wpa_s->hw.modes = wpa_drv_get_hw_feature_data(wpa_s,
						      &wpa_s->hw.num_modes,
						      &wpa_s->hw.flags);
#if 0
	if (wpa_s->hw.modes) {
		u16 i;

		for (i = 0; i < wpa_s->hw.num_modes; i++) {
			if (wpa_s->hw.modes[i].vht_capab) {
				wpa_s->hw_capab = CAPAB_VHT;
				break;
			}

			if (wpa_s->hw.modes[i].ht_capab &
			    HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET)
				wpa_s->hw_capab = CAPAB_HT40;
			else if (wpa_s->hw.modes[i].ht_capab &&
				 wpa_s->hw_capab == CAPAB_NO_HT_VHT)
				wpa_s->hw_capab = CAPAB_HT;
		}
	}
#endif
	capa_res = wpa_drv_get_capa(wpa_s, &capa);
	if (capa_res == 0) {
		wpa_s->drv_capa_known = 1;
		wpa_s->drv_flags = capa.flags;
		wpa_s->drv_enc = capa.enc;
		//wpa_s->drv_smps_modes = capa.smps_modes;
		wpa_s->probe_resp_offloads = capa.probe_resp_offloads;
		wpa_s->max_scan_ssids = capa.max_scan_ssids;
		//wpa_s->max_sched_scan_ssids = capa.max_sched_scan_ssids;
		//wpa_s->max_sched_scan_plans = capa.max_sched_scan_plans;
		//wpa_s->max_sched_scan_plan_interval =
			//capa.max_sched_scan_plan_interval;
		//wpa_s->max_sched_scan_plan_iterations =
			//capa.max_sched_scan_plan_iterations;
		//wpa_s->sched_scan_supported = capa.sched_scan_supported;
		//wpa_s->max_match_sets = capa.max_match_sets;
		//wpa_s->max_remain_on_chan = capa.max_remain_on_chan;
		wpa_s->max_stations = capa.max_stations;
		//wpa_s->extended_capa = capa.extended_capa;
		//wpa_s->extended_capa_mask = capa.extended_capa_mask;
		//wpa_s->extended_capa_len = capa.extended_capa_len;
		//wpa_s->num_multichan_concurrent =
			//capa.num_multichan_concurrent;
		//wpa_s->wmm_ac_supported = capa.wmm_ac_supported;

	}
	//if (wpa_s->max_remain_on_chan == 0)
		//wpa_s->max_remain_on_chan = 1000;

	//if (wpa_s->num_multichan_concurrent == 0)
	//	wpa_s->num_multichan_concurrent = 1;

	if (wpa_supplicant_driver_init(wpa_s) < 0)
		return -1;

#ifdef CONFIG_TDLS
	if ((!iface->p2p_mgmt ||
	     !(wpa_s->drv_flags &
	       WPA_DRIVER_FLAGS_DEDICATED_P2P_DEVICE)) &&
	    wpa_tdls_init(wpa_s->wpa))
		return -1;
#endif /* CONFIG_TDLS */

#if 0
	if (wpa_s->conf->country[0] && wpa_s->conf->country[1] &&
	    wpa_drv_set_country(wpa_s, wpa_s->conf->country)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Failed to set country");
		return -1;
	}
#endif    

#ifdef CONFIG_FST
	if (wpa_s->conf->fst_group_id) {
		struct fst_iface_cfg cfg;
		struct fst_wpa_obj iface_obj;

		fst_wpa_supplicant_fill_iface_obj(wpa_s, &iface_obj);
		os_strlcpy(cfg.group_id, wpa_s->conf->fst_group_id,
			   sizeof(cfg.group_id));
		cfg.priority = wpa_s->conf->fst_priority;
		cfg.llt = wpa_s->conf->fst_llt;

		wpa_s->fst = fst_attach(wpa_s->ifname, wpa_s->own_addr,
					&iface_obj, &cfg);
		if (!wpa_s->fst) {
			wpa_msg(wpa_s, MSG_ERROR,
				"FST: Cannot attach iface %s to group %s",
				wpa_s->ifname, cfg.group_id);
			return -1;
		}
	}
#endif /* CONFIG_FST */

	//if (wpas_wps_init(wpa_s))
	//	return -1;

	wpa_s->ctrl_iface = wpa_supplicant_ctrl_iface_init(wpa_s);
	if (wpa_s->ctrl_iface == NULL) {
		wpa_printf_error(MSG_ERROR,
			   "Failed to initialize control interface .\n"
			   "You may have another wpa_supplicant process "
			   "already running or the file was\n"
			   "left by an unclean termination of wpa_supplicant "
			   "in which case you will need\n"
			   "to manually remove this file before starting "
			   "wpa_supplicant again.\n");
		return -1;
	}

	if (wpa_bss_init(wpa_s) < 0)
		return -1;

	//wpas_rrm_reset(wpa_s);

#ifdef CONFIG_HS20
	hs20_init(wpa_s);
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
	wpas_mbo_update_non_pref_chan(wpa_s, wpa_s->conf->non_pref_chan);
#endif /* CONFIG_MBO */

	//wpa_supplicant_set_default_scan_ies(wpa_s);

	return 0;
}


static void wpa_supplicant_deinit_iface(struct wpa_supplicant *wpa_s,
					int notify, int terminate)
{
	struct wpa_global *global = wpa_s->global;
	struct wpa_supplicant *iface, *prev;

	iface = global->ifaces;
	while (iface) {
		if (iface == wpa_s || iface->parent != wpa_s) {
			iface = iface->next;
			continue;
		}
		wpa_printf(MSG_DEBUG,
			   "Remove remaining child interface %s from parent %s",
			   iface->ifname, wpa_s->ifname);
		prev = iface;
		iface = iface->next;
		wpa_supplicant_remove_iface(global, prev, terminate);
	}

	wpa_s->disconnected = 1;
	if (wpa_s->drv_priv) {
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);

		//wpa_drv_set_countermeasures(wpa_s, 0);
		wpa_clear_keys(wpa_s, NULL);
	}

	wpa_supplicant_cleanup(wpa_s);

	wpas_ctrl_radio_work_flush(wpa_s);
	radio_remove_interface(wpa_s);

#ifdef CONFIG_FST
	if (wpa_s->fst) {
		fst_detach(wpa_s->fst);
		wpa_s->fst = NULL;
	}
	if (wpa_s->received_mb_ies) {
		wpabuf_free(wpa_s->received_mb_ies);
		wpa_s->received_mb_ies = NULL;
	}
#endif /* CONFIG_FST */

	if (wpa_s->drv_priv)
		wpa_drv_deinit(wpa_s);

	if (terminate)
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_TERMINATING);

	if (wpa_s->ctrl_iface) {
		os_free(wpa_s->ctrl_iface);
		wpa_s->ctrl_iface = NULL;
	}

#ifdef CONFIG_MESH
	if (wpa_s->ifmsh) {
		wpa_supplicant_mesh_iface_deinit(wpa_s, wpa_s->ifmsh);
		wpa_s->ifmsh = NULL;
	}
#endif /* CONFIG_MESH */

	if (wpa_s->conf != NULL) {
		wpa_config_free(wpa_s->conf);
		wpa_s->conf = NULL;
	}

	os_free(wpa_s);
}


#ifdef CONFIG_MATCH_IFACE

/**
 * wpa_supplicant_match_iface - Match an interface description to a name
 * @global: Pointer to global data from wpa_supplicant_init()
 * @ifname: Name of the interface to match
 * Returns: Pointer to the created interface description or %NULL on failure
 */
struct wpa_interface * wpa_supplicant_match_iface(struct wpa_global *global,
						  const char *ifname)
{
	int i;
	struct wpa_interface *iface, *miface;

	for (i = 0; i < global->params.match_iface_count; i++) {
		miface = &global->params.match_ifaces[i];
		if (!miface->ifname ||
		    fnmatch(miface->ifname, ifname, 0) == 0) {
			iface = os_zalloc(sizeof(*iface));
			if (!iface)
				return NULL;
			*iface = *miface;
			iface->ifname = ifname;
			return iface;
		}
	}

	return NULL;
}


/**
 * wpa_supplicant_match_existing - Match existing interfaces
 * @global: Pointer to global data from wpa_supplicant_init()
 * Returns: 0 on success, -1 on failure
 */
static int wpa_supplicant_match_existing(struct wpa_global *global)
{
	struct if_nameindex *ifi, *ifp;
	struct wpa_supplicant *wpa_s;
	struct wpa_interface *iface;

	ifp = if_nameindex();
	if (!ifp) {
		wpa_printf_error(MSG_ERROR, "if_nameindex: %s", strerror(errno));
		return -1;
	}

	for (ifi = ifp; ifi->if_name; ifi++) {
		wpa_s = wpa_supplicant_get_iface(global, ifi->if_name);
		if (wpa_s)
			continue;
		iface = wpa_supplicant_match_iface(global, ifi->if_name);
		if (iface) {
			wpa_s = wpa_supplicant_add_iface(global, iface, NULL);
			os_free(iface);
			if (wpa_s)
				wpa_s->matched = 1;
		}
	}

	if_freenameindex(ifp);
	return 0;
}

#endif /* CONFIG_MATCH_IFACE */


/**
 * wpa_supplicant_add_iface - Add a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @iface: Interface configuration options
 * @parent: Parent interface or %NULL to assign new interface as parent
 * Returns: Pointer to the created interface or %NULL on failure
 *
 * This function is used to add new network interfaces for %wpa_supplicant.
 * This can be called before wpa_supplicant_run() to add interfaces before the
 * main event loop has been started. In addition, new interfaces can be added
 * dynamically while %wpa_supplicant is already running. This could happen,
 * e.g., when a hotplug network adapter is inserted.
 */
struct wpa_supplicant * wpa_supplicant_add_iface(struct wpa_global *global,
						 struct wpa_interface *iface,
						 struct wpa_supplicant *parent)
{
	struct wpa_supplicant *wpa_s;
	struct wpa_interface t_iface;
	//struct wpa_ssid *ssid;

	if (global == NULL || iface == NULL)
		return NULL;

	wpa_s = wpa_supplicant_alloc(parent);
	if (wpa_s == NULL)
		return NULL;

	wpa_s->global = global;

	t_iface = *iface;
	if (wpa_supplicant_init_iface(wpa_s, &t_iface)) {
		wpa_printf(MSG_DEBUG, "Failed to add interface %s",
			   iface->ifname);
		//wpa_supplicant_deinit_iface(wpa_s, 0, 0);
		return NULL;
	}

	//for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)
	//	wpas_notify_network_added(wpa_s, ssid);

	wpa_s->next = global->ifaces;
	global->ifaces = wpa_s;

	wpa_dbg(wpa_s, MSG_DEBUG, "Added interface %s", wpa_s->ifname);
	wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);

	return wpa_s;
}


/**
 * wpa_supplicant_remove_iface - Remove a network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @wpa_s: Pointer to the network interface to be removed
 * Returns: 0 if interface was removed, -1 if interface was not found
 *
 * This function can be used to dynamically remove network interfaces from
 * %wpa_supplicant, e.g., when a hotplug network adapter is ejected. In
 * addition, this function is used to remove all remaining interfaces when
 * %wpa_supplicant is terminated.
 */
int wpa_supplicant_remove_iface(struct wpa_global *global,
				struct wpa_supplicant *wpa_s,
				int terminate)
{
	struct wpa_supplicant *prev;
#ifdef CONFIG_MESH
	unsigned int mesh_if_created = wpa_s->mesh_if_created;
	char *ifname = NULL;
#endif /* CONFIG_MESH */

	/* Remove interface from the global list of interfaces */
	prev = global->ifaces;
	if (prev == wpa_s) {
		global->ifaces = wpa_s->next;
	} else {
		while (prev && prev->next != wpa_s)
			prev = prev->next;
		if (prev == NULL)
			return -1;
		prev->next = wpa_s->next;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "Removing interface %s", wpa_s->ifname);

#ifdef CONFIG_MESH
	if (mesh_if_created) {
		ifname = os_strdup(wpa_s->ifname);
		if (ifname == NULL) {
		    #ifndef CONFIG_ECR6600_DBGTRIM
			wpa_dbg(wpa_s, MSG_ERROR,
				"mesh: Failed to malloc ifname");
			#else
			wpa_dbg_error(wpa_s, MSG_ERROR,
				"mesh: Failed to malloc ifname");
			#endif
			return -1;
		}
	}
#endif /* CONFIG_MESH */

	wpa_supplicant_deinit_iface(wpa_s, 1, terminate);

#ifdef CONFIG_MESH
	if (mesh_if_created) {
		wpa_drv_if_remove(global->ifaces, WPA_IF_MESH, ifname);
		os_free(ifname);
	}
#endif /* CONFIG_MESH */

	return 0;
}


/**
 * wpa_supplicant_get_eap_mode - Get the current EAP mode
 * @wpa_s: Pointer to the network interface
 * Returns: Pointer to the eap mode or the string "UNKNOWN" if not found
 */
const char * wpa_supplicant_get_eap_mode(struct wpa_supplicant *wpa_s)
{
	//const char *eapol_method;

        if (wpa_key_mgmt_wpa_ieee8021x(wpa_s->key_mgmt) == 0 &&
            wpa_s->key_mgmt != WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		return "NO-EAP";
	}

	return "UNKNOWN-EAP";
}


/**
 * wpa_supplicant_get_iface - Get a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @ifname: Interface name
 * Returns: Pointer to the interface or %NULL if not found
 */
struct wpa_supplicant * wpa_supplicant_get_iface(struct wpa_global *global,
						 const char *ifname)
{
	struct wpa_supplicant *wpa_s;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (os_strcmp(wpa_s->ifname, ifname) == 0)
			return wpa_s;
	}
	return NULL;
}


#ifndef CONFIG_NO_WPA_MSG
#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static const char * wpa_supplicant_msg_ifname_cb(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s == NULL)
		return NULL;
	return wpa_s->ifname;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */
#endif /* CONFIG_NO_WPA_MSG */


#ifndef WPA_SUPPLICANT_CLEANUP_INTERVAL
#define WPA_SUPPLICANT_CLEANUP_INTERVAL 10
#endif /* WPA_SUPPLICANT_CLEANUP_INTERVAL */

/* Periodic cleanup tasks */
static void wpas_periodic(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_global *global = eloop_ctx;
	struct wpa_supplicant *wpa_s;

	//wpa_printf(MSG_DEBUG, "%s: %d", __func__, WPA_SUPPLICANT_CLEANUP_INTERVAL);

	eloop_register_timeout(WPA_SUPPLICANT_CLEANUP_INTERVAL, 0,
			       wpas_periodic, global, NULL);

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		wpa_bss_flush_by_age(wpa_s, wpa_s->conf->bss_expiration_age);
#ifdef CONFIG_AP
		//ap_periodic(wpa_s);
#endif /* CONFIG_AP */
	}
}


/**
 * wpa_supplicant_init - Initialize %wpa_supplicant
 * @params: Parameters for %wpa_supplicant
 * Returns: Pointer to global %wpa_supplicant data, or %NULL on failure
 *
 * This function is used to initialize %wpa_supplicant. After successful
 * initialization, the returned data pointer can be used to add and remove
 * network interfaces, and eventually, to deinitialize %wpa_supplicant.
 */
struct wpa_global * wpa_supplicant_init(void)
{
	struct wpa_global *global;

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;

	//wpa_printf(MSG_DEBUG, "wpa_supplicant v" VERSION_STR);

	if (eloop_init()) {
		wpa_printf_error(MSG_ERROR, "Failed to initialize event loop");
		//wpa_supplicant_deinit(global);
		return NULL;
	}

	global->ctrl_iface = wpa_supplicant_global_ctrl_iface_init(global);
	if (global->ctrl_iface == NULL) {
		//wpa_supplicant_deinit(global);
		return NULL;
	}

    global->drv_count = 1;
	global->drv_priv = os_calloc(global->drv_count, sizeof(void *));
	if (global->drv_priv == NULL) {
		//wpa_supplicant_deinit(global);
		return NULL;
	}

	eloop_register_timeout(WPA_SUPPLICANT_CLEANUP_INTERVAL, 0,
			       wpas_periodic, global, NULL);

	return global;
}


/**
 * wpa_supplicant_run - Run the %wpa_supplicant main event loop
 * @global: Pointer to global data from wpa_supplicant_init()
 * Returns: 0 after successful event loop run, -1 on failure
 *
 * This function starts the main event loop and continues running as long as
 * there are any remaining events. In most cases, this function is running as
 * long as the %wpa_supplicant process in still in use.
 */
int wpa_supplicant_run(struct wpa_global *global)
{
	//struct wpa_supplicant *wpa_s;

	eloop_run();

	return 0;
}


/**
 * wpa_supplicant_deinit - Deinitialize %wpa_supplicant
 * @global: Pointer to global data from wpa_supplicant_init()
 *
 * This function is called to deinitialize %wpa_supplicant and to free all
 * allocated resources. Remaining network interfaces will also be removed.
 */
void wpa_supplicant_deinit(struct wpa_global *global)
{
	int i;

	if (global == NULL)
		return;

	eloop_cancel_timeout(wpas_periodic, global, NULL);

#ifdef CONFIG_WIFI_DISPLAY
	wifi_display_deinit(global);
#endif /* CONFIG_WIFI_DISPLAY */

	while (global->ifaces)
		wpa_supplicant_remove_iface(global, global->ifaces, 1);

	os_free(global->ctrl_iface);

	//eap_peer_unregister_methods();
#ifdef CONFIG_AP
	//eap_server_unregister_methods();
#endif /* CONFIG_AP */

	for (i = 0; wpa_drivers[i]; i++) {
		if (wpa_drivers[i]->global_deinit) {
		wpa_drivers[i]->global_deinit(global->drv_priv[i]);
		}
	}
	os_free(global->drv_priv);

	eloop_destroy();

#ifdef CONFIG_MATCH_IFACE
	os_free(global->params.match_ifaces);
#endif /* CONFIG_MATCH_IFACE */
#ifdef CONFIG_P2P
	os_free(global->params.conf_p2p_dev);
#endif /* CONFIG_P2P */

	os_free(global);
}


void wpa_supplicant_update_config(struct wpa_supplicant *wpa_s)
{
#if 0
	if ((wpa_s->conf->changed_parameters & CFG_CHANGED_COUNTRY) &&
	    wpa_s->conf->country[0] && wpa_s->conf->country[1]) {
		char country[3];
		country[0] = wpa_s->conf->country[0];
		country[1] = wpa_s->conf->country[1];
		country[2] = '\0';
		if (wpa_drv_set_country(wpa_s, country) < 0) {
			wpa_printf_error(MSG_ERROR, "Failed to set country code "
				   "'%s'", country);
		}
	}

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_EXT_PW_BACKEND)
		wpas_init_ext_pw(wpa_s);

#ifdef CONFIG_WPS
	wpas_wps_update_config(wpa_s);
#endif /* CONFIG_WPS */
	wpa_s->conf->changed_parameters = 0;
#endif
}


void add_freq(int *freqs, int *num_freqs, int freq)
{
	int i;

	for (i = 0; i < *num_freqs; i++) {
		if (freqs[i] == freq)
			return;
	}

	freqs[*num_freqs] = freq;
	(*num_freqs)++;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int * get_bss_freqs_in_ess(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss, *cbss;
	const int max_freqs = 10;
	int *freqs;
	int num_freqs = 0;

	freqs = os_calloc(max_freqs + 1, sizeof(int));
	if (freqs == NULL)
		return NULL;

	cbss = wpa_s->current_bss;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (bss == cbss)
			continue;
		if (bss->ssid_len == cbss->ssid_len &&
		    os_memcmp(bss->ssid, cbss->ssid, bss->ssid_len) == 0) {
			add_freq(freqs, &num_freqs, bss->freq);
			if (num_freqs == max_freqs)
				break;
		}
	}

	if (num_freqs == 0) {
		os_free(freqs);
		freqs = NULL;
	}

	return freqs;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


void wpas_connection_failed(struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	int timeout;

	wpas_connect_work_done(wpa_s);

	/*
	 * Remove possible authentication timeout since the connection failed.
	 */
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);

	/*
	 * There is no point in blacklisting the AP if this event is
	 * generated based on local request to disconnect.
	 */
	if (wpa_s->own_disconnect_req) {
		wpa_s->own_disconnect_req = 0;
		wpa_dbg(wpa_s, MSG_DEBUG,
			"Ignore connection failure due to local request to disconnect");
		return;
	}
	if (wpa_s->disconnected) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Ignore connection failure "
			"indication since interface has been put into "
			"disconnected state");
		return;
	}


	/*if (wpa_s->current_ssid) {
		wpa_printf(MSG_DEBUG, "Continuous association failures - "
			   "consider temporary network disabling");
		wpas_auth_failed(wpa_s, "CONN_FAILED");
	}*/

#ifdef CONFIG_PSM_SUPER_LOWPOWER
    timeout = 5;
#else
    timeout = 100;
#endif

	/*
	 * TODO: if more than one possible AP is available in scan results,
	 * could try the other ones before requesting a new scan.
	 */
	wpa_supplicant_req_scan(wpa_s, timeout / 1000,
				1000 * (timeout % 1000));
}


int wpas_driver_bss_selection(struct wpa_supplicant *wpa_s)
{
	return wpa_s->conf->ap_scan == 2 ||
		(wpa_s->drv_flags & WPA_DRIVER_FLAGS_BSS_SELECTION);
}


#if defined(CONFIG_CTRL_IFACE) || defined(CONFIG_CTRL_IFACE_DBUS_NEW)
int wpa_supplicant_ctrl_iface_ctrl_rsp_handle(struct wpa_supplicant *wpa_s,
					      struct wpa_ssid *ssid,
					      const char *field,
					      const char *value)
{
#ifdef IEEE8021X_EAPOL
	struct eap_peer_config *eap = &ssid->eap;

	wpa_printf(MSG_DEBUG, "CTRL_IFACE: response handle field=%s", field);
	wpa_hexdump_ascii_key(MSG_DEBUG, "CTRL_IFACE: response value",
			      (const u8 *) value, os_strlen(value));

	switch (wpa_supplicant_ctrl_req_from_string(field)) {
	case WPA_CTRL_REQ_EAP_IDENTITY:
		os_free(eap->identity);
		eap->identity = (u8 *) os_strdup(value);
		eap->identity_len = os_strlen(value);
		eap->pending_req_identity = 0;
		if (ssid == wpa_s->current_ssid)
			wpa_s->reassociate = 1;
		break;
	case WPA_CTRL_REQ_EAP_PASSWORD:
		bin_clear_free(eap->password, eap->password_len);
		eap->password = (u8 *) os_strdup(value);
		eap->password_len = os_strlen(value);
		eap->pending_req_password = 0;
		if (ssid == wpa_s->current_ssid)
			wpa_s->reassociate = 1;
		break;
	case WPA_CTRL_REQ_EAP_NEW_PASSWORD:
		bin_clear_free(eap->new_password, eap->new_password_len);
		eap->new_password = (u8 *) os_strdup(value);
		eap->new_password_len = os_strlen(value);
		eap->pending_req_new_password = 0;
		if (ssid == wpa_s->current_ssid)
			wpa_s->reassociate = 1;
		break;
	case WPA_CTRL_REQ_EAP_PIN:
		str_clear_free(eap->pin);
		eap->pin = os_strdup(value);
		eap->pending_req_pin = 0;
		if (ssid == wpa_s->current_ssid)
			wpa_s->reassociate = 1;
		break;
	case WPA_CTRL_REQ_EAP_OTP:
		bin_clear_free(eap->otp, eap->otp_len);
		eap->otp = (u8 *) os_strdup(value);
		eap->otp_len = os_strlen(value);
		os_free(eap->pending_req_otp);
		eap->pending_req_otp = NULL;
		eap->pending_req_otp_len = 0;
		break;
	case WPA_CTRL_REQ_EAP_PASSPHRASE:
		str_clear_free(eap->private_key_passwd);
		eap->private_key_passwd = os_strdup(value);
		eap->pending_req_passphrase = 0;
		if (ssid == wpa_s->current_ssid)
			wpa_s->reassociate = 1;
		break;
	case WPA_CTRL_REQ_SIM:
		str_clear_free(eap->external_sim_resp);
		eap->external_sim_resp = os_strdup(value);
		break;
	case WPA_CTRL_REQ_PSK_PASSPHRASE:
		if (wpa_config_set(ssid, "psk", value, 0) < 0)
			return -1;
		if (ssid->passphrase)
			wpa_config_update_psk(ssid);
		if (wpa_s->wpa_state == WPA_SCANNING && !wpa_s->scanning)
			wpa_supplicant_req_scan(wpa_s, 0, 0);
		break;
	case WPA_CTRL_REQ_EXT_CERT_CHECK:
		if (eap->pending_ext_cert_check != PENDING_CHECK)
			return -1;
		if (os_strcmp(value, "good") == 0)
			eap->pending_ext_cert_check = EXT_CERT_CHECK_GOOD;
		else if (os_strcmp(value, "bad") == 0)
			eap->pending_ext_cert_check = EXT_CERT_CHECK_BAD;
		else
			return -1;
		break;
	default:
		wpa_printf(MSG_DEBUG, "CTRL_IFACE: Unknown field '%s'", field);
		return -1;
	}

	return 0;
#else /* IEEE8021X_EAPOL */
	wpa_printf(MSG_DEBUG, "CTRL_IFACE: IEEE 802.1X not included");
	return -1;
#endif /* IEEE8021X_EAPOL */
}
#endif /* CONFIG_CTRL_IFACE || CONFIG_CTRL_IFACE_DBUS_NEW */


int wpas_network_disabled(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	int i;
	unsigned int drv_enc;

	//if (wpa_s->p2p_mgmt)
	//	return 1; /* no normal network profiles on p2p_mgmt interface */

	if (ssid == NULL)
		return 1;

	if (ssid->disabled)
		return 1;

	if (wpa_s->drv_capa_known)
		drv_enc = wpa_s->drv_enc;
	else
		drv_enc = (unsigned int) -1;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		size_t len = ssid->wep_key_len[i];
		if (len == 0)
			continue;
		if (len == 5 && (drv_enc & WPA_DRIVER_CAPA_ENC_WEP40))
			continue;
		if (len == 13 && (drv_enc & WPA_DRIVER_CAPA_ENC_WEP104))
			continue;
		if (len == 16 && (drv_enc & WPA_DRIVER_CAPA_ENC_WEP128))
			continue;
		return 1; /* invalid WEP key */
	}

	return 0;
}


int wpas_get_ssid_pmf(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
#ifdef CONFIG_IEEE80211W
	if (ssid == NULL || ssid->ieee80211w == MGMT_FRAME_PROTECTION_DEFAULT) {
		if (wpa_s->conf->pmf == MGMT_FRAME_PROTECTION_OPTIONAL &&
		    !(wpa_s->drv_enc & WPA_DRIVER_CAPA_ENC_BIP)) {
			/*
			 * Driver does not support BIP -- ignore pmf=1 default
			 * since the connection with PMF would fail and the
			 * configuration does not require PMF to be enabled.
			 */
			return NO_MGMT_FRAME_PROTECTION;
		}

		if (ssid &&
		    (ssid->key_mgmt &
		     ~(WPA_KEY_MGMT_NONE | WPA_KEY_MGMT_WPS |
		       WPA_KEY_MGMT_IEEE8021X_NO_WPA)) == 0) {
			/*
			 * Do not use the default PMF value for non-RSN networks
			 * since PMF is available only with RSN and pmf=2
			 * configuration would otherwise prevent connections to
			 * all open networks.
			 */
			return NO_MGMT_FRAME_PROTECTION;
		}

		return wpa_s->conf->pmf;
	}

	return ssid->ieee80211w;
#else /* CONFIG_IEEE80211W */
	return NO_MGMT_FRAME_PROTECTION;
#endif /* CONFIG_IEEE80211W */
}

#if 0
void wpas_auth_failed(struct wpa_supplicant *wpa_s, char *reason)
{
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	int dur;
	struct os_reltime now;

	if (ssid == NULL) {
		wpa_printf(MSG_DEBUG, "Authentication failure but no known "
			   "SSID block");
		return;
	}

	if (ssid->key_mgmt == WPA_KEY_MGMT_WPS)
		return;

	ssid->auth_failures++;

#ifdef CONFIG_P2P
	if (ssid->p2p_group &&
	    (wpa_s->p2p_in_provisioning || wpa_s->show_group_started)) {
		/*
		 * Skip the wait time since there is a short timeout on the
		 * connection to a P2P group.
		 */
		return;
	}
#endif /* CONFIG_P2P */

	if (ssid->auth_failures > 50)
		dur = 300;
	else if (ssid->auth_failures > 10)
		dur = 120;
	else if (ssid->auth_failures > 5)
		dur = 90;
	else if (ssid->auth_failures > 3)
		dur = 60;
	else if (ssid->auth_failures > 2)
		dur = 30;
	else if (ssid->auth_failures > 1)
		dur = 20;
	else
		dur = 10;

	if (ssid->auth_failures > 1 &&
	    wpa_key_mgmt_wpa_ieee8021x(ssid->key_mgmt))
		dur += os_random() % (ssid->auth_failures * 10);

	os_get_reltime(&now);
	if (now.sec + dur <= ssid->disabled_until.sec)
		return;

	ssid->disabled_until.sec = now.sec + dur;

	wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_TEMP_DISABLED
		"id=%d ssid=\"%s\" auth_failures=%u duration=%d reason=%s",
		ssid->id, wpa_ssid_txt(ssid->ssid, ssid->ssid_len),
		ssid->auth_failures, dur, reason);
}

void wpas_clear_temp_disabled(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid, int clear_failures)
{
	if (ssid == NULL)
		return;

	if (ssid->disabled_until.sec) {
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_REENABLED
			"id=%d ssid=\"%s\"",
			ssid->id, wpa_ssid_txt(ssid->ssid, ssid->ssid_len));
	}
	ssid->disabled_until.sec = 0;
	ssid->disabled_until.usec = 0;
	if (clear_failures)
		ssid->auth_failures = 0;
}
#endif
/**
 * wpas_request_connection - Request a new connection
 * @wpa_s: Pointer to the network interface
 *
 * This function is used to request a new connection to be found. It will mark
 * the interface to allow reassociation and request a new scan to find a
 * suitable network to connect to.
 */
#if 0 
void wpas_request_connection(struct wpa_supplicant *wpa_s)
{
	wpa_s->normal_scans = 0;
	wpa_s->scan_req = NORMAL_SCAN_REQ;
	wpa_supplicant_reinit_autoscan(wpa_s);
	//wpa_s->extra_blacklist_count = 0;
	wpa_s->disconnected = 0;
	//wpa_s->reassociate = 1;

	if (wpa_supplicant_fast_associate(wpa_s) != 1)
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	else
		wpa_s->reattach = 0;
}
#endif

/**
 * wpas_request_disconnection - Request disconnection
 * @wpa_s: Pointer to the network interface
 *
 * This function is used to request disconnection from the currently connected
 * network. This will stop any ongoing scans and initiate deauthentication.
 */
void wpas_request_disconnection(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_SME
	wpa_s->sme.prev_bssid_set = 0;
#endif /* CONFIG_SME */
	//wpa_s->reassociate = 0;
	wpa_s->disconnected = 1;
	//wpa_supplicant_cancel_sched_scan(wpa_s);
	wpa_supplicant_cancel_scan(wpa_s);
	wpa_supplicant_deauthenticate(wpa_s, WLAN_REASON_DEAUTH_LEAVING);
	eloop_cancel_timeout(wpas_network_reenabled, wpa_s, NULL);
}


void dump_freq_data(struct wpa_supplicant *wpa_s, const char *title,
		    struct wpa_used_freq_data *freqs_data,
		    unsigned int len)
{
	unsigned int i;

	wpa_dbg(wpa_s, MSG_DEBUG, "Shared frequencies (len=%u): %s",
		len, title);
	for (i = 0; i < len; i++) {    
		struct wpa_used_freq_data *cur __attribute__((unused)) = &freqs_data[i];
		wpa_dbg(wpa_s, MSG_DEBUG, "freq[%u]: %d, flags=0x%X",
			i, cur->freq, cur->flags);		
	}
}


/*
 * Find the operating frequencies of any of the virtual interfaces that
 * are using the same radio as the current interface, and in addition, get
 * information about the interface types that are using the frequency.
 */
int get_shared_radio_freqs_data(struct wpa_supplicant *wpa_s,
				struct wpa_used_freq_data *freqs_data,
				unsigned int len)
{
	struct wpa_supplicant *ifs;
	u8 bssid[ETH_ALEN];
	int freq;
	unsigned int idx = 0, i;

	wpa_dbg(wpa_s, MSG_DEBUG,
		"Determining shared radio frequencies (max len %u)", len);
	os_memset(freqs_data, 0, sizeof(struct wpa_used_freq_data) * len);

	dl_list_for_each(ifs, &wpa_s->radio->ifaces, struct wpa_supplicant,
			 radio_list) {
		if (idx == len)
			break;

		if (ifs->current_ssid == NULL || ifs->assoc_freq == 0)
			continue;

		if (ifs->current_ssid->mode == WPAS_MODE_AP ||
		    ifs->current_ssid->mode == WPAS_MODE_P2P_GO ||
		    ifs->current_ssid->mode == WPAS_MODE_MESH)
			freq = ifs->current_ssid->frequency;
		else if (wpa_drv_get_bssid(ifs, bssid) == 0)
			freq = ifs->assoc_freq;
		else
			continue;

		/* Hold only distinct freqs */
		for (i = 0; i < idx; i++)
			if (freqs_data[i].freq == freq)
				break;

		if (i == idx)
			freqs_data[idx++].freq = freq;

		if (ifs->current_ssid->mode == WPAS_MODE_INFRA) {
			freqs_data[i].flags |= WPA_FREQ_USED_BY_INFRA_STATION;
		}
	}

	dump_freq_data(wpa_s, "completed iteration", freqs_data, idx);
	return idx;
}


/*
 * Find the operating frequencies of any of the virtual interfaces that
 * are using the same radio as the current interface.
 */
int get_shared_radio_freqs(struct wpa_supplicant *wpa_s,
			   int *freq_array, unsigned int len)
{
	struct wpa_used_freq_data *freqs_data;
	int num, i;

	os_memset(freq_array, 0, sizeof(int) * len);

	freqs_data = os_calloc(len, sizeof(struct wpa_used_freq_data));
	if (!freqs_data)
		return -1;

	num = get_shared_radio_freqs_data(wpa_s, freqs_data, len);
	for (i = 0; i < num; i++)
		freq_array[i] = freqs_data[i].freq;

	os_free(freqs_data);

	return num;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpas_rrm_neighbor_rep_timeout_handler(void *data, void *user_ctx)
{
#if 0
	struct rrm_data *rrm = data;

	if (!rrm->notify_neighbor_rep) {
		wpa_printf_error(MSG_ERROR,
			   "RRM: Unexpected neighbor report timeout");
		return;
	}

	wpa_printf(MSG_DEBUG, "RRM: Notifying neighbor report - NONE");
	rrm->notify_neighbor_rep(rrm->neighbor_rep_cb_ctx, NULL);

	rrm->notify_neighbor_rep = NULL;
	rrm->neighbor_rep_cb_ctx = NULL;
#endif    
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


/*
 * wpas_rrm_reset - Clear and reset all RRM data in wpa_supplicant
 * @wpa_s: Pointer to wpa_supplicant
 */
void wpas_rrm_reset(struct wpa_supplicant *wpa_s)
{
#if 0
	wpa_s->rrm.rrm_used = 0;

	eloop_cancel_timeout(wpas_rrm_neighbor_rep_timeout_handler, &wpa_s->rrm,
			     NULL);
	if (wpa_s->rrm.notify_neighbor_rep)
		wpas_rrm_neighbor_rep_timeout_handler(&wpa_s->rrm, NULL);
	wpa_s->rrm.next_neighbor_rep_token = 1;
#endif    
}


/*
 * wpas_rrm_process_neighbor_rep - Handle incoming neighbor report
 * @wpa_s: Pointer to wpa_supplicant
 * @report: Neighbor report buffer, prefixed by a 1-byte dialog token
 * @report_len: Length of neighbor report buffer
 */
void wpas_rrm_process_neighbor_rep(struct wpa_supplicant *wpa_s,
				   const u8 *report, size_t report_len)
{
#if 0
	struct wpabuf *neighbor_rep;

	wpa_hexdump(MSG_DEBUG, "RRM: New Neighbor Report", report, report_len);
	if (report_len < 1)
		return;

	if (report[0] != wpa_s->rrm.next_neighbor_rep_token - 1) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Discarding neighbor report with token %d (expected %d)",
			   report[0], wpa_s->rrm.next_neighbor_rep_token - 1);
		return;
	}

	eloop_cancel_timeout(wpas_rrm_neighbor_rep_timeout_handler, &wpa_s->rrm,
			     NULL);

	if (!wpa_s->rrm.notify_neighbor_rep) {
		wpa_printf_error(MSG_ERROR, "RRM: Unexpected neighbor report");
		return;
	}

	/* skipping the first byte, which is only an id (dialog token) */
	neighbor_rep = wpabuf_alloc(report_len - 1);
	if (neighbor_rep == NULL)
		return;
	wpabuf_put_data(neighbor_rep, report + 1, report_len - 1);
	wpa_printf(MSG_DEBUG, "RRM: Notifying neighbor report (token = %d)",
		   report[0]);
	wpa_s->rrm.notify_neighbor_rep(wpa_s->rrm.neighbor_rep_cb_ctx,
				       neighbor_rep);
	wpa_s->rrm.notify_neighbor_rep = NULL;
	wpa_s->rrm.neighbor_rep_cb_ctx = NULL;
#endif    
}


#if defined(__CYGWIN__) || defined(CONFIG_NATIVE_WINDOWS)
/* Workaround different, undefined for Windows, error codes used here */
#define ENOTCONN -1
#define EOPNOTSUPP -1
#define ECANCELED -1
#endif

/* Measurement Request element + Location Subject + Maximum Age subelement */
#define MEASURE_REQUEST_LCI_LEN (3 + 1 + 4)
/* Measurement Request element + Location Civic Request */
#define MEASURE_REQUEST_CIVIC_LEN (3 + 5)


/**
 * wpas_rrm_send_neighbor_rep_request - Request a neighbor report from our AP
 * @wpa_s: Pointer to wpa_supplicant
 * @ssid: if not null, this is sent in the request. Otherwise, no SSID IE
 *	  is sent in the request.
 * @lci: if set, neighbor request will include LCI request
 * @civic: if set, neighbor request will include civic location request
 * @cb: Callback function to be called once the requested report arrives, or
 *	timed out after RRM_NEIGHBOR_REPORT_TIMEOUT seconds.
 *	In the former case, 'neighbor_rep' is a newly allocated wpabuf, and it's
 *	the requester's responsibility to free it.
 *	In the latter case NULL will be sent in 'neighbor_rep'.
 * @cb_ctx: Context value to send the callback function
 * Returns: 0 in case of success, negative error code otherwise
 *
 * In case there is a previous request which has not been answered yet, the
 * new request fails. The caller may retry after RRM_NEIGHBOR_REPORT_TIMEOUT.
 * Request must contain a callback function.
 */
int wpas_rrm_send_neighbor_rep_request(struct wpa_supplicant *wpa_s,
				       const struct wpa_ssid_value *ssid,
				       int lci, int civic,
				       void (*cb)(void *ctx,
						  struct wpabuf *neighbor_rep),
				       void *cb_ctx)
{
#if 0
	struct wpabuf *buf;
	const u8 *rrm_ie;

	if (wpa_s->wpa_state != WPA_COMPLETED || wpa_s->current_ssid == NULL) {
		wpa_printf(MSG_DEBUG, "RRM: No connection, no RRM.");
		return -ENOTCONN;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_DEBUG, "RRM: No RRM in current connection.");
		return -EOPNOTSUPP;
	}

	rrm_ie = wpa_bss_get_ie(wpa_s->current_bss,
				WLAN_EID_RRM_ENABLED_CAPABILITIES);
	if (!rrm_ie || !(wpa_s->current_bss->caps & IEEE80211_CAP_RRM) ||
	    !(rrm_ie[2] & WLAN_RRM_CAPS_NEIGHBOR_REPORT)) {
		wpa_printf(MSG_DEBUG,
			   "RRM: No network support for Neighbor Report.");
		return -EOPNOTSUPP;
	}

	if (!cb) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Neighbor Report request must provide a callback.");
		return -EINVAL;
	}

	/* Refuse if there's a live request */
	if (wpa_s->rrm.notify_neighbor_rep) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Currently handling previous Neighbor Report.");
		return -EBUSY;
	}

	/* 3 = action category + action code + dialog token */
	buf = wpabuf_alloc(3 + (ssid ? 2 + ssid->ssid_len : 0) +
			   (lci ? 2 + MEASURE_REQUEST_LCI_LEN : 0) +
			   (civic ? 2 + MEASURE_REQUEST_CIVIC_LEN : 0));
	if (buf == NULL) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Failed to allocate Neighbor Report Request");
		return -ENOMEM;
	}

	wpa_printf(MSG_DEBUG, "RRM: Neighbor report request (for %s), token=%d",
		   (ssid ? wpa_ssid_txt(ssid->ssid, ssid->ssid_len) : ""),
		   wpa_s->rrm.next_neighbor_rep_token);

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_NEIGHBOR_REPORT_REQUEST);
	wpabuf_put_u8(buf, wpa_s->rrm.next_neighbor_rep_token);
	if (ssid) {
		wpabuf_put_u8(buf, WLAN_EID_SSID);
		wpabuf_put_u8(buf, ssid->ssid_len);
		wpabuf_put_data(buf, ssid->ssid, ssid->ssid_len);
	}

	if (lci) {
		/* IEEE P802.11-REVmc/D5.0 9.4.2.21 */
		wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
		wpabuf_put_u8(buf, MEASURE_REQUEST_LCI_LEN);

		/*
		 * Measurement token; nonzero number that is unique among the
		 * Measurement Request elements in a particular frame.
		 */
		wpabuf_put_u8(buf, 1); /* Measurement Token */

		/*
		 * Parallel, Enable, Request, and Report bits are 0, Duration is
		 * reserved.
		 */
		wpabuf_put_u8(buf, 0); /* Measurement Request Mode */
		wpabuf_put_u8(buf, MEASURE_TYPE_LCI); /* Measurement Type */

		/* IEEE P802.11-REVmc/D5.0 9.4.2.21.10 - LCI request */
		/* Location Subject */
		wpabuf_put_u8(buf, LOCATION_SUBJECT_REMOTE);

		/* Optional Subelements */
		/*
		 * IEEE P802.11-REVmc/D5.0 Figure 9-170
		 * The Maximum Age subelement is required, otherwise the AP can
		 * send only data that was determined after receiving the
		 * request. Setting it here to unlimited age.
		 */
		wpabuf_put_u8(buf, LCI_REQ_SUBELEM_MAX_AGE);
		wpabuf_put_u8(buf, 2);
		wpabuf_put_le16(buf, 0xffff);
	}

	if (civic) {
		/* IEEE P802.11-REVmc/D5.0 9.4.2.21 */
		wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
		wpabuf_put_u8(buf, MEASURE_REQUEST_CIVIC_LEN);

		/*
		 * Measurement token; nonzero number that is unique among the
		 * Measurement Request elements in a particular frame.
		 */
		wpabuf_put_u8(buf, 2); /* Measurement Token */

		/*
		 * Parallel, Enable, Request, and Report bits are 0, Duration is
		 * reserved.
		 */
		wpabuf_put_u8(buf, 0); /* Measurement Request Mode */
		/* Measurement Type */
		wpabuf_put_u8(buf, MEASURE_TYPE_LOCATION_CIVIC);

		/* IEEE P802.11-REVmc/D5.0 9.4.2.21.14:
		 * Location Civic request */
		/* Location Subject */
		wpabuf_put_u8(buf, LOCATION_SUBJECT_REMOTE);
		wpabuf_put_u8(buf, 0); /* Civic Location Type: IETF RFC 4776 */
		/* Location Service Interval Units: Seconds */
		wpabuf_put_u8(buf, 0);
		/* Location Service Interval: 0 - Only one report is requested
		 */
		wpabuf_put_le16(buf, 0);
		/* No optional subelements */
	}

	wpa_s->rrm.next_neighbor_rep_token++;

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0) < 0) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Failed to send Neighbor Report Request");
		wpabuf_free(buf);
		return -ECANCELED;
	}

	wpa_s->rrm.neighbor_rep_cb_ctx = cb_ctx;
	wpa_s->rrm.notify_neighbor_rep = cb;
	eloop_register_timeout(RRM_NEIGHBOR_REPORT_TIMEOUT, 0,
			       wpas_rrm_neighbor_rep_timeout_handler,
			       &wpa_s->rrm, NULL);

	wpabuf_free(buf);
#endif    
	return 0;
}


void wpas_rrm_handle_radio_measurement_request(struct wpa_supplicant *wpa_s,
					       const u8 *src,
					       const u8 *frame, size_t len)
{
#if 0
	struct wpabuf *buf, *report;
	u8 token;
	const u8 *ie, *end;

	if (wpa_s->wpa_state != WPA_COMPLETED) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring radio measurement request: Not associated");
		return;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring radio measurement request: Not RRM network");
		return;
	}

	if (len < 3) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring too short radio measurement request");
		return;
	}

	end = frame + len;

	token = *frame++;

	/* Ignore number of repetitions because it's not used in LCI request */
	frame += 2;

	report = NULL;
	while ((ie = get_ie(frame, end - frame, WLAN_EID_MEASURE_REQUEST)) &&
	       ie[1] >= 3) {
		u8 msmt_type;

		msmt_type = ie[4];
		wpa_printf(MSG_DEBUG, "RRM request %d", msmt_type);

		switch (msmt_type) {
		case MEASURE_TYPE_LCI:
			report = wpas_rrm_build_lci_report(wpa_s, ie + 2, ie[1],
							   report);
			break;
		default:
			wpa_printf(MSG_INFO,
				   "RRM: Unsupported radio measurement request %d",
				   msmt_type);
			break;
		}

		frame = ie + ie[1] + 2;
	}

	if (!report)
		return;

	buf = wpabuf_alloc(3 + wpabuf_len(report));
	if (!buf) {
		wpabuf_free(report);
		return;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_RADIO_MEASUREMENT_REPORT);
	wpabuf_put_u8(buf, token);

	wpabuf_put_buf(buf, report);
	wpabuf_free(report);

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, src,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0)) {
		wpa_printf_error(MSG_ERROR,
			   "RRM: Radio measurement report failed: Sending Action frame failed");
	}
	wpabuf_free(buf);
#endif    
}


void wpas_rrm_handle_link_measurement_request(struct wpa_supplicant *wpa_s,
					      const u8 *src,
					      const u8 *frame, size_t len,
					      int rssi)
{
#if 0
	struct wpabuf *buf;
	const struct rrm_link_measurement_request *req;
	struct rrm_link_measurement_report report;

	if (wpa_s->wpa_state != WPA_COMPLETED) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring link measurement request. Not associated");
		return;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring link measurement request. Not RRM network");
		return;
	}

	if (!(wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_TX_POWER_INSERTION)) {
		wpa_printf(MSG_INFO,
			   "RRM: Measurement report failed. TX power insertion not supported");
		return;
	}

	req = (const struct rrm_link_measurement_request *) frame;
	if (len < sizeof(*req)) {
		wpa_printf(MSG_INFO,
			   "RRM: Link measurement report failed. Request too short");
		return;
	}

	os_memset(&report, 0, sizeof(report));
	report.tpc.eid = WLAN_EID_TPC_REPORT;
	report.tpc.len = 2;
	report.rsni = 255; /* 255 indicates that RSNI is not available */
	report.dialog_token = req->dialog_token;

	/*
	 * It's possible to estimate RCPI based on RSSI in dBm. This
	 * calculation will not reflect the correct value for high rates,
	 * but it's good enough for Action frames which are transmitted
	 * with up to 24 Mbps rates.
	 */
	if (!rssi)
		report.rcpi = 255; /* not available */
	else if (rssi < -110)
		report.rcpi = 0;
	else if (rssi > 0)
		report.rcpi = 220;
	else
		report.rcpi = (rssi + 110) * 2;

	/* action_category + action_code */
	buf = wpabuf_alloc(2 + sizeof(report));
	if (buf == NULL) {
		wpa_printf_error(MSG_ERROR,
			   "RRM: Link measurement report failed. Buffer allocation failed");
		return;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_LINK_MEASUREMENT_REPORT);
	wpabuf_put_data(buf, &report, sizeof(report));
	wpa_hexdump(MSG_DEBUG, "RRM: Link measurement report:",
		    wpabuf_head(buf), wpabuf_len(buf));

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, src,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0)) {
		wpa_printf_error(MSG_ERROR,
			   "RRM: Link measurement report failed. Send action failed");
	}
	wpabuf_free(buf);
#endif    
}


struct wpa_supplicant *
wpas_vendor_elem(struct wpa_supplicant *wpa_s, enum wpa_vendor_elem_frame frame)
{
	switch (frame) {
#ifdef CONFIG_P2P
	case VENDOR_ELEM_PROBE_REQ_P2P:
	case VENDOR_ELEM_PROBE_RESP_P2P:
	case VENDOR_ELEM_PROBE_RESP_P2P_GO:
	case VENDOR_ELEM_BEACON_P2P_GO:
	case VENDOR_ELEM_P2P_PD_REQ:
	case VENDOR_ELEM_P2P_PD_RESP:
	case VENDOR_ELEM_P2P_GO_NEG_REQ:
	case VENDOR_ELEM_P2P_GO_NEG_RESP:
	case VENDOR_ELEM_P2P_GO_NEG_CONF:
	case VENDOR_ELEM_P2P_INV_REQ:
	case VENDOR_ELEM_P2P_INV_RESP:
	case VENDOR_ELEM_P2P_ASSOC_REQ:
	case VENDOR_ELEM_P2P_ASSOC_RESP:
		return wpa_s->p2pdev;
#endif /* CONFIG_P2P */
	default:
		return wpa_s;
	}
}

#if 0
void wpas_vendor_elem_update(struct wpa_supplicant *wpa_s)
{
	unsigned int i;
	char buf[30];

	wpa_printf(MSG_DEBUG, "Update vendor elements");

	for (i = 0; i < NUM_VENDOR_ELEM_FRAMES; i++) {
		if (wpa_s->vendor_elem[i]) {
			int res;

			res = os_snprintf(buf, sizeof(buf), "frame[%u]", i);
			if (!os_snprintf_error(sizeof(buf), res)) {
				wpa_hexdump_buf(MSG_DEBUG, buf,
						wpa_s->vendor_elem[i]);
			}
		}
	}

#ifdef CONFIG_P2P
	if (wpa_s->parent == wpa_s &&
	    wpa_s->global->p2p &&
	    !wpa_s->global->p2p_disabled)
		p2p_set_vendor_elems(wpa_s->global->p2p, wpa_s->vendor_elem);
#endif /* CONFIG_P2P */
}


int wpas_vendor_elem_remove(struct wpa_supplicant *wpa_s, int frame,
			    const u8 *elem, size_t len)
{
	u8 *ie, *end;

	ie = wpabuf_mhead_u8(wpa_s->vendor_elem[frame]);
	end = ie + wpabuf_len(wpa_s->vendor_elem[frame]);

	for (; ie + 1 < end; ie += 2 + ie[1]) {
		if (ie + len > end)
			break;
		if (os_memcmp(ie, elem, len) != 0)
			continue;

		if (wpabuf_len(wpa_s->vendor_elem[frame]) == len) {
			wpabuf_free(wpa_s->vendor_elem[frame]);
			wpa_s->vendor_elem[frame] = NULL;
		} else {
			os_memmove(ie, ie + len, end - (ie + len));
			wpa_s->vendor_elem[frame]->used -= len;
		}
		wpas_vendor_elem_update(wpa_s);
		return 0;
	}

	return -1;
}
#endif

struct hostapd_hw_modes * get_mode(struct hostapd_hw_modes *modes,
				   u16 num_modes, enum hostapd_hw_mode mode)
{
	u16 i;

	for (i = 0; i < num_modes; i++) {
		if (modes[i].mode == mode)
			return &modes[i];
	}

	return NULL;
}


#if 0
static struct
wpa_bss_tmp_disallowed * wpas_get_disallowed_bss(struct wpa_supplicant *wpa_s,
						 const u8 *bssid)
{
	struct wpa_bss_tmp_disallowed *bss;

	dl_list_for_each(bss, &wpa_s->bss_tmp_disallowed,
			 struct wpa_bss_tmp_disallowed, list) {
		if (os_memcmp(bssid, bss->bssid, ETH_ALEN) == 0)
			return bss;
	}

	return NULL;
}


void wpa_bss_tmp_disallow(struct wpa_supplicant *wpa_s, const u8 *bssid,
			  unsigned int sec)
{
	struct wpa_bss_tmp_disallowed *bss;
	struct os_reltime until;

	os_get_reltime(&until);
	until.sec += sec;

	bss = wpas_get_disallowed_bss(wpa_s, bssid);
	if (bss) {
		bss->disallowed_until = until;
		return;
	}

	bss = os_malloc(sizeof(*bss));
	if (!bss) {
		wpa_printf(MSG_DEBUG,
			   "Failed to allocate memory for temp disallow BSS");
		return;
	}

	bss->disallowed_until = until;
	os_memcpy(bss->bssid, bssid, ETH_ALEN);
	dl_list_add(&wpa_s->bss_tmp_disallowed, &bss->list);
}


int wpa_is_bss_tmp_disallowed(struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	struct wpa_bss_tmp_disallowed *bss = NULL, *tmp, *prev;
	struct os_reltime now, age;

	os_get_reltime(&now);

	dl_list_for_each_safe(tmp, prev, &wpa_s->bss_tmp_disallowed,
			 struct wpa_bss_tmp_disallowed, list) {
		if (!os_reltime_before(&now, &tmp->disallowed_until)) {
			/* This BSS is not disallowed anymore */
			dl_list_del(&tmp->list);
			os_free(tmp);
			continue;
		}
		if (os_memcmp(bssid, tmp->bssid, ETH_ALEN) == 0) {
			bss = tmp;
			break;
		}
	}
	if (!bss)
		return 0;

	os_reltime_sub(&bss->disallowed_until, &now, &age);
	wpa_printf(MSG_DEBUG,
		   "BSS " MACSTR " disabled for %ld.%0ld seconds",
		   MAC2STR(bss->bssid), age.sec, age.usec);
	return 1;
}
#endif
