#include "mesh_common.h"
#include "ieee802_11_common.h"

const unsigned char *mesh_find_ie(unsigned char eid, const unsigned char *ies, int len)
{
    while (len > 2 && ies[0] != eid) {
        len -= ies[1] + 2;
        ies += ies[1] + 2;
    }

    if (len < 2) {
        return NULL;
    }

    if (len < 2 + ies[1]) {
        return NULL;
    }

    return ies;
}

int mesh_get_ssid(unsigned char *beacon_frame, unsigned short frame_len, unsigned char *ssid)
{
    uint16_t ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);//same as u.probe_resp.variable
    const uint8_t *ptr = mesh_find_ie(WLAN_EID_SSID,
                                          beacon_frame + ieoffset, frame_len - ieoffset);
    if (ptr) {
        uint8_t ssid_len = ptr[1];
        if (ssid_len <= 32) {    /* ssid 32 octets at most */
            memcpy(ssid, ptr + 2, ssid_len);/* eating EID & len */
            ssid[ssid_len] = '\0';
            return 0;
        }
    }

    return -1;
}

int mesh_get_bss_channel(unsigned char *beacon_frame, unsigned short frame_len)
{
    uint16_t ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);//same as u.probe_resp.variable
    const unsigned char *ie = beacon_frame + ieoffset;
    unsigned short ielen = frame_len - ieoffset;
    const unsigned char *tmp;
    int channel_number = 0;

    tmp = mesh_find_ie(WLAN_EID_DS_PARAMS, ie, ielen);
    if (tmp && tmp[1] == 1) {
        channel_number = tmp[2];
    }

    tmp = mesh_find_ie(WLAN_EID_HT_OPERATION, ie, ielen);
    if (tmp && tmp[1] >= sizeof(struct ieee80211_ht_operation)) {
        struct ieee80211_ht_operation *htop = (void *)(tmp + 2);
        if(channel_number >= 1 && channel_number <= 14) {
            channel_number = htop->primary_chan;
        }
    }

    tmp = mesh_find_ie(WLAN_EID_CHANNEL_SWITCH, ie, ielen);
    if (tmp && tmp[1] >= 3) {
        if(channel_number >= 1 && channel_number <= 14) {
            channel_number = tmp[3];
        }
    }


    return channel_number;
}

int mesh_get_bssid(unsigned char *in, unsigned char *mac)
{
    const unsigned char *bssid = get_hdr_bssid((struct ieee80211_hdr *)in, sizeof(struct ieee80211_hdr));

    if (bssid)
        memcpy(mac, bssid, ETH_ALEN);
    else
        return -1;

    return 0;
}

const unsigned char *mesh_find_vendor_ie(const unsigned char *oui, unsigned char oui_type, const unsigned char *ies, int len)
{
    mesh_vendor_ie_t *ie;
    const unsigned char *pos = ies, *end = ies + len;

    while (pos < end) {
        pos = mesh_find_ie(MESH_EID_VENDOR_SPECIFIC, pos,
                               end - pos);
        if (!pos)
            return NULL;

        ie = (mesh_vendor_ie_t *)pos;
        if (ie->len < sizeof(*ie))
            goto cont;

        if (memcmp(ie->oui, oui, MESH_OUI_LEN) == 0 && ie->oui_type == oui_type)
            return pos;
cont:
        pos += 2 + ie->len;
    }

    return NULL;
}

char mesh_average_rssi(mesh_rssi_record_t *record, char new)
{
    record->sum = record->sum - record->rssi[record->idx] + new;
    record->rssi[record->idx] = new;

    if (record->idx == (MESH_MAX_RSSI_RECORD_NUM - 1)) {
        record->full = 1;
    }
    record->idx = (record->idx + 1) % MESH_MAX_RSSI_RECORD_NUM;

    record->average = record->sum / (record->full ? MESH_MAX_RSSI_RECORD_NUM : record->idx);

    return record->average;
}

void mesh_dump_hex(unsigned char *data, int len, int tab_num)
{
    int i;

    os_printf(LM_APP, LL_INFO, "%04x ",0);
    for (i = 0; i < len; i++) {
        os_printf(LM_APP, LL_INFO, "%02x ", data[i]);

        if (!((i + 1) % tab_num))
            os_printf(LM_APP, LL_INFO, "\n%04x ", (i + 1)) ;
    }

    os_printf(LM_APP, LL_INFO, "\n");
}

void dump_mac(char *str, unsigned char *mac)
{
    mesh_i("%s: %02x:%02x:%02x:%02x:%02x:%02x",
            str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *mesh_state_str[MESH_STATE_MAX]={
    "INIT",
    "CFG",
    "FIND",
    "FAST_ROOT",
    "VOTE",
    "ELECTED_ROOT",
    "JOIN",
    "MOVE",
    "CONNETED",
};

const char *mesh_state_txt_get(int state)
{
    if (state >= MESH_STATE_MAX) {
        return "UNKNOWN";
    }

    return mesh_state_str[state];
}

int8_t mesh_hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


