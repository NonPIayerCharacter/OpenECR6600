#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "system_wifi.h"
#include "mesh_net_cfg.h"
//#include "mesh_utils.h"

#define AP_NET_NAME_LEN_MAX 32
#define AP_NET_INFO_LEN_MAX 64
#define AP_NET_SERVER_PORT 33333

struct _ap_net_cfg;
static struct _ap_net_cfg *apnet_get_config(void);

#define AP_NET_CHECK_FAILED_RETURN(x) \
    if ((x) != 0) { \
        os_printf(LM_APP, LL_ERR, "%s[%d] failed %d with errno %d\n", __FUNCTION__, __LINE__, (x), errno); \
        return (x); \
    }

typedef struct _ap_net_cfg{
    int running;
    int sockfd;
    int acceptfd;
    unsigned char fssid[AP_NET_INFO_LEN_MAX];
    unsigned char tssid[AP_NET_INFO_LEN_MAX];
    unsigned char key[AP_NET_INFO_LEN_MAX];
    unsigned char timestamp[AP_NET_INFO_LEN_MAX];
    mesh_cfg_notity_handler_t handler;
} ap_net_cfg;

ap_net_cfg g_apnetcfg = {
    .running = 0,
    .sockfd = -1,
    .acceptfd = -1,
};
#if 0
static void apnet_ap_default(void)
{
    wifi_config_u wifiCfg = {0};
    wifi_work_mode_e wifi_mode = WIFI_MODE_AP;

    wifi_set_opmode(wifi_mode);
    wifiCfg.ap.channel = WIFI_CHANNEL_1;
    wifiCfg.ap.authmode = AUTH_WPA_WPA2_PSK;
    snprintf((char *)wifiCfg.ap.ssid, sizeof(wifiCfg.ap.ssid), "%s", "ap_ssid");
    snprintf((char *)wifiCfg.ap.password, sizeof(wifiCfg.ap.password), "%s", "12345678");
    wifi_stop_softap();
    wifi_start_softap(&wifiCfg);
    os_msleep(2000);
}
#endif

extern int8_t mesh_hex2num(char c);

static ap_net_cfg *apnet_get_config(void)
{
    return &g_apnetcfg;
}

static int apnet_wait_connect(ap_net_cfg *cfg)
{
    struct sockaddr_storage raddr = {0};
    struct sockaddr_in *v4addr = (struct sockaddr_in *)&raddr;
    socklen_t addr_len = sizeof(struct sockaddr);
    int ret = -1;

    while (cfg->running) {        
        struct timeval timeout = {2, 0};
        fd_set fdread;

        FD_ZERO(&fdread);
        FD_SET(cfg->sockfd, &fdread);

        ret = select(cfg->sockfd + 1, &fdread, NULL, NULL, &timeout);
        if (ret > 0) {
            int acceptfd = accept(cfg->sockfd, (struct sockaddr *)v4addr, &addr_len);
            AP_NET_CHECK_FAILED_RETURN(acceptfd < 0);

            os_printf(LM_APP, LL_ERR, "------------------------------------------------------------\n");
            os_printf(LM_APP, LL_ERR, "accept %s port %d\n", inet_ntoa(v4addr->sin_addr), htons(v4addr->sin_port));
            os_printf(LM_APP, LL_ERR, "------------------------------------------------------------\n");
            ret = acceptfd;
            break;
        }
    }

    return ret;
}

static int apnet_read_network(int fd, unsigned char *buf, int count)
{
    unsigned int recv_timeout = 2000;
    int readlen;
    int nleft = count;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    while (nleft > 0) {
        readlen = read(fd, buf, nleft);
        if (readlen < 0) {
            return readlen;
        }

        if (readlen == 0) {
            return -1;
        }
 
        nleft -= readlen;
        buf += readlen;
    }

    return count - nleft;
}

static int apnet_start_service(ap_net_cfg *cfg)
{
    struct sockaddr_storage saddr = {0};
    struct sockaddr_in *v4addr = (struct sockaddr_in *)&saddr;
    int ret;
    unsigned int ipinfo;
    int opt = 1;
    unsigned char fssid[6];
    unsigned char tssid[6];
    unsigned int timestamp;
    int i;

    cfg->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    AP_NET_CHECK_FAILED_RETURN(cfg->sockfd < 0);

    ret = setsockopt(cfg->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    AP_NET_CHECK_FAILED_RETURN(ret);

    v4addr->sin_family = AF_INET;
    v4addr->sin_port = htons(AP_NET_SERVER_PORT);
    ret = wifi_get_ip_addr(SOFTAP_IF, &ipinfo);
    AP_NET_CHECK_FAILED_RETURN(ret);
    v4addr->sin_addr.s_addr = ipinfo;

    ret = bind(cfg->sockfd, (struct sockaddr *)v4addr, sizeof(struct sockaddr_in));
    AP_NET_CHECK_FAILED_RETURN(ret);

    ret = listen(cfg->sockfd, 1);
    AP_NET_CHECK_FAILED_RETURN(ret);

    os_printf(LM_APP, LL_ERR, "ap net linsten on %d\n", AP_NET_SERVER_PORT);
    cfg->acceptfd = apnet_wait_connect(cfg);
    AP_NET_CHECK_FAILED_RETURN(cfg->acceptfd < 0);

    while (cfg->running) {
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 50};
        fd_set read_set;

        FD_ZERO(&read_set);
        FD_SET(cfg->acceptfd, &read_set);
        ret = select(cfg->acceptfd + 1, &read_set, NULL, NULL, &timeout);
        if (ret > 0 && FD_ISSET(cfg->acceptfd, &read_set)) {
            unsigned int offset = 0;
            unsigned int len = 0;
            unsigned char buff[32] = {0};
            int idx;

            for (idx = 0; idx < 32; idx++) {
                ret = apnet_read_network(cfg->acceptfd, buff + idx, 1);
                AP_NET_CHECK_FAILED_RETURN(ret != 1);

                if (buff[idx] == '-') {
                    buff[idx] = '\0';
                    len = atoi((char *)buff);
                    break;
                }
            }
            AP_NET_CHECK_FAILED_RETURN(idx == 32);

            for (idx = 0; idx < sizeof(cfg->fssid); idx++) {
                ret = apnet_read_network(cfg->acceptfd, cfg->fssid + idx, 1);
                AP_NET_CHECK_FAILED_RETURN(ret != 1);

                if (cfg->fssid[idx] == '-') {
                    cfg->fssid[idx] = '\0';
                    break;
                }
            }
            AP_NET_CHECK_FAILED_RETURN(idx == sizeof(cfg->fssid));
            offset += strlen((char *)cfg->fssid);

            for (idx = 0; idx < sizeof(cfg->tssid); idx++) {
                ret = apnet_read_network(cfg->acceptfd, cfg->tssid + idx, 1);
                AP_NET_CHECK_FAILED_RETURN(ret != 1);

                if (cfg->tssid[idx] == '-') {
                    cfg->tssid[idx] = '\0';
                    break;
                }
            }
            AP_NET_CHECK_FAILED_RETURN(idx == sizeof(cfg->tssid));
            offset += strlen((char *)cfg->tssid);

            for (idx = 0; idx < sizeof(cfg->timestamp); idx++) {
                ret = apnet_read_network(cfg->acceptfd, cfg->timestamp + idx, 1);
                AP_NET_CHECK_FAILED_RETURN(ret != 1);

                if (cfg->timestamp[idx] == '-') {
                    cfg->timestamp[idx] = '\0';
                    break;
                }
            }
            AP_NET_CHECK_FAILED_RETURN(idx == sizeof(cfg->timestamp));
            offset += strlen((char *)cfg->timestamp);

            memset(cfg->key, 0, sizeof(cfg->key));
            AP_NET_CHECK_FAILED_RETURN((len - offset) >= sizeof(cfg->key));
            ret = apnet_read_network(cfg->acceptfd, cfg->key, len - offset);
            AP_NET_CHECK_FAILED_RETURN(ret != (len - offset));
            if (strncmp((char *)cfg->key, "NONE", strlen("NONE")) == 0)
            {
                memset(cfg->key, 0, sizeof(cfg->key));
            }
            os_printf(LM_APP, LL_ERR, "len %d\n", len);
            os_printf(LM_APP, LL_ERR, "fssid %s\n", cfg->fssid);
            os_printf(LM_APP, LL_ERR, "tssid %s\n", cfg->tssid);
            os_printf(LM_APP, LL_ERR, "key %s\n", cfg->key);
            os_printf(LM_APP, LL_ERR, "timestamp %s\n", cfg->timestamp);

            if (strncmp((char *)cfg->fssid, "ALL", strlen("ALL")) == 0) {
                memset(fssid, 0xff, sizeof(fssid));
            } else if (strncmp((char *)cfg->fssid, "NOWIFI", strlen("NOWIFI")) == 0) {
                memset(fssid, 0, sizeof(fssid));
            } else {
                for (i = 0; i < 6; i++)
                {
                    fssid[i] = mesh_hex2num(cfg->fssid[i*3]) * 0x10 + mesh_hex2num(cfg->fssid[i*3+1]);
                }
            }

            for (i = 0; i < 6; i++)
            {
                tssid[i] = mesh_hex2num(cfg->tssid[i*3]) * 0x10 + mesh_hex2num(cfg->tssid[i*3+1]);
            }

            timestamp = atoi((char *)cfg->timestamp);

            if (cfg->handler) {
                cfg->handler(fssid, tssid, cfg->key,timestamp);
            }
            break;
        }
    }

    return 0;
}

void apnet_stop_network(void)
{
    ap_net_cfg *cfg = apnet_get_config();

    cfg->running = 0;
    cfg->handler = NULL;
}

int apnet_service_main(void *param)
{
    ap_net_cfg *cfg = apnet_get_config();
 
    if (cfg->running == 1) {
        os_printf(LM_APP, LL_ERR, "ap net service already running\n");
        return -1;
    }

    //apnet_ap_default();
    os_printf(LM_APP, LL_ERR, "start ap net service\n");
    cfg->running = 1;

    while (cfg->running) {
        apnet_start_service(cfg);

        if (cfg->acceptfd != -1) {
            close(cfg->acceptfd);
            cfg->acceptfd = -1;
        }

        if (cfg->sockfd != -1) {
            close(cfg->sockfd);
            cfg->sockfd = -1;
        }
    }

    os_printf(LM_APP, LL_ERR, "exit ap net service\n");

    return 0;
}

int apnet_cfg_notity_register_handler(mesh_cfg_notity_handler_t handler)
{
    ap_net_cfg *cfg = apnet_get_config();
    cfg->handler = handler;

    return 0;
}

void apnet_cfg_init(void)
{
    ap_net_cfg *cfg = apnet_get_config();

    memset(cfg, 0, sizeof(ap_net_cfg));
    cfg->running = 0;
    cfg->sockfd = -1;
    cfg->acceptfd = -1;
}

void apnet_cfg_deinit(void)
{
    apnet_stop_network();
}

