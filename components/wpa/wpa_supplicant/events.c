/*
 * WPA Supplicant - Driver event processing
 * Copyright (c) 2003-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "rsn_supp/wpa.h"
#include "eloop.h"
#include "config.h"
#include "l2_packet/l2_packet.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "pcsc_funcs.h"
#include "common/wpa_ctrl.h"
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#include "ap/tkip_countermeasures.h"
#include "notify.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "crypto/random.h"
#include "wpas_glue.h"
#include "wps_supplicant.h"
#include "ibss_rsn.h"
#include "sme.h"
#include "ap.h"
#include "bss.h"
#include "scan.h"
#include "offchannel.h"
#include "wpas_pmk.h"

#include "system_event.h"

uint8_t g_balconn = 0;

#ifndef CONFIG_NO_SCAN_PROCESSING
static int wpas_select_network_from_last_scan(struct wpa_supplicant *wpa_s,
					      int new_scan, int own_request);
#endif /* CONFIG_NO_SCAN_PROCESSING */


static int wpas_temp_disabled(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid)
{
#if 0
	struct os_reltime now;

	if (ssid == NULL || ssid->disabled_until.sec == 0)
		return 0;

	os_get_reltime(&now);
	if (ssid->disabled_until.sec > now.sec)
		return ssid->disabled_until.sec - now.sec;

	wpas_clear_temp_disabled(wpa_s, ssid, 0);
#endif
	return 0;
}


#ifndef CONFIG_NO_SCAN_PROCESSING
#if 0
/**
 * wpas_reenabled_network_time - Time until first network is re-enabled
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: If all enabled networks are temporarily disabled, returns the time
 *	(in sec) until the first network is re-enabled. Otherwise returns 0.
 *
 * This function is used in case all enabled networks are temporarily disabled,
 * in which case it returns the time (in sec) that the first network will be
 * re-enabled. The function assumes that at least one network is enabled.
 */
static int wpas_reenabled_network_time(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;
	int disabled_for, res = 0;

#ifdef CONFIG_INTERWORKING
	if (wpa_s->conf->auto_interworking && wpa_s->conf->interworking &&
	    wpa_s->conf->cred)
		return 0;
#endif /* CONFIG_INTERWORKING */

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid->disabled)
			continue;

		disabled_for = wpas_temp_disabled(wpa_s, ssid);
		if (!disabled_for)
			return 0;

		if (!res || disabled_for < res)
			res = disabled_for;
	}

	return res;
}
#endif
#endif /* CONFIG_NO_SCAN_PROCESSING */


void wpas_network_reenabled(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->disconnected || wpa_s->wpa_state != WPA_SCANNING)
		return;

	wpa_dbg(wpa_s, MSG_DEBUG,
		"Try to associate due to network getting re-enabled");
	if (wpa_supplicant_fast_associate(wpa_s) != 1) {
		//wpa_supplicant_cancel_sched_scan(wpa_s);
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}


static struct wpa_bss * wpa_supplicant_get_new_bss(
	struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	struct wpa_bss *bss = NULL;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (ssid->ssid_len > 0)
		bss = wpa_bss_get(wpa_s, bssid, ssid->ssid, ssid->ssid_len);
	if (!bss)
		bss = wpa_bss_get_bssid(wpa_s, bssid);

	return bss;
}


static void wpa_supplicant_update_current_bss(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss = wpa_supplicant_get_new_bss(wpa_s, wpa_s->bssid);

	if (!bss) {
		wpa_supplicant_update_scan_results(wpa_s);

		/* Get the BSS from the new scan results */
		bss = wpa_supplicant_get_new_bss(wpa_s, wpa_s->bssid);
	}

	if (bss)
		wpa_s->current_bss = bss;
}


static int wpa_supplicant_select_config(struct wpa_supplicant *wpa_s)
{
	//struct wpa_ssid *ssid, *old_ssid;
	struct wpa_ssid *ssid;
	u8 drv_ssid[SSID_MAX_LEN];
	size_t drv_ssid_len;
	int res;

	if (wpa_s->conf->ap_scan == 1 && wpa_s->current_ssid) {
		wpa_supplicant_update_current_bss(wpa_s);

		if (wpa_s->current_ssid->ssid_len == 0)
			return 0; /* current profile still in use */
		res = wpa_drv_get_ssid(wpa_s, drv_ssid);
		if (res < 0) {
			wpa_msg(wpa_s, MSG_INFO,
				"Failed to read SSID from driver");
			return 0; /* try to use current profile */
		}
		drv_ssid_len = res;

		if (drv_ssid_len == wpa_s->current_ssid->ssid_len &&
		    os_memcmp(drv_ssid, wpa_s->current_ssid->ssid,
			      drv_ssid_len) == 0)
			return 0; /* current profile still in use */

		wpa_msg(wpa_s, MSG_DEBUG,
			"Driver-initiated BSS selection changed the SSID to %s",
			wpa_ssid_txt(drv_ssid, drv_ssid_len));
		/* continue selecting a new network profile */
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "Select network based on association "
		"information");
	ssid = wpa_supplicant_get_ssid(wpa_s);
	if (ssid == NULL) {
		wpa_msg(wpa_s, MSG_INFO,
			"No network configuration found for the current AP");
		return -1;
	}

	if (wpas_network_disabled(wpa_s, ssid)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Selected network is disabled");
		return -1;
	}

	res = wpas_temp_disabled(wpa_s, ssid);
	if (res > 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Selected network is temporarily "
			"disabled for %d second(s)", res);
		return -1;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "Network configuration found for the "
		"current AP");
	if (wpa_key_mgmt_wpa_any(ssid->key_mgmt)) {
		u8 wpa_ie[80];
		size_t wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_set_suites(wpa_s, NULL, ssid,
					      wpa_ie, &wpa_ie_len) < 0)
			wpa_dbg(wpa_s, MSG_DEBUG, "Could not set WPA suites");
	} else {
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
	}

	//old_ssid = wpa_s->current_ssid;
	wpa_s->current_ssid = ssid;

	wpa_supplicant_update_current_bss(wpa_s);

	wpa_supplicant_rsn_supp_set_config(wpa_s, wpa_s->current_ssid);
	//wpa_supplicant_initiate_eapol(wpa_s);
	//if (old_ssid != wpa_s->current_ssid)
	//	wpas_notify_network_changed(wpa_s);

	return 0;
}


void wpa_supplicant_stop_countermeasures(void *eloop_ctx, void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->countermeasures) {
		wpa_s->countermeasures = 0;
		//wpa_drv_set_countermeasures(wpa_s, 0);
		wpa_msg(wpa_s, MSG_INFO, "WPA: TKIP countermeasures stopped");

		/*
		 * It is possible that the device is sched scanning, which means
		 * that a connection attempt will be done only when we receive
		 * scan results. However, in this case, it would be preferable
		 * to scan and connect immediately, so cancel the sched_scan and
		 * issue a regular scan flow.
		 */
		//wpa_supplicant_cancel_sched_scan(wpa_s);
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}


void wpa_supplicant_mark_disassoc(struct wpa_supplicant *wpa_s)
{
	//int bssid_changed;

	//wnm_bss_keep_alive_deinit(wpa_s);

#ifdef CONFIG_IBSS_RSN
	ibss_rsn_deinit(wpa_s->ibss_rsn);
	wpa_s->ibss_rsn = NULL;
#endif /* CONFIG_IBSS_RSN */

#ifdef CONFIG_AP
	wpa_supplicant_ap_deinit(wpa_s);
#endif /* CONFIG_AP */

#ifdef CONFIG_HS20
	/* Clear possibly configured frame filters */
	wpa_drv_configure_frame_filters(wpa_s, 0);
#endif /* CONFIG_HS20 */

	//if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED)
	//	return;

	wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
	//bssid_changed = !is_zero_ether_addr(wpa_s->bssid);
	os_memset(wpa_s->bssid, 0, ETH_ALEN);
	os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
	sme_clear_on_disassoc(wpa_s);
	wpa_s->current_bss = NULL;
	wpa_s->assoc_freq = 0;

	//if (bssid_changed)
	//	wpas_notify_bssid_changed(wpa_s);

	wpa_s->ap_ies_from_associnfo = 0;
	wpa_s->current_ssid = NULL;
	wpa_s->key_mgmt = 0;

	wpa_s->wnmsleep_used = 0;
}


static void wpa_find_assoc_pmkid(struct wpa_supplicant *wpa_s)
{
	struct wpa_ie_data ie;
	int pmksa_set __attribute__((unused)) = -1;
	size_t i;
	if (wpa_sm_parse_own_wpa_ie(wpa_s->wpa, &ie) < 0 ||
	    ie.pmkid == NULL)
		return;

	for (i = 0; i < ie.num_pmkid; i++) {
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "RSN: PMKID from assoc IE %sfound from "
		"PMKSA cache", pmksa_set == 0 ? "" : "not ");
}


static int wpa_supplicant_dynamic_keys(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE)
		return 0;

#ifdef IEEE8021X_EAPOL
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA &&
	    wpa_s->current_ssid &&
	    !(wpa_s->current_ssid->eapol_flags &
	      (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
	       EAPOL_FLAG_REQUIRE_KEY_BROADCAST))) {
		/* IEEE 802.1X, but not using dynamic WEP keys (i.e., either
		 * plaintext or static WEP keys). */
		return 0;
	}
#endif /* IEEE8021X_EAPOL */

	return 1;
}


/**
 * wpa_supplicant_scard_init - Initialize SIM/USIM access with PC/SC
 * @wpa_s: pointer to wpa_supplicant data
 * @ssid: Configuration data for the network
 * Returns: 0 on success, -1 on failure
 *
 * This function is called when starting authentication with a network that is
 * configured to use PC/SC for SIM/USIM access (EAP-SIM or EAP-AKA).
 */
int wpa_supplicant_scard_init(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid)
{
#ifdef IEEE8021X_EAPOL
#ifdef PCSC_FUNCS
	int aka = 0, sim = 0;

	if ((ssid != NULL && ssid->eap.pcsc == NULL) ||
	    wpa_s->scard != NULL || wpa_s->conf->external_sim)
		return 0;

	if (ssid == NULL || ssid->eap.eap_methods == NULL) {
		sim = 1;
		aka = 1;
	} else {
		struct eap_method_type *eap = ssid->eap.eap_methods;
		while (eap->vendor != EAP_VENDOR_IETF ||
		       eap->method != EAP_TYPE_NONE) {
			if (eap->vendor == EAP_VENDOR_IETF) {
				if (eap->method == EAP_TYPE_SIM)
					sim = 1;
				else if (eap->method == EAP_TYPE_AKA ||
					 eap->method == EAP_TYPE_AKA_PRIME)
					aka = 1;
			}
			eap++;
		}
	}

	if (eap_peer_get_eap_method(EAP_VENDOR_IETF, EAP_TYPE_SIM) == NULL)
		sim = 0;
	if (eap_peer_get_eap_method(EAP_VENDOR_IETF, EAP_TYPE_AKA) == NULL &&
	    eap_peer_get_eap_method(EAP_VENDOR_IETF, EAP_TYPE_AKA_PRIME) ==
	    NULL)
		aka = 0;

	if (!sim && !aka) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Selected network is configured to "
			"use SIM, but neither EAP-SIM nor EAP-AKA are "
			"enabled");
		return 0;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "Selected network is configured to use SIM "
		"(sim=%d aka=%d) - initialize PCSC", sim, aka);

	wpa_s->scard = scard_init(wpa_s->conf->pcsc_reader);
	if (wpa_s->scard == NULL) {
		wpa_msg(wpa_s, MSG_WARNING, "Failed to initialize SIM "
			"(pcsc-lite)");
		return -1;
	}
	wpa_sm_set_scard_ctx(wpa_s->wpa, wpa_s->scard);
	eapol_sm_register_scard_ctx(wpa_s->eapol, wpa_s->scard);
#endif /* PCSC_FUNCS */
#endif /* IEEE8021X_EAPOL */

	return 0;
}


#ifndef CONFIG_NO_SCAN_PROCESSING

static int has_wep_key(struct wpa_ssid *ssid)
{
	int i;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i])
			return 1;
	}

	return 0;
}


static int wpa_supplicant_match_privacy(struct wpa_bss *bss,
					struct wpa_ssid *ssid)
{
	int privacy = 0;

	//if (ssid->mixed_cell)
	//	return 1;

#ifdef CONFIG_WPS
	if (ssid->key_mgmt & WPA_KEY_MGMT_WPS)
		return 1;
#endif /* CONFIG_WPS */

	if (has_wep_key(ssid))
		privacy = 1;

#ifdef IEEE8021X_EAPOL
	if ((ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) &&
	    ssid->eapol_flags & (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
				 EAPOL_FLAG_REQUIRE_KEY_BROADCAST))
		privacy = 1;
#endif /* IEEE8021X_EAPOL */

	if (wpa_key_mgmt_wpa(ssid->key_mgmt))
		privacy = 1;

	if (ssid->key_mgmt & WPA_KEY_MGMT_OSEN)
		privacy = 1;

	if (bss->caps & IEEE80211_CAP_PRIVACY)
		return privacy;
	return !privacy;
}

static void wpa_set_psk_to_wep(struct wpa_ssid * ssid)
{
	if(ssid->wep_key_len[0] == 0 && ssid->passphrase != NULL && (
		os_strlen(ssid->passphrase) == 10 || os_strlen(ssid->passphrase) == 13 || os_strlen(ssid->passphrase) == 26))
	{
		ssid->key_mgmt = WPA_KEY_MGMT_NONE;
		if(os_strlen(ssid->passphrase) == 10 || os_strlen(ssid->passphrase) == 26)
		{
			size_t tlen, hlen = os_strlen(ssid->passphrase);
			tlen = hlen / 2;
			hexstr2bin(ssid->passphrase, ssid->wep_key[0], tlen);
			ssid->wep_key[0][tlen] = '\0';
			ssid->wep_key_len[0] = tlen;
		}
		else
		{
			os_memcpy(&ssid->wep_key[0], ssid->passphrase, 13);
			ssid->wep_key_len[0] = 13;
		}
	}

}
static void wpa_set_wep_to_psk(struct wpa_ssid * ssid)
{
    if (!has_wep_key(ssid))
    {
        return;
    }

    if (ssid->passphrase != NULL && (os_strlen(ssid->passphrase) == 10 || os_strlen(ssid->passphrase) == 13
        || os_strlen(ssid->passphrase) == 26))
    {
        ssid->key_mgmt = DEFAULT_KEY_MGMT;
        ssid->wep_key_len[0] = 0;
    }
}

static int wpa_supplicant_ssid_bss_match(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid,
					 struct wpa_bss *bss)
{
	struct wpa_ie_data ie;
	int proto_match = 0;
	const u8 *rsn_ie, *wpa_ie;
	int wep_ok;

	/*ret = wpas_wps_ssid_bss_match(wpa_s, ssid, bss);
	if (ret >= 0)
		return ret;*/

	/* Allow TSN if local configuration accepts WEP use without WPA/WPA2 */
	#if 0
	wep_ok = !wpa_key_mgmt_wpa(ssid->key_mgmt) &&
		(((ssid->key_mgmt & WPA_KEY_MGMT_NONE) &&
		  ssid->wep_key_len[ssid->wep_tx_keyidx] > 0) ||
		 (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA));
	#else
	wep_ok = 1;
	#endif
	rsn_ie = wpa_bss_get_ie(bss, WLAN_EID_RSN);
	while ((ssid->proto & WPA_PROTO_RSN) && rsn_ie) {
		proto_match++;

		if (wpa_parse_wpa_ie(rsn_ie, 2 + rsn_ie[1], &ie)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - parse "
				"failed");
			break;
		}

		if (wep_ok &&
		    (ie.group_cipher & (WPA_CIPHER_WEP40 | WPA_CIPHER_WEP104)))
		{
			wpa_dbg(wpa_s, MSG_DEBUG, "   selected based on TSN "
				"in RSN IE");

			wpa_set_psk_to_wep(ssid);

			return 1;
		}

		if (!(ie.proto & ssid->proto)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - proto "
				"mismatch");
			break;
		}

		if (!(ie.pairwise_cipher & ssid->pairwise_cipher)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - PTK "
				"cipher mismatch");
			break;
		}

		if (!(ie.group_cipher & ssid->group_cipher)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - GTK "
				"cipher mismatch");
			break;
		}

        wpa_set_wep_to_psk(ssid);

		if (!(ie.key_mgmt & ssid->key_mgmt)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - key mgmt "
				"mismatch");
			break;
		}
#ifdef CONFIG_SAE
		if(wpa_key_mgmt_sae(ie.key_mgmt))
			ssid->ieee80211w = MGMT_FRAME_PROTECTION_REQUIRED;
		else
			ssid->ieee80211w = MGMT_FRAME_PROTECTION_DEFAULT;
#endif
#ifdef CONFIG_IEEE80211W
		if((ie.capabilities & WPA_CAPABILITY_MFPC))
			ssid->ieee80211w = MGMT_FRAME_PROTECTION_OPTIONAL;
		else
			ssid->ieee80211w = NO_MGMT_FRAME_PROTECTION;
			
		if (!(ie.capabilities & WPA_CAPABILITY_MFPC) &&
		    wpas_get_ssid_pmf(wpa_s, ssid) ==
		    MGMT_FRAME_PROTECTION_REQUIRED) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip RSN IE - no mgmt "
				"frame protection");
			break;
		}

		if((ie.capabilities & WPA_CAPABILITY_MFPR))
			ssid->ieee80211w = MGMT_FRAME_PROTECTION_REQUIRED;
#endif /* CONFIG_IEEE80211W */
		if ((ie.capabilities & WPA_CAPABILITY_MFPR) &&
		    wpas_get_ssid_pmf(wpa_s, ssid) ==
		    NO_MGMT_FRAME_PROTECTION) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip RSN IE - no mgmt frame protection enabled but AP requires it");
			break;
		}
#ifdef CONFIG_MBO
		if (!(ie.capabilities & WPA_CAPABILITY_MFPC) &&
		    wpas_mbo_get_bss_attr(bss, MBO_ATTR_ID_AP_CAPA_IND) &&
		    wpas_get_ssid_pmf(wpa_s, ssid) !=
		    NO_MGMT_FRAME_PROTECTION) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip RSN IE - no mgmt frame protection enabled on MBO AP");
			break;
		}
#endif /* CONFIG_MBO */

		wpa_dbg(wpa_s, MSG_DEBUG, "   selected based on RSN IE");
		return 1;
	}

#ifdef CONFIG_IEEE80211W
	if(!rsn_ie)
		ssid->ieee80211w = NO_MGMT_FRAME_PROTECTION;

	if (wpas_get_ssid_pmf(wpa_s, ssid) == MGMT_FRAME_PROTECTION_REQUIRED) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"   skip - MFP Required but network not MFP Capable");
		return 0;
	}
#endif /* CONFIG_IEEE80211W */

	wpa_ie = wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE);
	while ((ssid->proto & WPA_PROTO_WPA) && wpa_ie) {
		proto_match++;

		if (wpa_parse_wpa_ie(wpa_ie, 2 + wpa_ie[1], &ie)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip WPA IE - parse "
				"failed");
			break;
		}

		if (wep_ok &&
		    (ie.group_cipher & (WPA_CIPHER_WEP40 | WPA_CIPHER_WEP104)))
		{
			wpa_dbg(wpa_s, MSG_DEBUG, "   selected based on TSN "
				"in WPA IE");

			wpa_set_psk_to_wep(ssid);

			return 1;
		}

		if (!(ie.proto & ssid->proto)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip WPA IE - proto "
				"mismatch");
			break;
		}

		if (!(ie.pairwise_cipher & ssid->pairwise_cipher)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip WPA IE - PTK "
				"cipher mismatch");
			break;
		}

		if (!(ie.group_cipher & ssid->group_cipher)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip WPA IE - GTK "
				"cipher mismatch");
			break;
		}

        wpa_set_wep_to_psk(ssid);

		if (!(ie.key_mgmt & ssid->key_mgmt)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip WPA IE - key mgmt "
				"mismatch");
			break;
		}

		wpa_dbg(wpa_s, MSG_DEBUG, "   selected based on WPA IE");
		return 1;
	}

	/*if ((ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) && !wpa_ie &&
	    !rsn_ie) {
		wpa_dbg(wpa_s, MSG_DEBUG, "   allow for non-WPA IEEE 802.1X");
		return 1;
	}*/
	
	if(proto_match == 0)
	{
		wpa_set_psk_to_wep(ssid);
	}
	
	if ((ssid->proto & (WPA_PROTO_WPA | WPA_PROTO_RSN)) &&
	    wpa_key_mgmt_wpa(ssid->key_mgmt) && proto_match == 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "   skip - no WPA/RSN proto match");
		return 0;
	}

	/*if ((ssid->key_mgmt & WPA_KEY_MGMT_OSEN) &&
	    wpa_bss_get_vendor_ie(bss, OSEN_IE_VENDOR_TYPE)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "   allow in OSEN");
		return 1;
	}*/

	if (!wpa_key_mgmt_wpa(ssid->key_mgmt)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "   allow in non-WPA/WPA2");
		return 1;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "   reject due to mismatch with "
		"WPA/WPA2");

	return 0;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int freq_allowed(int *freqs, int freq)
{
	int i;

	if (freqs == NULL)
		return 1;

	for (i = 0; freqs[i]; i++)
		if (freqs[i] == freq)
			return 1;
	return 0;
}


static int rate_match(struct wpa_supplicant *wpa_s, struct wpa_bss *bss)
{
	const struct hostapd_hw_modes *mode = NULL, *modes;
	const u8 scan_ie[2] = { WLAN_EID_SUPP_RATES, WLAN_EID_EXT_SUPP_RATES };
	const u8 *rate_ie;
	int i, j, k;

	if (bss->freq == 0)
		return 1; /* Cannot do matching without knowing band */

	modes = wpa_s->hw.modes;
	if (modes == NULL) {
		/*
		 * The driver does not provide any additional information
		 * about the utilized hardware, so allow the connection attempt
		 * to continue.
		 */
		return 1;
	}

	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		for (j = 0; j < modes[i].num_channels; j++) {
			int freq = modes[i].channels[j].freq;
			if (freq == bss->freq) {
				if (mode &&
				    mode->mode == HOSTAPD_MODE_IEEE80211G)
					break; /* do not allow 802.11b replace
						* 802.11g */
				mode = &modes[i];
				break;
			}
		}
	}

	if (mode == NULL)
		return 0;

	for (i = 0; i < (int) sizeof(scan_ie); i++) {
		rate_ie = wpa_bss_get_ie(bss, scan_ie[i]);
		if (rate_ie == NULL)
			continue;

		for (j = 2; j < rate_ie[1] + 2; j++) {
			int flagged = !!(rate_ie[j] & 0x80);
			int r = (rate_ie[j] & 0x7f) * 5;

			/*
			 * IEEE Std 802.11n-2009 7.3.2.2:
			 * The new BSS Membership selector value is encoded
			 * like a legacy basic rate, but it is not a rate and
			 * only indicates if the BSS members are required to
			 * support the mandatory features of Clause 20 [HT PHY]
			 * in order to join the BSS.
			 */
			if (flagged && ((rate_ie[j] & 0x7f) ==
					BSS_MEMBERSHIP_SELECTOR_HT_PHY)) {
				if (!ht_supported(mode)) {
					wpa_dbg(wpa_s, MSG_DEBUG,
						"   hardware does not support "
						"HT PHY");
					return 0;
				}
				continue;
			}

			/* There's also a VHT selector for 802.11ac */
			if (flagged && ((rate_ie[j] & 0x7f) ==
					BSS_MEMBERSHIP_SELECTOR_VHT_PHY)) {
				if (!vht_supported(mode)) {
					wpa_dbg(wpa_s, MSG_DEBUG,
						"   hardware does not support "
						"VHT PHY");
					return 0;
				}
				continue;
			}

			if (!flagged)
				continue;

			/* check for legacy basic rates */
			for (k = 0; k < mode->num_rates; k++) {
				if (mode->rates[k] == r)
					break;
			}
			if (k == mode->num_rates) {
				/*
				 * IEEE Std 802.11-2007 7.3.2.2 demands that in
				 * order to join a BSS all required rates
				 * have to be supported by the hardware.
				 */
				wpa_dbg(wpa_s, MSG_DEBUG,
					"   hardware does not support required rate %d.%d Mbps (freq=%d mode==%d num_rates=%d)",
					r / 10, r % 10,
					bss->freq, mode->mode, mode->num_rates);
				return 0;
			}
		}
	}

	return 1;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


/*
 * Test whether BSS is in an ESS.
 * This is done differently in DMG (60 GHz) and non-DMG bands
 */
static int bss_is_ess(struct wpa_bss *bss)
{
	if (bss_is_dmg(bss)) {
		return (bss->caps & IEEE80211_CAP_DMG_MASK) ==
			IEEE80211_CAP_DMG_AP;
	}

	return ((bss->caps & (IEEE80211_CAP_ESS | IEEE80211_CAP_IBSS)) ==
		IEEE80211_CAP_ESS);
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int match_mac_mask(const u8 *addr_a, const u8 *addr_b, const u8 *mask)
{
	size_t i;

	for (i = 0; i < ETH_ALEN; i++) {
		if ((addr_a[i] & mask[i]) != (addr_b[i] & mask[i]))
			return 0;
	}
	return 1;
}


static int addr_in_list(const u8 *addr, const u8 *list, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++) {
		const u8 *a = list + i * ETH_ALEN * 2;
		const u8 *m = a + ETH_ALEN;

		if (match_mac_mask(a, addr, m))
			return 1;
	}
	return 0;
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */

void wpa_scan_res_mark_fail(struct wpa_supplicant *wpa_s)
{
    uint8_t *bssid;
    if (wpa_s->wpa_state >= WPA_ASSOCIATED)
    {
        bssid = wpa_s->bssid;
    }
    else
    {
        bssid = wpa_s->pending_bssid;
    }
    if (g_balconn)
    {
        for (int i = 0; i < wpa_s->last_scan_res_used; i++) 
        {
            struct wpa_bss *bss = wpa_s->last_scan_res[i];
            if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0)
            {
                if (bss->fail_cnt < 0xff)
                {
                    bss->fail_cnt++;
                }
            }
        }
    }
}

void wpa_scan_res_clear_fail(struct wpa_supplicant *wpa_s)
{
    uint8_t *bssid;
    if (wpa_s->wpa_state >= WPA_ASSOCIATED)
    {
        bssid = wpa_s->bssid;
    }
    else
    {
        bssid = wpa_s->pending_bssid;
    }
    if (g_balconn)
    {
        for (int i = 0; i < wpa_s->last_scan_res_used; i++) 
        {
            struct wpa_bss *bss = wpa_s->last_scan_res[i];
            if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0)
            {
                    bss->fail_cnt = 0;
            }
        }
    }
}


struct wpa_ssid * wpa_scan_res_match(struct wpa_supplicant *wpa_s,
				     int i, struct wpa_bss *bss,
				     struct wpa_ssid *group,
				     int only_first_ssid)
{
	u8 wpa_ie_len, rsn_ie_len;
	int wpa;
	const u8 *ie;
	struct wpa_ssid *ssid;
	int osen;
#ifdef CONFIG_MBO
	const u8 *assoc_disallow;
#endif /* CONFIG_MBO */

	ie = wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE);
	wpa_ie_len = ie ? ie[1] : 0;

	ie = wpa_bss_get_ie(bss, WLAN_EID_RSN);
	rsn_ie_len = ie ? ie[1] : 0;

	ie = wpa_bss_get_vendor_ie(bss, OSEN_IE_VENDOR_TYPE);
	osen = ie != NULL;

	wpa_dbg(wpa_s, MSG_DEBUG, "%d: " MACSTR " ssid='%s' "
		"wpa_ie_len=%u rsn_ie_len=%u caps=0x%x level=%d freq=%d %s%s%s",
		i, MAC2STR(bss->bssid), wpa_ssid_txt(bss->ssid, bss->ssid_len),
		wpa_ie_len, rsn_ie_len, bss->caps, bss->level, bss->freq,
		wpa_bss_get_vendor_ie(bss, WPS_IE_VENDOR_TYPE) ? " wps" : "",
		(wpa_bss_get_vendor_ie(bss, P2P_IE_VENDOR_TYPE) ||
		 wpa_bss_get_vendor_ie_beacon(bss, P2P_IE_VENDOR_TYPE)) ?
		" p2p" : "",
		osen ? " osen=1" : "");

	if (bss->ssid_len == 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "   skip - SSID not known");
		return NULL;
	}

	wpa = wpa_ie_len > 0 || rsn_ie_len > 0;

	for (ssid = group; ssid; ssid = only_first_ssid ? NULL : ssid->pnext) {
		int check_ssid = wpa ? 1 : (ssid->ssid_len != 0);

		if (wpas_network_disabled(wpa_s, ssid)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - disabled");
			continue;
		}

		/*res = wpas_temp_disabled(wpa_s, ssid);
		if (res > 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - disabled "
				"temporarily for %d second(s)", res);
			continue;
		}*/

#ifdef CONFIG_WPS
		if ((ssid->key_mgmt & WPA_KEY_MGMT_WPS) && e && e->count > 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - blacklisted "
				"(WPS)");
			continue;
		}

		if (wpa && ssid->ssid_len == 0 &&
		    wpas_wps_ssid_wildcard_ok(wpa_s, ssid, bss))
			check_ssid = 0;

		if (!wpa && (ssid->key_mgmt & WPA_KEY_MGMT_WPS)) {
			/* Only allow wildcard SSID match if an AP
			 * advertises active WPS operation that matches
			 * with our mode. */
			check_ssid = 1;
			if (ssid->ssid_len == 0 &&
			    wpas_wps_ssid_wildcard_ok(wpa_s, ssid, bss))
				check_ssid = 0;
		}
#endif /* CONFIG_WPS */

		if (ssid->bssid_set && ssid->ssid_len == 0 &&
		    os_memcmp(bss->bssid, ssid->bssid, ETH_ALEN) == 0)
			check_ssid = 0;

		if (check_ssid &&
		    (bss->ssid_len != ssid->ssid_len ||
		     os_memcmp(bss->ssid, ssid->ssid, bss->ssid_len) != 0)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - SSID mismatch");
			continue;
		}

		if (ssid->bssid_set &&
		    os_memcmp(bss->bssid, ssid->bssid, ETH_ALEN) != 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - BSSID mismatch");
			continue;
		}


		if (!wpa_supplicant_ssid_bss_match(wpa_s, ssid, bss))
			continue;

		if (!osen && !wpa &&
		    !(ssid->key_mgmt & WPA_KEY_MGMT_NONE) &&
		    !(ssid->key_mgmt & WPA_KEY_MGMT_WPS) &&
		    !(ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - non-WPA network "
				"not allowed");
			continue;
		}

		if (wpa && !wpa_key_mgmt_wpa(ssid->key_mgmt) &&
		    has_wep_key(ssid)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - ignore WPA/WPA2 AP for WEP network block");
			continue;
		}

		/*if ((ssid->key_mgmt & WPA_KEY_MGMT_OSEN) && !osen) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - non-OSEN network "
				"not allowed");
			continue;
		}*/

		if (!wpa_supplicant_match_privacy(bss, ssid)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - privacy "
				"mismatch");
			continue;
		}

		if (ssid->mode != IEEE80211_MODE_MESH && !bss_is_ess(bss) &&
		    !bss_is_pbss(bss)) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip - not ESS, PBSS, or MBSS");
			continue;
		}

#if 0
		if (ssid->pbss != 2 && ssid->pbss != bss_is_pbss(bss)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - PBSS mismatch (ssid %d bss %d)",
				ssid->pbss, bss_is_pbss(bss));
			continue;
		}
#endif
		/*if (!freq_allowed(ssid->freq_list, bss->freq)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - frequency not "
				"allowed");
			continue;
		}*/

#ifdef CONFIG_MESH
		if (ssid->mode == IEEE80211_MODE_MESH && ssid->frequency > 0 &&
		    ssid->frequency != bss->freq) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - frequency not allowed (mesh)");
			continue;
		}
#endif /* CONFIG_MESH */

		/*if (!rate_match(wpa_s, bss)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - rate sets do "
				"not match");
			continue;
		}*/

#ifdef CONFIG_P2P
		if (ssid->p2p_group &&
		    !wpa_bss_get_vendor_ie(bss, P2P_IE_VENDOR_TYPE) &&
		    !wpa_bss_get_vendor_ie_beacon(bss, P2P_IE_VENDOR_TYPE)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "   skip - no P2P IE seen");
			continue;
		}

		if (!is_zero_ether_addr(ssid->go_p2p_dev_addr)) {
			struct wpabuf *p2p_ie;
			u8 dev_addr[ETH_ALEN];

			ie = wpa_bss_get_vendor_ie(bss, P2P_IE_VENDOR_TYPE);
			if (ie == NULL) {
				wpa_dbg(wpa_s, MSG_DEBUG, "   skip - no P2P element");
				continue;
			}
			p2p_ie = wpa_bss_get_vendor_ie_multi(
				bss, P2P_IE_VENDOR_TYPE);
			if (p2p_ie == NULL) {
				wpa_dbg(wpa_s, MSG_DEBUG, "   skip - could not fetch P2P element");
				continue;
			}

			if (p2p_parse_dev_addr_in_p2p_ie(p2p_ie, dev_addr) < 0
			    || os_memcmp(dev_addr, ssid->go_p2p_dev_addr,
					 ETH_ALEN) != 0) {
				wpa_dbg(wpa_s, MSG_DEBUG, "   skip - no matching GO P2P Device Address in P2P element");
				wpabuf_free(p2p_ie);
				continue;
			}
			wpabuf_free(p2p_ie);
		}

		/*
		 * TODO: skip the AP if its P2P IE has Group Formation
		 * bit set in the P2P Group Capability Bitmap and we
		 * are not in Group Formation with that device.
		 */
#endif /* CONFIG_P2P */

		if (os_reltime_before(&bss->last_update, &wpa_s->scan_min_time))
		{
			struct os_reltime diff;

			os_reltime_sub(&wpa_s->scan_min_time,
				       &bss->last_update, &diff);
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip - scan result not recent enough (%u.%06u seconds too old)",
				(unsigned int) diff.sec,
				(unsigned int) diff.usec);
			continue;
		}
#ifdef CONFIG_MBO
#ifdef CONFIG_TESTING_OPTIONS
		if (wpa_s->ignore_assoc_disallow)
			goto skip_assoc_disallow;
#endif /* CONFIG_TESTING_OPTIONS */
		assoc_disallow = wpas_mbo_get_bss_attr(
			bss, MBO_ATTR_ID_ASSOC_DISALLOW);
		if (assoc_disallow && assoc_disallow[1] >= 1) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip - MBO association disallowed (reason %u)",
				assoc_disallow[2]);
			continue;
		}

		if (wpa_is_bss_tmp_disallowed(wpa_s, bss->bssid)) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"   skip - MBO retry delay has not passed yet");
			continue;
		}
#ifdef CONFIG_TESTING_OPTIONS
	skip_assoc_disallow:
#endif /* CONFIG_TESTING_OPTIONS */
#endif /* CONFIG_MBO */

		/* Matching configuration found */
		return ssid;
	}

	/* No matching configuration found */
	return NULL;
}

static struct wpa_bss *
wpa_supplicant_select_bss(struct wpa_supplicant *wpa_s,
			  struct wpa_ssid *group,
			  struct wpa_ssid **selected_ssid,
			  int only_first_ssid)
{
	unsigned int i;
	struct wpa_bss *selected_bss = NULL;
	struct wpa_ssid *match_ssid = NULL;
    
	signed char rssi = -120;
	uint8_t min_fail_cnt = 0xff;
	/*if (only_first_ssid)
		wpa_dbg(wpa_s, MSG_DEBUG, "Try to find BSS matching pre-selected network id=%d",
			group->id);
	else
		wpa_dbg(wpa_s, MSG_DEBUG, "Selecting BSS from priority group %d",
			group->priority);*/

	for (i = 0; i < wpa_s->last_scan_res_used; i++) {
		struct wpa_bss *bss = wpa_s->last_scan_res[i];
		match_ssid= wpa_scan_res_match(wpa_s, i, bss, group,
						    only_first_ssid);
		if (!match_ssid)
			continue;
		wpa_dbg(wpa_s, MSG_DEBUG, "   selected BSS " MACSTR
			" ssid='%s'",
			MAC2STR(bss->bssid),
			wpa_ssid_txt(bss->ssid, bss->ssid_len));
		wpa_dbg(wpa_s,MSG_DEBUG,"fail_cnt = %d,"MACSTR"\n",bss->fail_cnt, MAC2STR(bss->bssid));

		if (g_balconn)
		{
			if (bss->fail_cnt < min_fail_cnt)
			{
				min_fail_cnt = bss->fail_cnt;
				rssi = (signed char )bss->level;
				selected_bss = bss;
				*selected_ssid = match_ssid;
			}
			else if (bss->fail_cnt == min_fail_cnt)
			{
				if(rssi <= (signed char)bss->level)
				{
					rssi = (signed char )bss->level;
					selected_bss = bss;
					*selected_ssid = match_ssid;
				}
			}
		}
		else
		{
			if(rssi <= (signed char)bss->level)
			{
				rssi = (signed char )bss->level;
				selected_bss = bss;
				*selected_ssid = match_ssid;
			}
		}
	}
	return selected_bss;
}

// TODO:  should consider  40MHz bw.
static int ap_channel_auto_select(struct wpa_supplicant *wpa_s)
{
    //default chn 1
    int i, req = 2412, num_of_freq[3] = {0}; 
    
	if (wpa_s->last_scan_res == NULL || wpa_s->last_scan_res_used == 0)
	    return req;

    for (i = 0; i < wpa_s->last_scan_res_used; i++) {
        struct wpa_bss *bss = wpa_s->last_scan_res[i];
        #if 0
        extern void system_printf(const char *f, ...);
        system_printf("--ssid[%s],freq[%d],rssi[%d]\n", wpa_ssid_txt(bss->ssid, bss->ssid_len),
            bss->freq, bss->level);
        #endif
        if (bss->freq <= 2422)
            num_of_freq[0]++; // ap num of channel 1
        else if (bss->freq <= 2447)
            num_of_freq[1]++; // ap num of channel 6
        else
            num_of_freq[2]++; // ap num of channel 11
    }

    if (num_of_freq[0] <= num_of_freq[1] && num_of_freq[0] <= num_of_freq[2])
        req = 2412;
    else if (num_of_freq[1] <= num_of_freq[0] && num_of_freq[1] <= num_of_freq[2])
        req = 2437;
    else
        req = 2462;

    return req;
}

struct wpa_bss * wpa_supplicant_pick_network(struct wpa_supplicant *wpa_s,
					     struct wpa_ssid **selected_ssid)
{
	struct wpa_bss *selected = NULL;
	int prio;
	//struct wpa_ssid *next_ssid = NULL;
	//struct wpa_ssid *ssid;

	if (wpa_s->last_scan_res == NULL ||
	    wpa_s->last_scan_res_used == 0)
		return NULL; /* no scan results from last update */

#if 0
	if (wpa_s->next_ssid) {
		/* check that next_ssid is still valid */
		for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
			if (ssid == wpa_s->next_ssid)
				break;
		}
		next_ssid = ssid;
		wpa_s->next_ssid = NULL;
	}
#endif 
	while (selected == NULL) {
		for (prio = 0; prio < wpa_s->conf->num_prio; prio++) {
#if 0            
			if (next_ssid && next_ssid->priority ==
			    wpa_s->conf->pssid[prio]->priority) {
				selected = wpa_supplicant_select_bss(
					wpa_s, next_ssid, selected_ssid, 1);
				if (selected)
					break;
			}
#endif            
			selected = wpa_supplicant_select_bss(
				wpa_s, wpa_s->conf->pssid[prio],
				selected_ssid, 0);
			if (selected)
				break;
		}

        if (selected == NULL)
			break;
	}

	//ssid = *selected_ssid;
#if 0    
	if (selected && ssid && !ssid->psk_set &&
	    !ssid->passphrase/* && !ssid->ext_psk*/) {
		const char *field_name, *txt = NULL;

		wpa_dbg(wpa_s, MSG_DEBUG,
			"PSK/passphrase not yet available for the selected network");

		//wpas_notify_network_request(wpa_s, ssid,
		//			    WPA_CTRL_REQ_PSK_PASSPHRASE, NULL);

		field_name = wpa_supplicant_ctrl_req_to_string(
			WPA_CTRL_REQ_PSK_PASSPHRASE, NULL, &txt);
		if (field_name == NULL)
			return NULL;

		wpas_send_ctrl_req(wpa_s, ssid, field_name, txt);

		selected = NULL;
	}
#endif
	return selected;
}


static void wpa_supplicant_req_new_scan(struct wpa_supplicant *wpa_s,
					int timeout_sec, int timeout_usec)
{
	if (!wpa_supplicant_enabled_networks(wpa_s)) {
		/*
		 * No networks are enabled; short-circuit request so
		 * we don't wait timeout seconds before transitioning
		 * to INACTIVE state.
		 */
		wpa_dbg(wpa_s, MSG_DEBUG, "Short-circuit new scan request "
			"since there are no enabled networks");
		wpa_supplicant_set_state(wpa_s, WPA_INACTIVE);
		return;
	}

	wpa_s->scan_for_connection = 1;
	wpa_supplicant_req_scan(wpa_s, timeout_sec, timeout_usec);
}


int wpa_supplicant_connect(struct wpa_supplicant *wpa_s,
			   struct wpa_bss *selected,
			   struct wpa_ssid *ssid)
{
#if 0
	if (wpas_wps_scan_pbc_overlap(wpa_s, selected, ssid)) {
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_OVERLAP
			"PBC session overlap");
		wpas_notify_wps_event_pbc_overlap(wpa_s);
#ifdef CONFIG_P2P
		if (wpa_s->p2p_group_interface == P2P_GROUP_INTERFACE_CLIENT ||
		    wpa_s->p2p_in_provisioning) {
			eloop_register_timeout(0, 0, wpas_p2p_pbc_overlap_cb,
					       wpa_s, NULL);
			return -1;
		}
#endif /* CONFIG_P2P */

#ifdef CONFIG_WPS
		wpas_wps_pbc_overlap(wpa_s);
		wpas_wps_cancel(wpa_s);
#endif /* CONFIG_WPS */
		return -1;
	}
#endif
	wpa_msg(wpa_s, MSG_DEBUG,
		"Considering connect request: reassociate: %d  selected: "
		MACSTR "  bssid: " MACSTR "  pending: " MACSTR
		"  wpa_state: %d  ssid=%p  current_ssid=%p",
		0/*wpa_s->reassociate*/, MAC2STR(selected->bssid),
		MAC2STR(wpa_s->bssid), MAC2STR(wpa_s->pending_bssid),
		wpa_s->wpa_state,
		ssid, wpa_s->current_ssid);

	/*
	 * Do not trigger new association unless the BSSID has changed or if
	 * reassociation is requested. If we are in process of associating with
	 * the selected BSSID, do not trigger new attempt.
	 */
	if (/*wpa_s->reassociate ||*/
	    (os_memcmp(selected->bssid, wpa_s->bssid, ETH_ALEN) != 0 &&
	     ((wpa_s->wpa_state != WPA_ASSOCIATING &&
	       wpa_s->wpa_state != WPA_AUTHENTICATING) ||
	      (!is_zero_ether_addr(wpa_s->pending_bssid) &&
	       os_memcmp(selected->bssid, wpa_s->pending_bssid, ETH_ALEN) !=
	       0) ||
	      (is_zero_ether_addr(wpa_s->pending_bssid) &&
	       ssid != wpa_s->current_ssid)))) {
		//if (wpa_supplicant_scard_init(wpa_s, ssid)) {
			//wpa_supplicant_req_new_scan(wpa_s, 10, 0);
			//return 0;
		//}
		wpa_msg(wpa_s, MSG_DEBUG, "Request association with " MACSTR,
			MAC2STR(selected->bssid));
		wpa_supplicant_associate(wpa_s, selected, ssid);
	} else {
		wpa_dbg(wpa_s, MSG_DEBUG, "Already associated or trying to "
			"connect with the selected AP");
	}

	return 0;
}


static struct wpa_ssid *
wpa_supplicant_pick_new_network(struct wpa_supplicant *wpa_s)
{
	int prio;
	struct wpa_ssid *ssid;

	for (prio = 0; prio < wpa_s->conf->num_prio; prio++) {
		for (ssid = wpa_s->conf->pssid[prio]; ssid; ssid = ssid->pnext)
		{
			if (wpas_network_disabled(wpa_s, ssid))
				continue;
			if (ssid->mode == IEEE80211_MODE_IBSS ||
			    ssid->mode == IEEE80211_MODE_AP ||
			    ssid->mode == IEEE80211_MODE_MESH)
				return ssid;
		}
	}
	return NULL;
}


/* TODO: move the rsn_preauth_scan_result*() to be called from notify.c based
 * on BSS added and BSS changed events */
#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpa_supplicant_rsn_preauth_scan_results(
	struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		const u8 *ssid, *rsn;

		ssid = wpa_bss_get_ie(bss, WLAN_EID_SSID);
		if (ssid == NULL)
			continue;

		rsn = wpa_bss_get_ie(bss, WLAN_EID_RSN);
		if (rsn == NULL)
			continue;
	}

}


static int wpa_supplicant_need_to_roam(struct wpa_supplicant *wpa_s,
				       struct wpa_bss *selected,
				       struct wpa_ssid *ssid)
{
	struct wpa_bss *current_bss = NULL;
#ifndef CONFIG_NO_ROAMING
	int min_diff;
	int to_5ghz;
#endif /* CONFIG_NO_ROAMING */

	//if (wpa_s->reassociate)
	//	return 1; /* explicit request to reassociate */
	if (wpa_s->wpa_state < WPA_ASSOCIATED)
		return 1; /* we are not associated; continue */
	if (wpa_s->current_ssid == NULL)
		return 1; /* unknown current SSID */
	if (wpa_s->current_ssid != ssid)
		return 1; /* different network block */

	if (wpas_driver_bss_selection(wpa_s))
		return 0; /* Driver-based roaming */

	if (wpa_s->current_ssid->ssid)
		current_bss = wpa_bss_get(wpa_s, wpa_s->bssid,
					  wpa_s->current_ssid->ssid,
					  wpa_s->current_ssid->ssid_len);
	if (!current_bss)
		current_bss = wpa_bss_get_bssid(wpa_s, wpa_s->bssid);

	if (!current_bss)
		return 1; /* current BSS not seen in scan results */

	if (current_bss == selected)
		return 0;

	if (selected->last_update_idx > current_bss->last_update_idx)
		return 1; /* current BSS not seen in the last scan */

#ifndef CONFIG_NO_ROAMING
	wpa_dbg(wpa_s, MSG_DEBUG, "Considering within-ESS reassociation");
	wpa_dbg(wpa_s, MSG_DEBUG, "Current BSS: " MACSTR
		" level=%d snr=%d est_throughput=%u",
		MAC2STR(current_bss->bssid), current_bss->level,
		current_bss->snr, current_bss->est_throughput);
	wpa_dbg(wpa_s, MSG_DEBUG, "Selected BSS: " MACSTR
		" level=%d snr=%d est_throughput=%u",
		MAC2STR(selected->bssid), selected->level,
		selected->snr, selected->est_throughput);

	if (wpa_s->current_ssid->bssid_set &&
	    os_memcmp(selected->bssid, wpa_s->current_ssid->bssid, ETH_ALEN) ==
	    0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Allow reassociation - selected BSS "
			"has preferred BSSID");
		return 1;
	}

	if (selected->est_throughput > current_bss->est_throughput + 5000) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"Allow reassociation - selected BSS has better estimated throughput");
		return 1;
	}

	to_5ghz = selected->freq > 4000 && current_bss->freq < 4000;

	if (current_bss->level < 0 &&
	    current_bss->level > selected->level + to_5ghz * 2) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Skip roam - Current BSS has better "
			"signal level");
		return 0;
	}

	min_diff = 2;
	if (current_bss->level < 0) {
		if (current_bss->level < -85)
			min_diff = 1;
		else if (current_bss->level < -80)
			min_diff = 2;
		else if (current_bss->level < -75)
			min_diff = 3;
		else if (current_bss->level < -70)
			min_diff = 4;
		else
			min_diff = 5;
	}
	if (to_5ghz) {
		/* Make it easier to move to 5 GHz band */
		if (min_diff > 2)
			min_diff -= 2;
		else
			min_diff = 0;
	}
	if (abs(current_bss->level - selected->level) < min_diff) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Skip roam - too small difference "
			"in signal level");
		return 0;
	}

	return 1;
#else /* CONFIG_NO_ROAMING */
	return 0;
#endif /* CONFIG_NO_ROAMING */
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


/* Return != 0 if no scan results could be fetched or if scan results should not
 * be shared with other virtual interfaces. */
static int _wpa_supplicant_event_scan_results(struct wpa_supplicant *wpa_s,
					      union wpa_event_data *data,
					      int own_request)
{
	struct wpa_scan_results *scan_res = NULL;
	int ret = 0;
	int ap = 0;
#ifndef CONFIG_NO_RANDOM_POOL
	size_t i, num;
#endif /* CONFIG_NO_RANDOM_POOL */

#ifdef CONFIG_AP
	if (wpa_s->ap_iface)
		ap = 1;
#endif /* CONFIG_AP */

	wpa_supplicant_notify_scanning(wpa_s, 0);

	scan_res = wpa_supplicant_get_scan_results(wpa_s,
						   data ? &data->scan_info :
						   NULL, 1);
	if (scan_res == NULL) {
		if (wpa_s->conf->ap_scan == 2 || ap)
			return -1;
		if (!own_request)
			return -1;
		//if (data && data->scan_info.external_scan)
		//	return -1;
		wpa_dbg(wpa_s, MSG_DEBUG, "Failed to get scan results - try "
			"scanning again");
		wpa_supplicant_req_new_scan(wpa_s, 1, 0);

		wpas_notify_scan_done(wpa_s, 1);

		if(wpa_s->last_scan_req == NORMAL_SCAN_REQ) {
			wpa_update_disconnect_reason(WLAN_REASON_AP_NOT_FOUND);
		}
   
		ret = -1;
		goto scan_work_done;
	}

#ifndef CONFIG_NO_RANDOM_POOL
	num = scan_res->num;
	if (num > 10)
		num = 10;
	for (i = 0; i < num; i++) {
		u8 buf[5];
		struct wpa_scan_res *res = scan_res->res[i];
		buf[0] = res->bssid[5];
		buf[1] = res->qual & 0xff;
		buf[2] = res->noise & 0xff;
		buf[3] = res->level & 0xff;
		buf[4] = res->tsf & 0xff;
		random_add_randomness(buf, sizeof(buf));
	}
#endif /* CONFIG_NO_RANDOM_POOL */

	if (ap) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Ignore scan results in AP mode");
#ifdef CONFIG_AP
		//if (wpa_s->ap_iface->scan_cb)
			//wpa_s->ap_iface->scan_cb(wpa_s->ap_iface);
#endif /* CONFIG_AP */
		goto scan_work_done;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "New scan results available (own=%u ext=%u)",
		wpa_s->own_scan_running,
		data ? data->scan_info.external_scan : 0);
	/*if (wpa_s->last_scan_req == MANUAL_SCAN_REQ &&
	    wpa_s->manual_scan_use_id && wpa_s->own_scan_running &&
	    own_request && !(data && data->scan_info.external_scan)) {
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPA_EVENT_SCAN_RESULTS "id=%u",
			     wpa_s->manual_scan_id);
		wpa_s->manual_scan_use_id = 0;
	} else {
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPA_EVENT_SCAN_RESULTS);
	}*/
	//wpas_notify_scan_results(wpa_s);

	wpas_notify_scan_done(wpa_s, 1);

	/*if (data && data->scan_info.external_scan) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Do not use results from externally requested scan operation for network selection");
		wpa_scan_results_free(scan_res);
		return 0;
	}*/

	if (wpa_s->disconnected) {
		wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
		goto scan_work_done;
	}

	if (wpa_s->wpa_state >= WPA_AUTHENTICATING &&
	    wpa_s->wpa_state < WPA_COMPLETED)
		goto scan_work_done;

	wpa_scan_results_free(scan_res);

	if (own_request && wpa_s->scan_work) {
		struct wpa_radio_work *work = wpa_s->scan_work;
		wpa_s->scan_work = NULL;
		radio_work_done(work);
	}

    if (wpa_s->last_scan_req == MANUAL_SCAN_REQ) {
        if (wpa_supplicant_enabled_networks(wpa_s)) {
            if (wpa_s->wpa_state == WPA_SCANNING) {
                wpa_s->scan_for_connection = 1;
                wpa_supplicant_req_scan(wpa_s, 1, 0);
            }
        } else {
            wpa_supplicant_set_state(wpa_s, WPA_INACTIVE);
        }
        return 0;
    }

	return wpas_select_network_from_last_scan(wpa_s, 1, own_request);

scan_work_done:
	wpa_scan_results_free(scan_res);
	if (own_request && wpa_s->scan_work) {
		struct wpa_radio_work *work = wpa_s->scan_work;
		wpa_s->scan_work = NULL;
		radio_work_done(work);
	}
	return ret;
}


static int wpas_select_network_from_last_scan(struct wpa_supplicant *wpa_s,
					      int new_scan, int own_request)
{
	struct wpa_bss *selected;
	struct wpa_ssid *ssid = NULL;
#if 0    
	int time_to_reenable = wpas_reenabled_network_time(wpa_s);

	if (time_to_reenable > 0) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"Postpone network selection by %d seconds since all networks are disabled",
			time_to_reenable);
		eloop_cancel_timeout(wpas_network_reenabled, wpa_s, NULL);
		eloop_register_timeout(time_to_reenable, 0,
				       wpas_network_reenabled, wpa_s, NULL);
		return 0;
	}
#endif
	//if (wpa_s->p2p_mgmt)
	//	return 0; /* no normal connection on p2p_mgmt interface */
	selected = wpa_supplicant_pick_network(wpa_s, &ssid);

#ifdef CONFIG_MESH
	if (wpa_s->ifmsh) {
		wpa_msg(wpa_s, MSG_INFO,
			"Avoiding join because we already joined a mesh group");
		return 0;
	}
#endif /* CONFIG_MESH */

	if (selected) {
		//int skip;
		//skip = !wpa_supplicant_need_to_roam(wpa_s, selected, ssid);
		//if (skip) {
			//if (new_scan)
				//wpa_supplicant_rsn_preauth_scan_results(wpa_s);
			//return 0;
		//}

		if (ssid != wpa_s->current_ssid &&
		    wpa_s->wpa_state >= WPA_AUTHENTICATING) {
			wpa_s->own_disconnect_req = 1;
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_DEAUTH_LEAVING);
		}
		
		extern unsigned char nv_bw40_enable;
		if (nv_bw40_enable) {
			ssid->disable_ht40 = 0;
		}

        /*extern int wifi_update_nv_sta_start(char *ssid, char *pwd, uint8_t *pmk);
        if (!(ssid->wep_key[0] && ssid->wep_key[0][0])) {
            wifi_update_nv_sta_start((char *)ssid->ssid, ssid->passphrase, ssid->psk);
        }
        else {
            wifi_update_nv_sta_start((char *)ssid->ssid, (char *)ssid->wep_key[0], ssid->psk);
        }*/

		if (wpa_supplicant_connect(wpa_s, selected, ssid) < 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "Connect failed");
			return -1;
		}
		//if (new_scan)
			//wpa_supplicant_rsn_preauth_scan_results(wpa_s);
		/*
		 * Do not notify other virtual radios of scan results since we do not
		 * want them to start other associations at the same time.
		 */
		return 1;
	} else {
		wpa_dbg(wpa_s, MSG_DEBUG, "No suitable network found");
		ssid = wpa_supplicant_pick_new_network(wpa_s);
		if (ssid) {
            if (ssid->mode == IEEE80211_MODE_AP && 0 == ssid->frequency) {
                ssid->frequency = ap_channel_auto_select(wpa_s);
            }
			wpa_dbg(wpa_s, MSG_DEBUG, "Setup a new network");
			wpa_supplicant_associate(wpa_s, NULL, ssid);
			//if (new_scan)
				//wpa_supplicant_rsn_preauth_scan_results(wpa_s);
		} else if (own_request) {
			/*
			 * No SSID found. If SCAN results are as a result of
			 * own scan request and not due to a scan request on
			 * another shared interface, try another scan.
			 */
			int timeout_sec = wpa_s->normal_scans < 3 ? 0 : wpa_s->scan_interval;
			int timeout_usec = 0;
#ifdef CONFIG_WPS
			if (wpa_s->after_wps > 0 || wpas_wps_searching(wpa_s)) {
				wpa_dbg(wpa_s, MSG_DEBUG, "Use shorter wait during WPS processing");
				timeout_sec = 0;
				timeout_usec = 500000;
				wpa_supplicant_req_new_scan(wpa_s, timeout_sec,
							    timeout_usec);
				return 0;
			}
#endif /* CONFIG_WPS */
#if 0
			if (wpa_supplicant_req_sched_scan(wpa_s))
#endif                
            if (new_scan) {
				wpa_supplicant_req_new_scan(wpa_s, timeout_sec,
							    timeout_usec);
            }
			wpa_msg_ctrl(wpa_s, MSG_INFO,
				     WPA_EVENT_NETWORK_NOT_FOUND);
		}
	}
	return 0;
}


static int wpa_supplicant_event_scan_results(struct wpa_supplicant *wpa_s,
					     union wpa_event_data *data)
{
	//struct wpa_supplicant *ifs;
	int res;

	res = _wpa_supplicant_event_scan_results(wpa_s, data, 1);
	if (res == 2) {
		/*
		 * Interface may have been removed, so must not dereference
		 * wpa_s after this.
		 */
		return 1;
	}
	if (res != 0) {
		/*
		 * If no scan results could be fetched, then no need to
		 * notify those interfaces that did not actually request
		 * this scan. Similarly, if scan results started a new operation on this
		 * interface, do not notify other interfaces to avoid concurrent
		 * operations during a connection attempt.
		 */
		return 0;
	}

	/*
	 * Check other interfaces to see if they share the same radio. If
	 * so, they get updated with this same scan info.
	 */
	#if 0 //sw modify this, not need, this will trigger bogus scan events.
	dl_list_for_each(ifs, &wpa_s->radio->ifaces, struct wpa_supplicant,
			 radio_list) {
		if (ifs != wpa_s) {
			wpa_printf(MSG_DEBUG, "%s: Updating scan results from "
				   "sibling", ifs->ifname);
			_wpa_supplicant_event_scan_results(ifs, data, 0);
		}
	}
    #endif

	return 0;
}

#endif /* CONFIG_NO_SCAN_PROCESSING */


int wpa_supplicant_fast_associate(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_NO_SCAN_PROCESSING
	return -1;
#else /* CONFIG_NO_SCAN_PROCESSING */
#ifdef CONFIG_AP
    struct wpa_ssid *ssid = NULL;
    
    ssid = wpa_supplicant_pick_new_network(wpa_s);
    if (ssid) {
        if (ssid->mode == IEEE80211_MODE_AP && 0 != ssid->frequency){
            wpa_dbg(wpa_s, MSG_DEBUG, "Setup a new network");
            wpa_supplicant_associate(wpa_s, NULL, ssid);

            wpa_s->scan_req = NORMAL_SCAN_REQ;
            
            return 1;
        }
    }
#endif

	struct os_reltime now;

	if (wpa_s->last_scan_res_used == 0)
		return -1;

	os_get_reltime(&now);
	if (os_reltime_expired(&now, &wpa_s->last_scan, 5)) {
		wpa_printf(MSG_DEBUG, "Fast associate: Old scan results");
		return -1;
	}

	return wpas_select_network_from_last_scan(wpa_s, 0, 1);
#endif /* CONFIG_NO_SCAN_PROCESSING */
}

#ifdef CONFIG_WNM

static void wnm_bss_keep_alive(void *eloop_ctx, void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->wpa_state < WPA_ASSOCIATED)
		return;

	if (!wpa_s->no_keep_alive) {
		wpa_printf(MSG_DEBUG, "WNM: Send keep-alive to AP " MACSTR,
			   MAC2STR(wpa_s->bssid));
		/* TODO: could skip this if normal data traffic has been sent */
		/* TODO: Consider using some more appropriate data frame for
		 * this */
		if (wpa_s->l2)
			l2_packet_send(wpa_s->l2, wpa_s->bssid, 0x0800,
				       (u8 *) "", 0);
	}

#ifdef CONFIG_SME
	if (wpa_s->sme.bss_max_idle_period) {
		unsigned int msec;
		msec = wpa_s->sme.bss_max_idle_period * 1024; /* times 1000 */
		if (msec > 100)
			msec -= 100;
		eloop_register_timeout(msec / 1000, msec % 1000 * 1000,
				       wnm_bss_keep_alive, wpa_s, NULL);
	}
#endif /* CONFIG_SME */
}


static void wnm_process_assoc_resp(struct wpa_supplicant *wpa_s,
				   const u8 *ies, size_t ies_len)
{
	struct ieee802_11_elems elems;

	if (ies == NULL)
		return;

	if (ieee802_11_parse_elems(ies, ies_len, &elems, 1) == ParseFailed)
		return;

#ifdef CONFIG_SME
	if (elems.bss_max_idle_period) {
		unsigned int msec;
		wpa_s->sme.bss_max_idle_period =
			WPA_GET_LE16(elems.bss_max_idle_period);
		wpa_printf(MSG_DEBUG, "WNM: BSS Max Idle Period: %u (* 1000 "
			   "TU)%s", wpa_s->sme.bss_max_idle_period,
			   (elems.bss_max_idle_period[2] & 0x01) ?
			   " (protected keep-live required)" : "");
		if (wpa_s->sme.bss_max_idle_period == 0)
			wpa_s->sme.bss_max_idle_period = 1;
		if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME) {
			eloop_cancel_timeout(wnm_bss_keep_alive, wpa_s, NULL);
			 /* msec times 1000 */
			msec = wpa_s->sme.bss_max_idle_period * 1024;
			if (msec > 100)
				msec -= 100;
			eloop_register_timeout(msec / 1000, msec % 1000 * 1000,
					       wnm_bss_keep_alive, wpa_s,
					       NULL);
		}
	}
#endif /* CONFIG_SME */
}

#endif /* CONFIG_WNM */


void wnm_bss_keep_alive_deinit(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_WNM
	eloop_cancel_timeout(wnm_bss_keep_alive, wpa_s, NULL);
#endif /* CONFIG_WNM */
}


#ifdef CONFIG_INTERWORKING

static int wpas_qos_map_set(struct wpa_supplicant *wpa_s, const u8 *qos_map,
			    size_t len)
{
	int res;

	wpa_hexdump(MSG_DEBUG, "Interworking: QoS Map Set", qos_map, len);
	res = wpa_drv_set_qos_map(wpa_s, qos_map, len);
	if (res) {
		wpa_printf(MSG_DEBUG, "Interworking: Failed to configure QoS Map Set to the driver");
	}

	return res;
}


static void interworking_process_assoc_resp(struct wpa_supplicant *wpa_s,
					    const u8 *ies, size_t ies_len)
{
	struct ieee802_11_elems elems;

	if (ies == NULL)
		return;

	if (ieee802_11_parse_elems(ies, ies_len, &elems, 1) == ParseFailed)
		return;

	if (elems.qos_map_set) {
		wpas_qos_map_set(wpa_s, elems.qos_map_set,
				 elems.qos_map_set_len);
	}
}

#endif /* CONFIG_INTERWORKING */


#ifdef CONFIG_FST
static int wpas_fst_update_mbie(struct wpa_supplicant *wpa_s,
				const u8 *ie, size_t ie_len)
{
	struct mb_ies_info mb_ies;

	if (!ie || !ie_len || !wpa_s->fst)
	    return -ENOENT;

	os_memset(&mb_ies, 0, sizeof(mb_ies));

	while (ie_len >= 2 && mb_ies.nof_ies < MAX_NOF_MB_IES_SUPPORTED) {
		size_t len;

		len = 2 + ie[1];
		if (len > ie_len) {
			wpa_hexdump(MSG_DEBUG, "FST: Truncated IE found",
				    ie, ie_len);
			break;
		}

		if (ie[0] == WLAN_EID_MULTI_BAND) {
			wpa_printf(MSG_DEBUG, "MB IE of %u bytes found",
				   (unsigned int) len);
			mb_ies.ies[mb_ies.nof_ies].ie = ie + 2;
			mb_ies.ies[mb_ies.nof_ies].ie_len = len - 2;
			mb_ies.nof_ies++;
		}

		ie_len -= len;
		ie += len;
	}

	if (mb_ies.nof_ies > 0) {
		wpabuf_free(wpa_s->received_mb_ies);
		wpa_s->received_mb_ies = mb_ies_by_info(&mb_ies);
		return 0;
	}

	return -ENOENT;
}
#endif /* CONFIG_FST */


static int wpa_supplicant_event_associnfo(struct wpa_supplicant *wpa_s,
					  union wpa_event_data *data)
{
	int l, len, found = 0, wpa_found, rsn_found;
	const u8 *p;
#ifdef CONFIG_IEEE80211R
	u8 bssid[ETH_ALEN];
#endif /* CONFIG_IEEE80211R */

	wpa_dbg(wpa_s, MSG_DEBUG, "Association info event");
	if (data->assoc_info.req_ies)
		wpa_hexdump(MSG_DEBUG, "req_ies", data->assoc_info.req_ies,
			    data->assoc_info.req_ies_len);
	if (data->assoc_info.resp_ies) {
		wpa_hexdump(MSG_DEBUG, "resp_ies", data->assoc_info.resp_ies,
			    data->assoc_info.resp_ies_len);
#ifdef CONFIG_TDLS
		wpa_tdls_assoc_resp_ies(wpa_s->wpa, data->assoc_info.resp_ies,
					data->assoc_info.resp_ies_len);
#endif /* CONFIG_TDLS */
#ifdef CONFIG_WNM
		wnm_process_assoc_resp(wpa_s, data->assoc_info.resp_ies,
				       data->assoc_info.resp_ies_len);
#endif /* CONFIG_WNM */
#ifdef CONFIG_INTERWORKING
		interworking_process_assoc_resp(wpa_s, data->assoc_info.resp_ies,
						data->assoc_info.resp_ies_len);
#endif /* CONFIG_INTERWORKING */
	}
	if (data->assoc_info.beacon_ies)
		wpa_hexdump(MSG_DEBUG, "beacon_ies",
			    data->assoc_info.beacon_ies,
			    data->assoc_info.beacon_ies_len);
	if (data->assoc_info.freq)
		wpa_dbg(wpa_s, MSG_DEBUG, "freq=%u MHz",
			data->assoc_info.freq);

	p = data->assoc_info.req_ies;
	l = data->assoc_info.req_ies_len;

	/* Go through the IEs and make a copy of the WPA/RSN IE, if present. */
	while (p && l >= 2) {
		len = p[1] + 2;
		if (len > l) {
			wpa_hexdump(MSG_DEBUG, "Truncated IE in assoc_info",
				    p, l);
			break;
		}
		if ((p[0] == WLAN_EID_VENDOR_SPECIFIC && p[1] >= 6 &&
		     (os_memcmp(&p[2], "\x00\x50\xF2\x01\x01\x00", 6) == 0)) ||
		    (p[0] == WLAN_EID_VENDOR_SPECIFIC && p[1] >= 4 &&
		     (os_memcmp(&p[2], "\x50\x6F\x9A\x12", 4) == 0)) ||
		    (p[0] == WLAN_EID_RSN && p[1] >= 2)) {
			if (wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, p, len))
				break;
			found = 1;
			wpa_find_assoc_pmkid(wpa_s);
			break;
		}
		l -= len;
		p += len;
	}
	if (!found && data->assoc_info.req_ies)
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);

#ifdef CONFIG_IEEE80211R
#ifdef CONFIG_SME
	if (wpa_s->sme.auth_alg == WPA_AUTH_ALG_FT) {
		if (wpa_drv_get_bssid(wpa_s, bssid) < 0 ||
		    wpa_ft_validate_reassoc_resp(wpa_s->wpa,
						 data->assoc_info.resp_ies,
						 data->assoc_info.resp_ies_len,
						 bssid) < 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "FT: Validation of "
				"Reassociation Response failed");
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_INVALID_IE);
			return -1;
		}
	}

	p = data->assoc_info.resp_ies;
	l = data->assoc_info.resp_ies_len;

#ifdef CONFIG_WPS_STRICT
	if (p && wpa_s->current_ssid &&
	    wpa_s->current_ssid->key_mgmt == WPA_KEY_MGMT_WPS) {
		struct wpabuf *wps;
		wps = ieee802_11_vendor_ie_concat(p, l, WPS_IE_VENDOR_TYPE);
		if (wps == NULL) {
			wpa_msg(wpa_s, MSG_INFO, "WPS-STRICT: AP did not "
				"include WPS IE in (Re)Association Response");
			return -1;
		}

		if (wps_validate_assoc_resp(wps) < 0) {
			wpabuf_free(wps);
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_INVALID_IE);
			return -1;
		}
		wpabuf_free(wps);
	}
#endif /* CONFIG_WPS_STRICT */

	/* Go through the IEs and make a copy of the MDIE, if present. */
	while (p && l >= 2) {
		len = p[1] + 2;
		if (len > l) {
			wpa_hexdump(MSG_DEBUG, "Truncated IE in assoc_info",
				    p, l);
			break;
		}
		if (p[0] == WLAN_EID_MOBILITY_DOMAIN &&
		    p[1] >= MOBILITY_DOMAIN_ID_LEN) {
			wpa_s->sme.ft_used = 1;
			os_memcpy(wpa_s->sme.mobility_domain, p + 2,
				  MOBILITY_DOMAIN_ID_LEN);
			break;
		}
		l -= len;
		p += len;
	}
#endif /* CONFIG_SME */

	/* Process FT when SME is in the driver */
	if (!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME) &&
	    wpa_ft_is_completed(wpa_s->wpa)) {
		if (wpa_drv_get_bssid(wpa_s, bssid) < 0 ||
		    wpa_ft_validate_reassoc_resp(wpa_s->wpa,
						 data->assoc_info.resp_ies,
						 data->assoc_info.resp_ies_len,
						 bssid) < 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "FT: Validation of "
				"Reassociation Response failed");
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_INVALID_IE);
			return -1;
		}
		wpa_dbg(wpa_s, MSG_DEBUG, "FT: Reassociation Response done");
	}

	wpa_sm_set_ft_params(wpa_s->wpa, data->assoc_info.resp_ies,
			     data->assoc_info.resp_ies_len);
#endif /* CONFIG_IEEE80211R */

	/* WPA/RSN IE from Beacon/ProbeResp */
	p = data->assoc_info.beacon_ies;
	l = data->assoc_info.beacon_ies_len;

	/* Go through the IEs and make a copy of the WPA/RSN IEs, if present.
	 */
	wpa_found = rsn_found = 0;
	while (p && l >= 2) {
		len = p[1] + 2;
		if (len > l) {
			wpa_hexdump(MSG_DEBUG, "Truncated IE in beacon_ies",
				    p, l);
			break;
		}
		if (!wpa_found &&
		    p[0] == WLAN_EID_VENDOR_SPECIFIC && p[1] >= 6 &&
		    os_memcmp(&p[2], "\x00\x50\xF2\x01\x01\x00", 6) == 0) {
			wpa_found = 1;
			wpa_sm_set_ap_wpa_ie(wpa_s->wpa, p, len);
		}

		if (!rsn_found &&
		    p[0] == WLAN_EID_RSN && p[1] >= 2) {
			rsn_found = 1;
			wpa_sm_set_ap_rsn_ie(wpa_s->wpa, p, len);
		}

		l -= len;
		p += len;
	}

	if (!wpa_found && data->assoc_info.beacon_ies)
		wpa_sm_set_ap_wpa_ie(wpa_s->wpa, NULL, 0);
	if (!rsn_found && data->assoc_info.beacon_ies)
		wpa_sm_set_ap_rsn_ie(wpa_s->wpa, NULL, 0);
	if (wpa_found || rsn_found)
		wpa_s->ap_ies_from_associnfo = 1;

	if (wpa_s->assoc_freq && data->assoc_info.freq &&
	    wpa_s->assoc_freq != data->assoc_info.freq) {
		wpa_printf(MSG_DEBUG, "Operating frequency changed from "
			   "%u to %u MHz",
			   wpa_s->assoc_freq, data->assoc_info.freq);
		wpa_supplicant_update_scan_results(wpa_s);
	}

	wpa_s->assoc_freq = data->assoc_info.freq;

	return 0;
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static int wpa_supplicant_assoc_update_ie(struct wpa_supplicant *wpa_s)
{
	const u8 *bss_wpa = NULL, *bss_rsn = NULL;

	if (!wpa_s->current_bss || !wpa_s->current_ssid)
		return -1;

	if (!wpa_key_mgmt_wpa_any(wpa_s->current_ssid->key_mgmt))
		return 0;

	bss_wpa = wpa_bss_get_vendor_ie(wpa_s->current_bss,
					WPA_IE_VENDOR_TYPE);
	bss_rsn = wpa_bss_get_ie(wpa_s->current_bss, WLAN_EID_RSN);

	if (wpa_sm_set_ap_wpa_ie(wpa_s->wpa, bss_wpa,
				 bss_wpa ? 2 + bss_wpa[1] : 0) ||
	    wpa_sm_set_ap_rsn_ie(wpa_s->wpa, bss_rsn,
				 bss_rsn ? 2 + bss_rsn[1] : 0))
		return -1;

	return 0;
}


static void wpas_fst_update_mb_assoc(struct wpa_supplicant *wpa_s,
				     union wpa_event_data *data)
{
#ifdef CONFIG_FST
	struct assoc_info *ai = data ? &data->assoc_info : NULL;
	struct wpa_bss *bss = wpa_s->current_bss;
	const u8 *ieprb, *iebcn;

	wpabuf_free(wpa_s->received_mb_ies);
	wpa_s->received_mb_ies = NULL;

	if (ai &&
	    !wpas_fst_update_mbie(wpa_s, ai->resp_ies, ai->resp_ies_len)) {
		wpa_printf(MSG_DEBUG,
			   "FST: MB IEs updated from Association Response frame");
		return;
	}

	if (ai &&
	    !wpas_fst_update_mbie(wpa_s, ai->beacon_ies, ai->beacon_ies_len)) {
		wpa_printf(MSG_DEBUG,
			   "FST: MB IEs updated from association event Beacon IEs");
		return;
	}

	if (!bss)
		return;

	ieprb = (const u8 *) (bss + 1);
	iebcn = ieprb + bss->ie_len;

	if (!wpas_fst_update_mbie(wpa_s, ieprb, bss->ie_len))
		wpa_printf(MSG_DEBUG, "FST: MB IEs updated from bss IE");
	else if (!wpas_fst_update_mbie(wpa_s, iebcn, bss->beacon_ie_len))
		wpa_printf(MSG_DEBUG, "FST: MB IEs updated from bss beacon IE");
#endif /* CONFIG_FST */
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


static void wpa_supplicant_event_assoc(struct wpa_supplicant *wpa_s,
				       union wpa_event_data *data)
{
	u8 bssid[ETH_ALEN];
	//int already_authorized;
	//int new_bss = 0;

#if 0
#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		if (!data)
			return;
		hostapd_notif_assoc(wpa_s->ap_iface->bss[0],
				    data->assoc_info.addr,
				    data->assoc_info.req_ies,
				    data->assoc_info.req_ies_len,
				    data->assoc_info.reassoc);
		return;
	}
#endif /* CONFIG_AP */
#endif

	eloop_cancel_timeout(wpas_network_reenabled, wpa_s, NULL);

	if (data && wpa_supplicant_event_associnfo(wpa_s, data) < 0)
		return;

	if (wpa_drv_get_bssid(wpa_s, bssid) < 0) {
        #ifndef CONFIG_ECR6600_DBGTRIM
		wpa_dbg(wpa_s, MSG_ERROR, "Failed to get BSSID");
		#else
		wpa_dbg_error(wpa_s, MSG_ERROR, "Failed to get BSSID");
		#endif
		wpa_supplicant_deauthenticate(
			wpa_s, WLAN_REASON_DEAUTH_LEAVING);
		return;
	}

	wpa_supplicant_set_state(wpa_s, WPA_ASSOCIATED);
    
	if (os_memcmp(bssid, wpa_s->bssid, ETH_ALEN) != 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Associated to a new BSS: BSSID="
			MACSTR, MAC2STR(bssid));
		//new_bss = 1;
		random_add_randomness(bssid, ETH_ALEN);
		os_memcpy(wpa_s->bssid, bssid, ETH_ALEN);
		os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
		//wpas_notify_bssid_changed(wpa_s);

		if (wpa_supplicant_dynamic_keys(wpa_s)) {
			wpa_clear_keys(wpa_s, bssid);
		}
		if (wpa_supplicant_select_config(wpa_s) < 0) {
			wpa_supplicant_deauthenticate(
				wpa_s, WLAN_REASON_DEAUTH_LEAVING);
			return;
		}
	}

	wpa_msg(wpa_s, MSG_INFO, "Associated with " MACSTR, MAC2STR(bssid));
	wpa_sm_notify_assoc(wpa_s->wpa, bssid);
	if (wpa_s->l2)
		l2_packet_notify_auth_start(wpa_s->l2);

	//already_authorized = data && data->assoc_info.authorized;

	wpa_s->eapol_received = 0;
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE ||
	    (wpa_s->current_ssid &&
	     wpa_s->current_ssid->mode == IEEE80211_MODE_IBSS)) {
		if (wpa_s->current_ssid &&
		    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE &&
		    (wpa_s->drv_flags &
		     WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC_DONE)) {
			/*
			 * Set the key after having received joined-IBSS event
			 * from the driver.
			 */
			wpa_supplicant_set_wpa_none_key(wpa_s,
							wpa_s->current_ssid);
		}
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	} else {
		/* Timeout for receiving the first EAPOL packet */
		wpa_supplicant_req_auth_timeout(wpa_s, 10, 0);
	}
	wpa_supplicant_cancel_scan(wpa_s);

	wpa_s->last_eapol_matches_bssid = 0;

	if (wpa_s->pending_eapol_rx) {
		struct os_reltime now, age;
		os_get_reltime(&now);
		os_reltime_sub(&now, &wpa_s->pending_eapol_rx_time, &age);
		if (age.sec == 0 && age.usec < 100000 &&
		    os_memcmp(wpa_s->pending_eapol_rx_src, bssid, ETH_ALEN) ==
		    0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "Process pending EAPOL "
				"frame that was received just before "
				"association notification");
			wpa_supplicant_rx_eapol(
				wpa_s, wpa_s->pending_eapol_rx_src,
				wpabuf_head(wpa_s->pending_eapol_rx),
				wpabuf_len(wpa_s->pending_eapol_rx));
		}
		wpabuf_free(wpa_s->pending_eapol_rx);
		wpa_s->pending_eapol_rx = NULL;
	}

	if ((wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	     wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) &&
	    wpa_s->current_ssid &&
	    (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC_DONE)) {
		/* Set static WEP keys again */
		wpa_set_wep_keys(wpa_s, wpa_s->current_ssid);
	}

	//wpas_wps_notify_assoc(wpa_s, bssid);
}


#ifdef CONFIG_PSM_SUPER_LOWPOWER 
static int disconnect_reason_recoverable(u16 reason_code)
{
#if 0
	return reason_code == WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY ||
		reason_code == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
		reason_code == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA ;
#else
	return 1;
#endif
}
#endif
#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpa_supplicant_event_disassoc(struct wpa_supplicant *wpa_s,
					  u16 reason_code,
					  int locally_generated)
{
	const u8 *bssid;

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/*
		 * At least Host AP driver and a Prism3 card seemed to be
		 * generating streams of disconnected events when configuring
		 * IBSS for WPA-None. Ignore them for now.
		 */
		return;
	}

	bssid = wpa_s->bssid;
	if (is_zero_ether_addr(bssid))
		bssid = wpa_s->pending_bssid;

	if (!is_zero_ether_addr(bssid) ||
	    wpa_s->wpa_state >= WPA_AUTHENTICATING) {
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "bssid=" MACSTR
			" reason=%d%s",
			MAC2STR(bssid), reason_code,
			locally_generated ? " locally_generated=1" : "");
	}
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


static int could_be_psk_mismatch(struct wpa_supplicant *wpa_s, u16 reason_code,
				 int locally_generated)
{
	if (wpa_s->wpa_state != WPA_4WAY_HANDSHAKE ||
	    !wpa_key_mgmt_wpa_psk(wpa_s->key_mgmt))
		return 0; /* Not in 4-way handshake with PSK */

	/*
	 * It looks like connection was lost while trying to go through PSK
	 * 4-way handshake. Filter out known disconnection cases that are caused
	 * by something else than PSK mismatch to avoid confusing reports.
	 */

	if (locally_generated) {
		if (reason_code == WLAN_REASON_IE_IN_4WAY_DIFFERS)
			return 0;
	}

	return 1;
}

static wifi_discon_reason_e disconnect_reason_code = 0;
void wpa_update_disconnect_reason(u16 reason)
{
    switch (reason)
    {
        case WLAN_REASON_CONNECTED:
            disconnect_reason_code = REASON_NO_ERROR;
            break;
        case WLAN_REASON_4WAY_HANDSHAKE_FAIL:
            disconnect_reason_code = REASON_PWD_ERROR;
            break;
        case WLAN_REASON_UNSPECIFIED | WLAN_RX_SELF_DEFINE_FLAG:
            disconnect_reason_code = REASON_BEACON_LOST;
            break;
        case WLAN_REASON_DEAUTH_LEAVING | WLAN_RX_SELF_DEFINE_FLAG:
            disconnect_reason_code = REASON_USER_DISCONNECT;
            break;
        case WLAN_REASON_DEAUTH_LEAVING:
        case WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:
        case WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:
            disconnect_reason_code = REASON_AP_DISCONNECT;
            break;
        case WLAN_REASON_AP_NOT_FOUND:
            disconnect_reason_code = REASON_SCAN_NOT_FOUND;
            break;
        default:
            break;
    }
}

wifi_discon_reason_e wpa_disconnect_reason()
{	
    return disconnect_reason_code;
}


#ifdef CONFIG_PSM_SUPER_LOWPOWER
unsigned int fast_reconnect_freq = 0;
#endif
static void wpa_supplicant_event_disassoc_finish(struct wpa_supplicant *wpa_s,
						 u16 reason_code,
						 int locally_generated)
{
	const u8 *bssid;
	int authenticating;
	u8 prev_pending_bssid[ETH_ALEN];
#ifdef CONFIG_PSM_SUPER_LOWPOWER 
	struct wpa_bss *fast_reconnect = NULL;
	struct wpa_ssid *fast_reconnect_ssid = NULL;
#endif
	struct wpa_ssid *last_ssid;
	struct wpa_bss *curr = NULL;

	authenticating = wpa_s->wpa_state == WPA_AUTHENTICATING;
	os_memcpy(prev_pending_bssid, wpa_s->pending_bssid, ETH_ALEN);

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/*
		 * At least Host AP driver and a Prism3 card seemed to be
		 * generating streams of disconnected events when configuring
		 * IBSS for WPA-None. Ignore them for now.
		 */
		wpa_dbg(wpa_s, MSG_DEBUG, "Disconnect event - ignore in "
			"IBSS/WPA-None mode");
		return;
	}

	if (!wpa_s->disconnected && wpa_s->wpa_state >= WPA_AUTHENTICATING &&
	    reason_code == WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY &&
	    locally_generated)
		/*
		 * Remove the inactive AP (which is probably out of range) from
		 * the BSS list after marking disassociation. In particular
		 * mac80211-based drivers use the
		 * WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY reason code in
		 * locally generated disconnection events for cases where the
		 * AP does not reply anymore.
		 */
		curr = wpa_s->current_bss;

	if (could_be_psk_mismatch(wpa_s, reason_code, locally_generated)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: 4-Way Handshake failed - "
			"pre-shared key may be incorrect");
		//wpas_auth_failed(wpa_s, "WRONG_KEY");
		wpas_notify_sta_4way_hs_failed(wpa_s);
	}
#ifdef CONFIG_PSM_SUPER_LOWPOWER 
	if (!wpa_s->disconnected /*&&
	    (!wpa_s->auto_reconnect_disabled ||
	     wpa_s->key_mgmt == WPA_KEY_MGMT_WPS ||
	     wpas_wps_searching(wpa_s) ||
	     wpas_wps_reenable_networks_pending(wpa_s))*/) {
		 wpa_dbg(wpa_s, MSG_DEBUG, "Auto connect enabled: try to "
			"reconnect (wps=%d/%d wpa_state=%d)",
			wpa_s->key_mgmt == WPA_KEY_MGMT_WPS,
			wpas_wps_searching(wpa_s),
			wpa_s->wpa_state);
		if (wpa_s->wpa_state == WPA_COMPLETED &&
		    wpa_s->current_ssid &&
		    wpa_s->current_ssid->mode == WPAS_MODE_INFRA &&
		    !locally_generated &&
		    disconnect_reason_recoverable(reason_code)) {
			/*
			 * It looks like the AP has dropped association with
			 * us, but could allow us to get back in. Try to
			 * reconnect to the same BSS without full scan to save
			 * time for some common cases.
			 */
			fast_reconnect = wpa_s->current_bss;
			fast_reconnect_ssid = wpa_s->current_ssid;
		} else if (wpa_s->wpa_state >= WPA_ASSOCIATING)
			wpa_supplicant_req_scan(wpa_s, 0, 100000);
		else
			wpa_dbg(wpa_s, MSG_DEBUG, "Do not request new "
				"immediate scan");
	} else {
		wpa_dbg(wpa_s, MSG_DEBUG, "Auto connect disabled: do not "
			"try to re-connect");
		//wpa_s->reassociate = 0;
		wpa_s->disconnected = 1;
		//wpa_supplicant_cancel_sched_scan(wpa_s);
	}
#else
    if (wpa_s->wpa_state >= WPA_ASSOCIATING)
		wpa_supplicant_req_scan(wpa_s, 0, 100000);
#endif
	bssid = wpa_s->bssid;
	if (is_zero_ether_addr(bssid))
		bssid = wpa_s->pending_bssid;
	if (wpa_s->wpa_state >= WPA_AUTHENTICATING)
		wpas_connection_failed(wpa_s, bssid);
#ifdef CONFIG_PSM_SUPER_LOWPOWER 
    if(fast_reconnect)
    {
        wpa_supplicant_cancel_scan(wpa_s);
    }
#endif
	wpa_sm_notify_disassoc(wpa_s->wpa);
	if (locally_generated)
		wpa_s->disconnect_reason = -reason_code;
	else
		wpa_s->disconnect_reason = reason_code;
	wpa_update_disconnect_reason(locally_generated ? reason_code | WLAN_RX_SELF_DEFINE_FLAG :  reason_code);
	//wpas_notify_disconnect_reason(wpa_s);
	if (wpa_supplicant_dynamic_keys(wpa_s)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Disconnect event - remove keys");
		wpa_clear_keys(wpa_s, wpa_s->bssid);
	}
	last_ssid = wpa_s->current_ssid;
	wpa_supplicant_mark_disassoc(wpa_s);

	if (curr)
		wpa_bss_remove(wpa_s, curr, "Connection to AP lost");

	if (authenticating && (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME)) {
		sme_disassoc_while_authenticating(wpa_s, prev_pending_bssid);
		wpa_s->current_ssid = last_ssid;
	}

#ifdef CONFIG_PSM_SUPER_LOWPOWER
	if (fast_reconnect &&
	    !wpas_network_disabled(wpa_s, fast_reconnect_ssid) &&
	    !wpas_temp_disabled(wpa_s, fast_reconnect_ssid) /*&&
	    !wpa_is_bss_tmp_disallowed(wpa_s, fast_reconnect->bssid)*/) {
#ifndef CONFIG_NO_SCAN_PROCESSING
		wpa_dbg(wpa_s, MSG_DEBUG, "Try to reconnect to the same BSS");
        fast_reconnect_freq = fast_reconnect->freq;
		if (wpa_supplicant_connect(wpa_s, fast_reconnect,
					   fast_reconnect_ssid) < 0) {
			/* Recover through full scan */
			wpa_supplicant_req_scan(wpa_s, 0, 100000);
		}
#endif /* CONFIG_NO_SCAN_PROCESSING */
	} else if (fast_reconnect) {
		/*
		 * Could not reconnect to the same BSS due to network being
		 * disabled. Use a new scan to match the alternative behavior
		 * above, i.e., to continue automatic reconnection attempt in a
		 * way that enforces disabled network rules.
		 */
		wpa_supplicant_req_scan(wpa_s, 0, 100000);
	}
#endif
}


#ifdef CONFIG_DELAYED_MIC_ERROR_REPORT
void wpa_supplicant_delayed_mic_error_report(void *eloop_ctx, void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (!wpa_s->pending_mic_error_report)
		return;

	wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Sending pending MIC error report");
	wpa_sm_key_request(wpa_s->wpa, 1, wpa_s->pending_mic_error_pairwise);
	wpa_s->pending_mic_error_report = 0;
}
#endif /* CONFIG_DELAYED_MIC_ERROR_REPORT */


static void wpa_supplicant_event_michael_mic_failure(struct wpa_supplicant *wpa_s,
					 union wpa_event_data *data)
{
	int pairwise;
	struct os_reltime t;

#if 1 //for softap
    if (wpa_s->ap_iface)
    {
        struct sta_info *sta = (struct sta_info *)ap_get_sta(wpa_s->ap_iface->bss[0], data->michael_mic_failure.src);
        if (sta)
        {
            michael_mic_failure(wpa_s->ap_iface->bss[0], data->michael_mic_failure.src, 1);
            return;
        }
    }
#endif

	wpa_msg(wpa_s, MSG_WARNING, "Michael MIC failure detected");
	pairwise = (data && data->michael_mic_failure.unicast);
	os_get_reltime(&t);
	if ((wpa_s->last_michael_mic_error.sec &&
	     !os_reltime_expired(&t, &wpa_s->last_michael_mic_error, 60)) ||
	    wpa_s->pending_mic_error_report) {
		if (wpa_s->pending_mic_error_report) {
			/*
			 * Send the pending MIC error report immediately since
			 * we are going to start countermeasures and AP better
			 * do the same.
			 */
			wpa_sm_key_request(wpa_s->wpa, 1,
					   wpa_s->pending_mic_error_pairwise);
		}

		/* Send the new MIC error report immediately since we are going
		 * to start countermeasures and AP better do the same.
		 */
		wpa_sm_key_request(wpa_s->wpa, 1, pairwise);

		/* initialize countermeasures */
		wpa_s->countermeasures = 1;


		wpa_msg(wpa_s, MSG_WARNING, "TKIP countermeasures started");

		/*
		 * Need to wait for completion of request frame. We do not get
		 * any callback for the message completion, so just wait a
		 * short while and hope for the best. */
		os_sleep(0, 10000);

		//wpa_drv_set_countermeasures(wpa_s, 1);
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_MICHAEL_MIC_FAILURE);
		eloop_cancel_timeout(wpa_supplicant_stop_countermeasures,
				     wpa_s, NULL);
		eloop_register_timeout(60, 0,
				       wpa_supplicant_stop_countermeasures,
				       wpa_s, NULL);
		/* TODO: mark the AP rejected for 60 second. STA is
		 * allowed to associate with another AP.. */
	} else {
#ifdef CONFIG_DELAYED_MIC_ERROR_REPORT
		if (wpa_s->mic_errors_seen) {
			/*
			 * Reduce the effectiveness of Michael MIC error
			 * reports as a means for attacking against TKIP if
			 * more than one MIC failure is noticed with the same
			 * PTK. We delay the transmission of the reports by a
			 * random time between 0 and 60 seconds in order to
			 * force the attacker wait 60 seconds before getting
			 * the information on whether a frame resulted in a MIC
			 * failure.
			 */
			u8 rval[4];
			int sec;

			if (os_get_random(rval, sizeof(rval)) < 0)
				sec = os_random() % 60;
			else
				sec = WPA_GET_BE32(rval) % 60;
			wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Delay MIC error "
				"report %d seconds", sec);
			wpa_s->pending_mic_error_report = 1;
			wpa_s->pending_mic_error_pairwise = pairwise;
			eloop_cancel_timeout(
				wpa_supplicant_delayed_mic_error_report,
				wpa_s, NULL);
			eloop_register_timeout(
				sec, os_random() % 1000000,
				wpa_supplicant_delayed_mic_error_report,
				wpa_s, NULL);
		} else {
			wpa_sm_key_request(wpa_s->wpa, 1, pairwise);
		}
#else /* CONFIG_DELAYED_MIC_ERROR_REPORT */
		wpa_sm_key_request(wpa_s->wpa, 1, pairwise);
#endif /* CONFIG_DELAYED_MIC_ERROR_REPORT */
	}
	wpa_s->last_michael_mic_error = t;
	wpa_s->mic_errors_seen++;
}


#ifdef CONFIG_TERMINATE_ONLASTIF
static int any_interfaces(struct wpa_supplicant *head)
{
	struct wpa_supplicant *wpa_s;

	for (wpa_s = head; wpa_s != NULL; wpa_s = wpa_s->next)
		if (!wpa_s->interface_removed)
			return 1;
	return 0;
}
#endif /* CONFIG_TERMINATE_ONLASTIF */

#if 0
static void
wpa_supplicant_event_interface_status(struct wpa_supplicant *wpa_s,
				      union wpa_event_data *data)
{
	if (os_strcmp(wpa_s->ifname, data->interface_status.ifname) != 0)
		return;

	switch (data->interface_status.ievent) {
	case EVENT_INTERFACE_ADDED:
		if (!wpa_s->interface_removed)
			break;
		wpa_s->interface_removed = 0;
		wpa_dbg(wpa_s, MSG_DEBUG, "Configured interface was added");
		if (wpa_supplicant_driver_init(wpa_s) < 0) {
			wpa_msg(wpa_s, MSG_INFO, "Failed to initialize the "
				"driver after interface was added");
		}

#ifdef CONFIG_P2P
		if (!wpa_s->global->p2p &&
		    !wpa_s->global->p2p_disabled &&
		    !wpa_s->conf->p2p_disabled &&
		    (wpa_s->drv_flags &
		     WPA_DRIVER_FLAGS_DEDICATED_P2P_DEVICE) &&
		    wpas_p2p_add_p2pdev_interface(
			    wpa_s, wpa_s->global->params.conf_p2p_dev) < 0) {
			wpa_printf(MSG_INFO,
				   "P2P: Failed to enable P2P Device interface");
			/* Try to continue without. P2P will be disabled. */
		}
#endif /* CONFIG_P2P */

		break;
	case EVENT_INTERFACE_REMOVED:
		wpa_dbg(wpa_s, MSG_DEBUG, "Configured interface was removed");
		wpa_s->interface_removed = 1;
		wpa_supplicant_mark_disassoc(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_INTERFACE_DISABLED);
		l2_packet_deinit(wpa_s->l2);
		wpa_s->l2 = NULL;

#ifdef CONFIG_P2P
		if (wpa_s->global->p2p &&
		    wpa_s->global->p2p_init_wpa_s->parent == wpa_s &&
		    (wpa_s->drv_flags &
		     WPA_DRIVER_FLAGS_DEDICATED_P2P_DEVICE)) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"Removing P2P Device interface");
			wpa_supplicant_remove_iface(
				wpa_s->global, wpa_s->global->p2p_init_wpa_s,
				0);
			wpa_s->global->p2p_init_wpa_s = NULL;
		}
#endif /* CONFIG_P2P */

#ifdef CONFIG_MATCH_IFACE
		if (wpa_s->matched) {
			wpa_supplicant_remove_iface(wpa_s->global, wpa_s, 0);
			break;
		}
#endif /* CONFIG_MATCH_IFACE */

#ifdef CONFIG_TERMINATE_ONLASTIF
		/* check if last interface */
		if (!any_interfaces(wpa_s->global->ifaces))
			eloop_terminate();
#endif /* CONFIG_TERMINATE_ONLASTIF */
		break;
	}
}
#endif

#ifdef CONFIG_PEERKEY
static void
wpa_supplicant_event_stkstart(struct wpa_supplicant *wpa_s,
			      union wpa_event_data *data)
{
	if (data == NULL)
		return;
	wpa_sm_stkstart(wpa_s->wpa, data->stkstart.peer);
}
#endif /* CONFIG_PEERKEY */


#ifdef CONFIG_TDLS
static void wpa_supplicant_event_tdls(struct wpa_supplicant *wpa_s,
				      union wpa_event_data *data)
{
	if (data == NULL)
		return;
	switch (data->tdls.oper) {
	case TDLS_REQUEST_SETUP:
		wpa_tdls_remove(wpa_s->wpa, data->tdls.peer);
		if (wpa_tdls_is_external_setup(wpa_s->wpa))
			wpa_tdls_start(wpa_s->wpa, data->tdls.peer);
		else
			wpa_drv_tdls_oper(wpa_s, TDLS_SETUP, data->tdls.peer);
		break;
	case TDLS_REQUEST_TEARDOWN:
		if (wpa_tdls_is_external_setup(wpa_s->wpa))
			wpa_tdls_teardown_link(wpa_s->wpa, data->tdls.peer,
					       data->tdls.reason_code);
		else
			wpa_drv_tdls_oper(wpa_s, TDLS_TEARDOWN,
					  data->tdls.peer);
		break;
	case TDLS_REQUEST_DISCOVER:
			wpa_tdls_send_discovery_request(wpa_s->wpa,
							data->tdls.peer);
		break;
	}
}
#endif /* CONFIG_TDLS */


#ifdef CONFIG_WNM
static void wpa_supplicant_event_wnm(struct wpa_supplicant *wpa_s,
				     union wpa_event_data *data)
{
	if (data == NULL)
		return;
	switch (data->wnm.oper) {
	case WNM_OPER_SLEEP:
		wpa_printf(MSG_DEBUG, "Start sending WNM-Sleep Request "
			   "(action=%d, intval=%d)",
			   data->wnm.sleep_action, data->wnm.sleep_intval);
		ieee802_11_send_wnmsleep_req(wpa_s, data->wnm.sleep_action,
					     data->wnm.sleep_intval, NULL);
		break;
	}
}
#endif /* CONFIG_WNM */


#ifdef CONFIG_IEEE80211R
static void
wpa_supplicant_event_ft_response(struct wpa_supplicant *wpa_s,
				 union wpa_event_data *data)
{
	if (data == NULL)
		return;

	if (wpa_ft_process_response(wpa_s->wpa, data->ft_ies.ies,
				    data->ft_ies.ies_len,
				    data->ft_ies.ft_action,
				    data->ft_ies.target_ap,
				    data->ft_ies.ric_ies,
				    data->ft_ies.ric_ies_len) < 0) {
		/* TODO: prevent MLME/driver from trying to associate? */
	}
}
#endif /* CONFIG_IEEE80211R */


#ifdef CONFIG_IBSS_RSN
static void wpa_supplicant_event_ibss_rsn_start(struct wpa_supplicant *wpa_s,
						union wpa_event_data *data)
{
	struct wpa_ssid *ssid;
	if (wpa_s->wpa_state < WPA_ASSOCIATED)
		return;
	if (data == NULL)
		return;
	ssid = wpa_s->current_ssid;
	if (ssid == NULL)
		return;
	if (ssid->mode != WPAS_MODE_IBSS || !wpa_key_mgmt_wpa(ssid->key_mgmt))
		return;

	ibss_rsn_start(wpa_s->ibss_rsn, data->ibss_rsn_start.peer);
}


static void wpa_supplicant_event_ibss_auth(struct wpa_supplicant *wpa_s,
					   union wpa_event_data *data)
{
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (ssid == NULL)
		return;

	/* check if the ssid is correctly configured as IBSS/RSN */
	if (ssid->mode != WPAS_MODE_IBSS || !wpa_key_mgmt_wpa(ssid->key_mgmt))
		return;

	ibss_rsn_handle_auth(wpa_s->ibss_rsn, data->rx_mgmt.frame,
			     data->rx_mgmt.frame_len);
}
#endif /* CONFIG_IBSS_RSN */


#ifdef CONFIG_IEEE80211R
static void ft_rx_action(struct wpa_supplicant *wpa_s, const u8 *data,
			 size_t len)
{
	const u8 *sta_addr, *target_ap_addr;
	u16 status;

	wpa_hexdump(MSG_MSGDUMP, "FT: RX Action", data, len);
	if (!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME))
		return; /* only SME case supported for now */
	if (len < 1 + 2 * ETH_ALEN + 2)
		return;
	if (data[0] != 2)
		return; /* Only FT Action Response is supported for now */
	sta_addr = data + 1;
	target_ap_addr = data + 1 + ETH_ALEN;
	status = WPA_GET_LE16(data + 1 + 2 * ETH_ALEN);
	wpa_dbg(wpa_s, MSG_DEBUG, "FT: Received FT Action Response: STA "
		MACSTR " TargetAP " MACSTR " status %u",
		MAC2STR(sta_addr), MAC2STR(target_ap_addr), status);

	if (os_memcmp(sta_addr, wpa_s->own_addr, ETH_ALEN) != 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "FT: Foreign STA Address " MACSTR
			" in FT Action Response", MAC2STR(sta_addr));
		return;
	}

	if (status) {
		wpa_dbg(wpa_s, MSG_DEBUG, "FT: FT Action Response indicates "
			"failure (status code %d)", status);
		/* TODO: report error to FT code(?) */
		return;
	}

	if (wpa_ft_process_response(wpa_s->wpa, data + 1 + 2 * ETH_ALEN + 2,
				    len - (1 + 2 * ETH_ALEN + 2), 1,
				    target_ap_addr, NULL, 0) < 0)
		return;

#ifdef CONFIG_SME
	{
		struct wpa_bss *bss;
		bss = wpa_bss_get_bssid(wpa_s, target_ap_addr);
		if (bss)
			wpa_s->sme.freq = bss->freq;
		wpa_s->sme.auth_alg = WPA_AUTH_ALG_FT;
		sme_associate(wpa_s, WPAS_MODE_INFRA, target_ap_addr,
			      WLAN_AUTH_FT);
	}
#endif /* CONFIG_SME */
}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_IEEE80211W
static void wpa_supplicant_event_unprot_deauth(struct wpa_supplicant *wpa_s,
					       struct unprot_deauth *e)
{
	wpa_printf(MSG_DEBUG, "Unprotected Deauthentication frame "
		   "dropped: " MACSTR " -> " MACSTR
		   " (reason code %u)",
		   MAC2STR(e->sa), MAC2STR(e->da), e->reason_code);
	sme_event_unprot_disconnect(wpa_s, e->sa, e->da, e->reason_code);
}

static void wpa_supplicant_event_unprot_disassoc(struct wpa_supplicant *wpa_s,
						 struct unprot_disassoc *e)
{
	wpa_printf(MSG_DEBUG, "Unprotected Disassociation frame "
		   "dropped: " MACSTR " -> " MACSTR
		   " (reason code %u)",
		   MAC2STR(e->sa), MAC2STR(e->da), e->reason_code);
	sme_event_unprot_disconnect(wpa_s, e->sa, e->da, e->reason_code);
}
#endif /* CONFIG_IEEE80211W */

static void wpas_event_disconnect(struct wpa_supplicant *wpa_s, const u8 *addr,
				  u16 reason_code, int locally_generated,
				  const u8 *ie, size_t ie_len, int deauth)
{
#if 0
#ifdef CONFIG_AP
	if (wpa_s->ap_iface && addr) {
		hostapd_notif_disassoc(wpa_s->ap_iface->bss[0], addr);
		return;
	}

	if (wpa_s->ap_iface) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Ignore deauth event in AP mode");
		return;
	}
#endif /* CONFIG_AP */
#endif

	if (!locally_generated)
		wpa_s->own_disconnect_req = 0;

	//wpa_supplicant_event_disassoc(wpa_s, reason_code, locally_generated);

	//if (((reason_code == WLAN_REASON_IEEE_802_1X_AUTH_FAILED) &&
	//     !wpa_s->eap_expected_failure))
	//	wpas_auth_failed(wpa_s, "AUTH_FAILED");

#ifdef CONFIG_P2P
	if (deauth && reason_code > 0) {
		if (wpas_p2p_deauth_notif(wpa_s, addr, reason_code, ie, ie_len,
					  locally_generated) > 0) {
			/*
			 * The interface was removed, so cannot continue
			 * processing any additional operations after this.
			 */
			return;
		}
	}
#endif /* CONFIG_P2P */

	wpa_supplicant_event_disassoc_finish(wpa_s, reason_code,
					     locally_generated);
}


static void wpas_event_disassoc(struct wpa_supplicant *wpa_s,
				struct disassoc_info *info)
{
	u16 reason_code = 0;
	int locally_generated = 0;
	const u8 *addr = NULL;
	const u8 *ie = NULL;
	size_t ie_len = 0;

	wpa_dbg(wpa_s, MSG_DEBUG, "Disassociation notification");

	if (info) {
		addr = info->addr;
		ie = info->ie;
		ie_len = info->ie_len;
		reason_code = info->reason_code;
		locally_generated = info->locally_generated;
		wpa_dbg(wpa_s, MSG_DEBUG, " * reason %u%s", reason_code,
			locally_generated ? " (locally generated)" : "");
		if (addr)
			wpa_dbg(wpa_s, MSG_DEBUG, " * address " MACSTR,
				MAC2STR(addr));
		wpa_hexdump(MSG_DEBUG, "Disassociation frame IE(s)",
			    ie, ie_len);
	}

#if 0
#ifdef CONFIG_AP
	if (wpa_s->ap_iface && info && info->addr) {
		hostapd_notif_disassoc(wpa_s->ap_iface->bss[0], info->addr);
		return;
	}

	if (wpa_s->ap_iface) {
		wpa_dbg(wpa_s, MSG_DEBUG, "Ignore disassoc event in AP mode");
		return;
	}
#endif /* CONFIG_AP */
#endif

#ifdef CONFIG_P2P
	if (info) {
		wpas_p2p_disassoc_notif(
			wpa_s, info->addr, reason_code, info->ie, info->ie_len,
			locally_generated);
	}
#endif /* CONFIG_P2P */

	if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME)
		sme_event_disassoc(wpa_s, info);

	wpas_event_disconnect(wpa_s, addr, reason_code, locally_generated,
			      ie, ie_len, 0);
}


static void wpas_event_deauth(struct wpa_supplicant *wpa_s,
			      struct deauth_info *info)
{
	u16 reason_code = 0;
	int locally_generated = 0;
	const u8 *addr = NULL;
	const u8 *ie = NULL;
	size_t ie_len = 0;

	wpa_dbg(wpa_s, MSG_DEBUG, "Deauthentication notification");

	if (info) {
		addr = info->addr;
		ie = info->ie;
		ie_len = info->ie_len;
		reason_code = info->reason_code;
		locally_generated = info->locally_generated;
		wpa_dbg(wpa_s, MSG_DEBUG, " * reason %u%s",
			reason_code,
			locally_generated ? " (locally generated)" : "");
		if (addr) {
			wpa_dbg(wpa_s, MSG_DEBUG, " * address " MACSTR,
				MAC2STR(addr));
		}
		wpa_hexdump(MSG_DEBUG, "Deauthentication frame IE(s)",
			    ie, ie_len);
	}

	wpa_reset_ft_completed(wpa_s->wpa);

	wpas_event_disconnect(wpa_s, addr, reason_code,
			      locally_generated, ie, ie_len, 1);
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static const char * reg_init_str(enum reg_change_initiator init)
{
	switch (init) {
	case REGDOM_SET_BY_CORE:
		return "CORE";
	case REGDOM_SET_BY_USER:
		return "USER";
	case REGDOM_SET_BY_DRIVER:
		return "DRIVER";
	case REGDOM_SET_BY_COUNTRY_IE:
		return "COUNTRY_IE";
	case REGDOM_BEACON_HINT:
		return "BEACON_HINT";
	}
	return "?";
}


static const char * reg_type_str(enum reg_type type)
{
	switch (type) {
	case REGDOM_TYPE_UNKNOWN:
		return "UNKNOWN";
	case REGDOM_TYPE_COUNTRY:
		return "COUNTRY";
	case REGDOM_TYPE_WORLD:
		return "WORLD";
	case REGDOM_TYPE_CUSTOM_WORLD:
		return "CUSTOM_WORLD";
	case REGDOM_TYPE_INTERSECTION:
		return "INTERSECTION";
	}
	return "?";
}


static void wpa_supplicant_update_channel_list(
	struct wpa_supplicant *wpa_s, struct channel_list_changed *info)
{
#if 0
	struct wpa_supplicant *ifs;

	/*
	 * To allow backwards compatibility with higher level layers that
	 * assumed the REGDOM_CHANGE event is sent over the initially added
	 * interface. Find the highest parent of this interface and use it to
	 * send the event.
	 */
	for (ifs = wpa_s; ifs->parent && ifs != ifs->parent; ifs = ifs->parent)
		;

	wpa_msg(ifs, MSG_INFO, WPA_EVENT_REGDOM_CHANGE "init=%s type=%s%s%s",
		reg_init_str(info->initiator), reg_type_str(info->type),
		info->alpha2[0] ? " alpha2=" : "",
		info->alpha2[0] ? info->alpha2 : "");

	if (wpa_s->drv_priv == NULL)
		return; /* Ignore event during drv initialization */

	dl_list_for_each(ifs, &wpa_s->radio->ifaces, struct wpa_supplicant,
			 radio_list) {
		wpa_printf(MSG_DEBUG, "%s: Updating hw mode",
			   ifs->ifname);
		free_hw_features(ifs);
		ifs->hw.modes = wpa_drv_get_hw_feature_data(
			ifs, &ifs->hw.num_modes, &ifs->hw.flags);

        if (ifs->sched_scanning) {
			wpa_dbg(ifs, MSG_DEBUG,
				"Channel list changed - restart sched_scan");
			wpas_scan_restart_sched_scan(ifs);
		}
	}
#endif
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


static void wpas_event_rx_mgmt_action(struct wpa_supplicant *wpa_s,
				      const u8 *frame, size_t len, int freq,
				      int rssi)
{
	const struct ieee80211_mgmt *mgmt;
	const u8 *payload;
	size_t plen;
	u8 category;

	if (len < IEEE80211_HDRLEN + 2)
		return;

	mgmt = (const struct ieee80211_mgmt *) frame;
	payload = frame + IEEE80211_HDRLEN;
	category = *payload++;
	plen = len - IEEE80211_HDRLEN - 1;

	wpa_dbg(wpa_s, MSG_DEBUG, "Received Action frame: SA=" MACSTR
		" Category=%u DataLen=%d freq=%d MHz",
		MAC2STR(mgmt->sa), category, (int) plen, freq);

	if (category == WLAN_ACTION_WMM) {
	//	wmm_ac_rx_action(wpa_s, mgmt->da, mgmt->sa, payload, plen);
		return;
	}

#ifdef CONFIG_IEEE80211R
	if (category == WLAN_ACTION_FT) {
		ft_rx_action(wpa_s, payload, plen);
		return;
	}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_IEEE80211W
#ifdef CONFIG_SME
	if (category == WLAN_ACTION_SA_QUERY) {
		sme_sa_query_rx(wpa_s, mgmt->sa, payload, plen);
		return;
	}
#endif /* CONFIG_SME */
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_WNM
	if (mgmt->u.action.category == WLAN_ACTION_WNM) {
		ieee802_11_rx_wnm_action(wpa_s, mgmt, len);
		return;
	}
#endif /* CONFIG_WNM */

#ifdef CONFIG_GAS
	if ((mgmt->u.action.category == WLAN_ACTION_PUBLIC ||
	     mgmt->u.action.category == WLAN_ACTION_PROTECTED_DUAL) &&
	    gas_query_rx(wpa_s->gas, mgmt->da, mgmt->sa, mgmt->bssid,
			 mgmt->u.action.category,
			 payload, plen, freq) == 0)
		return;
#endif /* CONFIG_GAS */

#ifdef CONFIG_TDLS
	if (category == WLAN_ACTION_PUBLIC && plen >= 4 &&
	    payload[0] == WLAN_TDLS_DISCOVERY_RESPONSE) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"TDLS: Received Discovery Response from " MACSTR,
			MAC2STR(mgmt->sa));
		return;
	}
#endif /* CONFIG_TDLS */

#ifdef CONFIG_INTERWORKING
	if (category == WLAN_ACTION_QOS && plen >= 1 &&
	    payload[0] == QOS_QOS_MAP_CONFIG) {
		const u8 *pos = payload + 1;
		size_t qlen = plen - 1;
		wpa_dbg(wpa_s, MSG_DEBUG, "Interworking: Received QoS Map Configure frame from "
			MACSTR, MAC2STR(mgmt->sa));
		if (os_memcmp(mgmt->sa, wpa_s->bssid, ETH_ALEN) == 0 &&
		    qlen > 2 && pos[0] == WLAN_EID_QOS_MAP_SET &&
		    pos[1] <= qlen - 2 && pos[1] >= 16)
			wpas_qos_map_set(wpa_s, pos + 2, pos[1]);
		return;
	}
#endif /* CONFIG_INTERWORKING */

	if (category == WLAN_ACTION_RADIO_MEASUREMENT &&
	    payload[0] == WLAN_RRM_RADIO_MEASUREMENT_REQUEST) {
		wpas_rrm_handle_radio_measurement_request(wpa_s, mgmt->sa,
							  payload + 1,
							  plen - 1);
		return;
	}

	if (category == WLAN_ACTION_RADIO_MEASUREMENT &&
	    payload[0] == WLAN_RRM_NEIGHBOR_REPORT_RESPONSE) {
		wpas_rrm_process_neighbor_rep(wpa_s, payload + 1, plen - 1);
		return;
	}

	if (category == WLAN_ACTION_RADIO_MEASUREMENT &&
	    payload[0] == WLAN_RRM_LINK_MEASUREMENT_REQUEST) {
		wpas_rrm_handle_link_measurement_request(wpa_s, mgmt->sa,
							 payload + 1, plen - 1,
							 rssi);
		return;
	}

#ifdef CONFIG_FST
	if (mgmt->u.action.category == WLAN_ACTION_FST && wpa_s->fst) {
		fst_rx_action(wpa_s->fst, mgmt, len);
		return;
	}
#endif /* CONFIG_FST */

	//if (wpa_s->ifmsh)
	//	mesh_mpm_action_rx(wpa_s, mgmt, len);
}


#ifdef COMPILE_WARNING_OPTIMIZE_WPA
static void wpa_supplicant_notify_avoid_freq(struct wpa_supplicant *wpa_s,
					     union wpa_event_data *event)
{
	struct wpa_freq_range_list *list;
	char *str = NULL;

	list = &event->freq_range;

	if (list->num)
		str = freq_range_list_str(list);
	wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_AVOID_FREQ "ranges=%s",
		str ? str : "");

#ifdef CONFIG_P2P
	if (freq_range_list_parse(&wpa_s->global->p2p_go_avoid_freq, str)) {
	
        #ifndef CONFIG_ECR6600_DBGTRIM
		wpa_dbg(wpa_s, MSG_ERROR, "%s: Failed to parse freq range",
			__func__);
		#else
        wpa_dbg_error(wpa_s, MSG_ERROR, "%s: Failed to parse freq range",
		#endif
	} else {
		wpa_dbg(wpa_s, MSG_DEBUG, "P2P: Update channel list based on frequency avoid event");

		/*
		 * The update channel flow will also take care of moving a GO
		 * from the unsafe frequency if needed.
		 */
		wpas_p2p_update_channel_list(wpa_s,
					     WPAS_P2P_CHANNEL_UPDATE_AVOID);
	}
#endif /* CONFIG_P2P */

	os_free(str);
}


static void wpa_supplicant_event_assoc_auth(struct wpa_supplicant *wpa_s,
					    union wpa_event_data *data)
{
	wpa_dbg(wpa_s, MSG_DEBUG,
		"Connection authorized by device, previous state %d",
		wpa_s->wpa_state);
	if (wpa_s->wpa_state == WPA_ASSOCIATED) {
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	}
	wpa_sm_set_rx_replay_ctr(wpa_s->wpa, data->assoc_info.key_replay_ctr);
	wpa_sm_set_ptk_kck_kek(wpa_s->wpa, data->assoc_info.ptk_kck,
			       data->assoc_info.ptk_kck_len,
			       data->assoc_info.ptk_kek,
			       data->assoc_info.ptk_kek_len);
}
#endif /* COMPILE_WARNING_OPTIMIZE_WPA */


void wpa_supplicant_event(void *ctx, enum wpa_event_type event,
			  union wpa_event_data *data)
{
	struct wpa_supplicant *wpa_s = ctx;
	//int resched;

/*	if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED &&
	    event != EVENT_INTERFACE_ENABLED &&
	    event != EVENT_INTERFACE_STATUS &&
	    event != EVENT_SCAN_RESULTS &&
	    event != EVENT_SCHED_SCAN_STOPPED) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"Ignore event %s (%d) while interface is disabled",
			event_to_string(event), event);
		return;
	}
*/
#ifndef CONFIG_NO_STDOUT_DEBUG
{
	int level = MSG_DEBUG;

	if (event == EVENT_RX_MGMT && data->rx_mgmt.frame_len >= 24) {
		const struct ieee80211_hdr *hdr;
		u16 fc;
		hdr = (const struct ieee80211_hdr *) data->rx_mgmt.frame;
		fc = le_to_host16(hdr->frame_control);
		if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT &&
		    WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_BEACON)
			level = MSG_EXCESSIVE;
	}

	wpa_dbg(wpa_s, level, "Event %s (%d) received",
		event_to_string(event), event);
}
#endif /* CONFIG_NO_STDOUT_DEBUG */

	switch (event) {
	case EVENT_AUTH:
#ifdef CONFIG_FST
		if (!wpas_fst_update_mbie(wpa_s, data->auth.ies,
					  data->auth.ies_len))
			wpa_printf(MSG_DEBUG,
				   "FST: MB IEs updated from auth IE");
#endif /* CONFIG_FST */
		sme_event_auth(wpa_s, data);
		break;
	case EVENT_ASSOC:
#ifdef CONFIG_TESTING_OPTIONS
		if (wpa_s->ignore_auth_resp) {
			wpa_printf(MSG_INFO,
				   "EVENT_ASSOC - ignore_auth_resp active!");
			break;
		}
#endif /* CONFIG_TESTING_OPTIONS */
		wpa_supplicant_event_assoc(wpa_s, data);
		//if (data && data->assoc_info.authorized)
		//	wpa_supplicant_event_assoc_auth(wpa_s, data);
		if (data) {
			wpa_msg(wpa_s, MSG_INFO,
				WPA_EVENT_SUBNET_STATUS_UPDATE "status=%u",
				data->assoc_info.subnet_status);
		}
		break;
	case EVENT_DISASSOC:
		if (data && (!data->disassoc_info.locally_generated))
		{
			wpa_scan_res_mark_fail(wpa_s);
		}
		wpas_event_disassoc(wpa_s,
				    data ? &data->disassoc_info : NULL);
		break;
	case EVENT_DEAUTH:
#ifdef CONFIG_TESTING_OPTIONS
		if (wpa_s->ignore_auth_resp) {
			wpa_printf(MSG_INFO,
				   "EVENT_DEAUTH - ignore_auth_resp active!");
			break;
		}
#endif /* CONFIG_TESTING_OPTIONS */
		if (data && (!data->deauth_info.locally_generated))
		{
			wpa_scan_res_mark_fail(wpa_s);
		}
		wpas_event_deauth(wpa_s,
				  data ? &data->deauth_info : NULL);
		break;
	case EVENT_MICHAEL_MIC_FAILURE:
		wpa_supplicant_event_michael_mic_failure(wpa_s, data);
		break;
#ifndef CONFIG_NO_SCAN_PROCESSING
	case EVENT_SCAN_STARTED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_SCAN_RESULTS:
		/*if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED) {
			wpa_s->scan_res_handler = NULL;
			wpa_s->own_scan_running = 0;
			wpa_s->radio->external_scan_running = 0;
			wpa_s->last_scan_req = NORMAL_SCAN_REQ;
			break;
		}

		if (!(data && data->scan_info.external_scan) &&
		    os_reltime_initialized(&wpa_s->scan_start_time)) {
			struct os_reltime now, diff;
			os_get_reltime(&now);
			os_reltime_sub(&now, &wpa_s->scan_start_time, &diff);
			wpa_s->scan_start_time.sec = 0;
			wpa_s->scan_start_time.usec = 0;
			wpa_dbg(wpa_s, MSG_DEBUG, "Scan completed in %ld.%06ld seconds",
				diff.sec, diff.usec);
		}*/
		if (wpa_supplicant_event_scan_results(wpa_s, data))
			break; /* interface may have been removed */
		if (!(data && data->scan_info.external_scan))
			wpa_s->own_scan_running = 0;
		if (data && data->scan_info.nl_scan_event)
			wpa_s->radio->external_scan_running = 0;
		radio_work_check_next(wpa_s);
		break;
#endif /* CONFIG_NO_SCAN_PROCESSING */
	case EVENT_ASSOCINFO:
		wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_INTERFACE_STATUS:
		wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_PMKID_CANDIDATE:
		wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
#ifdef CONFIG_PEERKEY
	case EVENT_STKSTART:
		wpa_supplicant_event_stkstart(wpa_s, data);
		break;
#endif /* CONFIG_PEERKEY */
#ifdef CONFIG_TDLS
	case EVENT_TDLS:
		wpa_supplicant_event_tdls(wpa_s, data);
		break;
#endif /* CONFIG_TDLS */
#ifdef CONFIG_WNM
	case EVENT_WNM:
		wpa_supplicant_event_wnm(wpa_s, data);
		break;
#endif /* CONFIG_WNM */
#ifdef CONFIG_IEEE80211R
	case EVENT_FT_RESPONSE:
		wpa_supplicant_event_ft_response(wpa_s, data);
		break;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IBSS_RSN
	case EVENT_IBSS_RSN_START:
		wpa_supplicant_event_ibss_rsn_start(wpa_s, data);
		break;
#endif /* CONFIG_IBSS_RSN */
	case EVENT_ASSOC_REJECT:
		wpa_scan_res_mark_fail(wpa_s);
		if (data->assoc_reject.bssid)
			wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_ASSOC_REJECT
				"bssid=" MACSTR	" status_code=%u%s",
				MAC2STR(data->assoc_reject.bssid),
				data->assoc_reject.status_code,
				data->assoc_reject.timed_out ? " timeout" : "");
		else
			wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_ASSOC_REJECT
				"status_code=%u%s",
				data->assoc_reject.status_code,
				data->assoc_reject.timed_out ? " timeout" : "");
		wpa_s->assoc_status_code = data->assoc_reject.status_code;
		//wpas_notify_assoc_status_code(wpa_s);
		if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME)
			sme_event_assoc_reject(wpa_s, data);
		else {
			const u8 *bssid = data->assoc_reject.bssid;
			if (bssid == NULL || is_zero_ether_addr(bssid))
				bssid = wpa_s->pending_bssid;
            if (wpas_pmkid_get() != NULL  && wpa_s->current_ssid && wpa_key_mgmt_sae(wpa_s->current_ssid->key_mgmt) 
    	         /*&& (wpa_s->assoc_status_code == WLAN_STATUS_INVALID_PMKID || wpa_s->assoc_status_code == WLAN_STATUS_INVALID_IE)*/ )
            {
        		wpa_dbg(wpa_s, MSG_DEBUG,
        			"PMKSA caching attempt rejected - drop PMKSA cache entry and fall back to SAE authentication:%d\r\n", wpa_s->assoc_status_code);
        		wpas_pmk_info_clean();
            }
            
			wpas_connection_failed(wpa_s, bssid);
			wpa_supplicant_mark_disassoc(wpa_s);
		}
		break;
	case EVENT_AUTH_TIMED_OUT:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_ASSOC_TIMED_OUT:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_TX_STATUS:
		wpa_dbg(wpa_s, MSG_DEBUG, "EVENT_TX_STATUS dst=" MACSTR
			" type=%d stype=%d",
			MAC2STR(data->tx_status.dst),
			data->tx_status.type, data->tx_status.stype);
#ifdef CONFIG_AP
		if (wpa_s->ap_iface == NULL) {
#ifdef CONFIG_OFFCHANNEL
			if (data->tx_status.type == WLAN_FC_TYPE_MGMT &&
			    data->tx_status.stype == WLAN_FC_STYPE_ACTION)
				offchannel_send_action_tx_status(
					wpa_s, data->tx_status.dst,
					data->tx_status.data,
					data->tx_status.data_len,
					data->tx_status.ack ?
					OFFCHANNEL_SEND_ACTION_SUCCESS :
					OFFCHANNEL_SEND_ACTION_NO_ACK);
#endif /* CONFIG_OFFCHANNEL */
			break;
		}
#endif /* CONFIG_AP */
#ifdef CONFIG_OFFCHANNEL
		wpa_dbg(wpa_s, MSG_DEBUG, "EVENT_TX_STATUS pending_dst="
			MACSTR, MAC2STR(wpa_s->p2pdev->pending_action_dst));
		/*
		 * Catch TX status events for Action frames we sent via group
		 * interface in GO mode, or via standalone AP interface.
		 * Note, wpa_s->p2pdev will be the same as wpa_s->parent,
		 * except when the primary interface is used as a GO interface
		 * (for drivers which do not have group interface concurrency)
		 */
		if (data->tx_status.type == WLAN_FC_TYPE_MGMT &&
		    data->tx_status.stype == WLAN_FC_STYPE_ACTION &&
		    os_memcmp(wpa_s->p2pdev->pending_action_dst,
			      data->tx_status.dst, ETH_ALEN) == 0) {
			offchannel_send_action_tx_status(
				wpa_s->p2pdev, data->tx_status.dst,
				data->tx_status.data,
				data->tx_status.data_len,
				data->tx_status.ack ?
				OFFCHANNEL_SEND_ACTION_SUCCESS :
				OFFCHANNEL_SEND_ACTION_NO_ACK);
			break;
		}
#endif /* CONFIG_OFFCHANNEL */
#ifdef CONFIG_AP
		switch (data->tx_status.type) {
		case WLAN_FC_TYPE_MGMT:
			ap_mgmt_tx_cb(wpa_s, data->tx_status.data,
				      data->tx_status.data_len,
				      data->tx_status.stype,
				      data->tx_status.ack);
			break;
		case WLAN_FC_TYPE_DATA:
			/*ap_tx_status(wpa_s, data->tx_status.dst,
				     data->tx_status.data,
				     data->tx_status.data_len,
				     data->tx_status.ack);*/
			break;
		}
#endif /* CONFIG_AP */
		break;
#ifdef CONFIG_AP
	case EVENT_EAPOL_TX_STATUS:
		/*ap_eapol_tx_status(wpa_s, data->eapol_tx_status.dst,
				   data->eapol_tx_status.data,
				   data->eapol_tx_status.data_len,
				   data->eapol_tx_status.ack);*/
		break;
	case EVENT_DRIVER_CLIENT_POLL_OK:
		wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_RX_FROM_UNKNOWN:
        if (wpa_s->ap_iface == NULL)
			break;
		ap_rx_from_unknown_sta(wpa_s, data->rx_from_unknown.addr,
				       data->rx_from_unknown.wds);
		break;
	case EVENT_CH_SWITCH:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
#ifdef NEED_AP_MLME
	case EVENT_DFS_RADAR_DETECTED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_DFS_CAC_STARTED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_DFS_CAC_FINISHED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_DFS_CAC_ABORTED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_DFS_NOP_FINISHED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
#endif /* NEED_AP_MLME */
#endif /* CONFIG_AP */
	case EVENT_RX_MGMT: {
		u16 fc, stype;
		const struct ieee80211_mgmt *mgmt;

#ifdef CONFIG_TESTING_OPTIONS
		if (wpa_s->ext_mgmt_frame_handling) {
			struct rx_mgmt *rx = &data->rx_mgmt;
			size_t hex_len = 2 * rx->frame_len + 1;
			char *hex = os_malloc(hex_len);
			if (hex) {
				wpa_snprintf_hex(hex, hex_len,
						 rx->frame, rx->frame_len);
				wpa_msg(wpa_s, MSG_INFO, "MGMT-RX freq=%d datarate=%u ssi_signal=%d %s",
					rx->freq, rx->datarate, rx->ssi_signal,
					hex);
				os_free(hex);
			}
			break;
		}
#endif /* CONFIG_TESTING_OPTIONS */

		mgmt = (const struct ieee80211_mgmt *)
			data->rx_mgmt.frame;
		fc = le_to_host16(mgmt->frame_control);

#if defined(CONFIG_P2P) || defined(CONFIG_IBSS_RSN) || defined(CONFIG_SAE) || defined(CONFIG_IEEE80211W)
		stype = WLAN_FC_GET_STYPE(fc);
#endif

#ifdef CONFIG_AP
		if (wpa_s->ap_iface == NULL) {
#endif /* CONFIG_AP */
#ifdef CONFIG_P2P
			if (stype == WLAN_FC_STYPE_PROBE_REQ &&
			    data->rx_mgmt.frame_len > IEEE80211_HDRLEN) {
				const u8 *src = mgmt->sa;
				const u8 *ie;
				size_t ie_len;

				ie = data->rx_mgmt.frame + IEEE80211_HDRLEN;
				ie_len = data->rx_mgmt.frame_len -
					IEEE80211_HDRLEN;
				wpas_p2p_probe_req_rx(
					wpa_s, src, mgmt->da,
					mgmt->bssid, ie, ie_len,
					data->rx_mgmt.freq,
					data->rx_mgmt.ssi_signal);
				break;
			}
#endif /* CONFIG_P2P */
#ifdef CONFIG_IBSS_RSN
			if (wpa_s->current_ssid &&
			    wpa_s->current_ssid->mode == WPAS_MODE_IBSS &&
			    stype == WLAN_FC_STYPE_AUTH &&
			    data->rx_mgmt.frame_len >= 30) {
				wpa_supplicant_event_ibss_auth(wpa_s, data);
				break;
			}
#endif /* CONFIG_IBSS_RSN */

#ifdef CONFIG_IEEE80211W
			if (stype == WLAN_FC_STYPE_ACTION) {
                #if 1
				wpas_event_rx_mgmt_action(
					wpa_s, data->rx_mgmt.frame,
					data->rx_mgmt.frame_len,
					data->rx_mgmt.freq,
					data->rx_mgmt.ssi_signal);
                #endif
				break;
			}

			//if (wpa_s->ifmsh) {
			//	mesh_mpm_mgmt_rx(wpa_s, &data->rx_mgmt);
			//	break;
			//}

			wpa_dbg(wpa_s, MSG_DEBUG, "AP: ignore received "
				"management frame in non-AP mode");
#endif       
#ifdef CONFIG_SAE
			if (stype == WLAN_FC_STYPE_AUTH &&
				!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME) &&
				(wpa_s->drv_flags & WPA_DRIVER_FLAGS_SAE)) {
				sme_external_auth_mgmt_rx(
					wpa_s, data->rx_mgmt.frame,
					data->rx_mgmt.frame_len);
				break;
			}
#endif /* CONFIG_SAE */

			break;
#ifdef CONFIG_AP
		}

#if 0
		if (stype == WLAN_FC_STYPE_PROBE_REQ &&
		    data->rx_mgmt.frame_len > IEEE80211_HDRLEN) {
			const u8 *ie;
			size_t ie_len;

			ie = data->rx_mgmt.frame + IEEE80211_HDRLEN;
			ie_len = data->rx_mgmt.frame_len - IEEE80211_HDRLEN;

			wpas_notify_preq(wpa_s, mgmt->sa, mgmt->da,
					 mgmt->bssid, ie, ie_len,
					 data->rx_mgmt.ssi_signal);
		}
#endif

		ap_mgmt_rx(wpa_s, &data->rx_mgmt);
#endif /* CONFIG_AP */
		break;
		}
	case EVENT_RX_PROBE_REQ:
#if 0        
		if (data->rx_probe_req.sa == NULL ||
		    data->rx_probe_req.ie == NULL)
			break;
#ifdef CONFIG_AP
		if (wpa_s->ap_iface) {
			hostapd_probe_req_rx(wpa_s->ap_iface->bss[0],
					     data->rx_probe_req.sa,
					     data->rx_probe_req.da,
					     data->rx_probe_req.bssid,
					     data->rx_probe_req.ie,
					     data->rx_probe_req.ie_len,
					     data->rx_probe_req.ssi_signal);
			break;
		}
#endif /* CONFIG_AP */
#ifdef CONFIG_P2P
		wpas_p2p_probe_req_rx(wpa_s, data->rx_probe_req.sa,
				      data->rx_probe_req.da,
				      data->rx_probe_req.bssid,
				      data->rx_probe_req.ie,
				      data->rx_probe_req.ie_len,
				      0,
				      data->rx_probe_req.ssi_signal);
#endif
#endif
		break;
	case EVENT_REMAIN_ON_CHANNEL:
#ifdef CONFIG_OFFCHANNEL
		offchannel_remain_on_channel_cb(
			wpa_s, data->remain_on_channel.freq,
			data->remain_on_channel.duration);
#endif /* CONFIG_OFFCHANNEL */
#ifdef CONFIG_P2P
		wpas_p2p_remain_on_channel_cb(
			wpa_s, data->remain_on_channel.freq,
			data->remain_on_channel.duration);
#endif
		break;
	case EVENT_CANCEL_REMAIN_ON_CHANNEL:
#ifdef CONFIG_OFFCHANNEL
		offchannel_cancel_remain_on_channel_cb(
			wpa_s, data->remain_on_channel.freq);
#endif /* CONFIG_OFFCHANNEL */
#ifdef CONFIG_P2P
		wpas_p2p_cancel_remain_on_channel_cb(
			wpa_s, data->remain_on_channel.freq);
#endif
		break;
	case EVENT_EAPOL_RX:
#if 0        
		wpa_supplicant_rx_eapol(wpa_s, data->eapol_rx.src,
					data->eapol_rx.data,
					data->eapol_rx.data_len);
#endif
		break;
	case EVENT_SIGNAL_CHANGE:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_INTERFACE_ENABLED:
		wpa_dbg(wpa_s, MSG_DEBUG, "Interface was enabled");
#if 0        
		if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED) {
			wpa_supplicant_update_mac_addr(wpa_s);
			//if (wpa_s->p2p_mgmt) {
			//	wpa_supplicant_set_state(wpa_s,
			//				 WPA_DISCONNECTED);
			//	break;
			//}

#ifdef CONFIG_AP
			if (!wpa_s->ap_iface) {
				wpa_supplicant_set_state(wpa_s,
							 WPA_DISCONNECTED);
				wpa_s->scan_req = NORMAL_SCAN_REQ;
				wpa_supplicant_req_scan(wpa_s, 0, 0);
			} else
				wpa_supplicant_set_state(wpa_s,
							 WPA_COMPLETED);
#else /* CONFIG_AP */
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
			wpa_supplicant_req_scan(wpa_s, 0, 0);
#endif /* CONFIG_AP */
		}
#endif        
		break;
	case EVENT_INTERFACE_DISABLED:
		wpa_dbg(wpa_s, MSG_DEBUG, "Interface was disabled");
#if 0        
#ifdef CONFIG_P2P
		if (wpa_s->p2p_group_interface == P2P_GROUP_INTERFACE_GO ||
		    (wpa_s->current_ssid && wpa_s->current_ssid->p2p_group &&
		     wpa_s->current_ssid->mode == WPAS_MODE_P2P_GO)) {
			/*
			 * Mark interface disabled if this happens to end up not
			 * being removed as a separate P2P group interface.
			 */
			wpa_supplicant_set_state(wpa_s, WPA_INTERFACE_DISABLED);
			/*
			 * The interface was externally disabled. Remove
			 * it assuming an external entity will start a
			 * new session if needed.
			 */
			if (wpa_s->current_ssid &&
			    wpa_s->current_ssid->p2p_group)
				wpas_p2p_interface_unavailable(wpa_s);
			else
				wpas_p2p_disconnect(wpa_s);
			/*
			 * wpa_s instance may have been freed, so must not use
			 * it here anymore.
			 */
			break;
		}
		if (wpa_s->p2p_scan_work && wpa_s->global->p2p &&
		    p2p_in_progress(wpa_s->global->p2p) > 1) {
			/* This radio work will be cancelled, so clear P2P
			 * state as well.
			 */
			p2p_stop_find(wpa_s->global->p2p);
		}
#endif /* CONFIG_P2P */

		if (wpa_s->wpa_state >= WPA_AUTHENTICATING) {
			/*
			 * Indicate disconnection to keep ctrl_iface events
			 * consistent.
			 */
			wpa_supplicant_event_disassoc(
				wpa_s, WLAN_REASON_DEAUTH_LEAVING, 1);
		}
		wpa_supplicant_mark_disassoc(wpa_s);
		wpa_bss_flush(wpa_s);
		radio_remove_works(wpa_s, NULL, 0);

		wpa_supplicant_set_state(wpa_s, WPA_INTERFACE_DISABLED);
#endif
		break;
	case EVENT_CHANNEL_LIST_CHANGED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_INTERFACE_UNAVAILABLE:
#ifdef CONFIG_P2P        
		wpas_p2p_interface_unavailable(wpa_s);
#endif
		break;
	case EVENT_BEST_CHANNEL:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
#ifdef CONFIG_IEEE80211W
	case EVENT_UNPROT_DEAUTH:
        //wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
        wpa_supplicant_event_unprot_deauth(wpa_s, (struct unprot_deauth *)data);
		break;
	case EVENT_UNPROT_DISASSOC:
        //wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
        wpa_supplicant_event_unprot_disassoc(wpa_s, (struct unprot_disassoc *)data);
		break;
#endif
	case EVENT_STATION_LOW_ACK:
#if 0
#ifdef CONFIG_AP
		if (wpa_s->ap_iface && data)
			hostapd_event_sta_low_ack(wpa_s->ap_iface->bss[0],
						  data->low_ack.addr);
#endif /* CONFIG_AP */
#ifdef CONFIG_TDLS
		if (data)
			wpa_tdls_disable_unreachable_link(wpa_s->wpa,
							  data->low_ack.addr);
#endif /* CONFIG_TDLS */
#endif
		break;
	case EVENT_IBSS_PEER_LOST:
#ifdef CONFIG_IBSS_RSN
		ibss_rsn_stop(wpa_s->ibss_rsn, data->ibss_peer_lost.peer);
#endif /* CONFIG_IBSS_RSN */
		break;
	case EVENT_DRIVER_GTK_REKEY:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_SCHED_SCAN_STOPPED:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_WPS_BUTTON_PUSHED:
#ifdef CONFIG_WPS
		wpas_wps_start_pbc(wpa_s, NULL, 0);
#endif /* CONFIG_WPS */
		break;
	case EVENT_AVOID_FREQUENCIES:
		//wpa_supplicant_notify_avoid_freq(wpa_s, data);
		break;
	case EVENT_CONNECT_FAILED_REASON:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_NEW_PEER_CANDIDATE:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_SURVEY:
        wpa_msg(wpa_s, MSG_INFO, "event %d not support!", event);
		break;
	case EVENT_ACS_CHANNEL_SELECTED:
#ifdef CONFIG_ACS
		if (!wpa_s->ap_iface)
			break;
		hostapd_acs_channel_selected(wpa_s->ap_iface->bss[0],
					     &data->acs_selected_channels);
#endif /* CONFIG_ACS */
		break;
	case EVENT_P2P_LO_STOP:
#ifdef CONFIG_P2P
		wpa_s->p2p_lo_started = 0;
		wpa_msg(wpa_s, MSG_INFO, P2P_EVENT_LISTEN_OFFLOAD_STOP
			P2P_LISTEN_OFFLOAD_STOP_REASON "reason=%d",
			data->p2p_lo_stop.reason_code);
#endif /* CONFIG_P2P */
		break;
		case EVENT_EXTERNAL_AUTH:
#ifdef CONFIG_SAE
		if (!wpa_s->current_ssid) {
			wpa_printf(MSG_DEBUG, "SAE: current_ssid is NULL");
			break;
		}
		sme_external_auth_trigger(wpa_s, data);
#endif /* CONFIG_SAE */
		break;
	default:
		wpa_msg(wpa_s, MSG_INFO, "Unknown event %d", event);
		break;
	}
}


void wpa_supplicant_event_global(void *ctx, enum wpa_event_type event,
				 union wpa_event_data *data)
{
	struct wpa_supplicant *wpa_s;

	if (event != EVENT_INTERFACE_STATUS)
		return;

	wpa_s = wpa_supplicant_get_iface(ctx, data->interface_status.ifname);
	if (wpa_s && wpa_s->driver->get_ifindex) {
		unsigned int ifindex;

		ifindex = wpa_s->driver->get_ifindex(wpa_s->drv_priv);
		if (ifindex != data->interface_status.ifindex) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"interface status ifindex %d mismatch (%d)",
				ifindex, data->interface_status.ifindex);
			return;
		}
	}
#ifdef CONFIG_MATCH_IFACE
	else if (data->interface_status.ievent == EVENT_INTERFACE_ADDED) {
		struct wpa_interface *wpa_i;

		wpa_i = wpa_supplicant_match_iface(
			ctx, data->interface_status.ifname);
		if (!wpa_i)
			return;
		wpa_s = wpa_supplicant_add_iface(ctx, wpa_i, NULL);
		os_free(wpa_i);
		if (wpa_s)
			wpa_s->matched = 1;
	}
#endif /* CONFIG_MATCH_IFACE */

	if (wpa_s)
		wpa_supplicant_event(wpa_s, event, data);
}
