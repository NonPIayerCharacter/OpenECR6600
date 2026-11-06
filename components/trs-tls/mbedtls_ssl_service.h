#ifndef __MBEDTLS_SSL_SERVICE_H__
#define __MBEDTLS_SSL_SERVICE_H__

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/mbedtls_config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/debug.h"

typedef struct {
    int family;
    /* MBEDTLS_NET_PROTO_UDP or MBEDTLS_NET_PROTO_TCP */
    int protocols;
    int sockfd;
    /* ssl client connect to host service */
    char *hostCNname;
    unsigned int port;
    unsigned int timeoutms;

#define SSL_SERVICE_NOAUTH 0
#define SSL_SERVICE_SERVER_AUTH 1
#define SSL_SERVICE_CLIENT_AUTH 2
#define SSL_SERVICE_BOTH_AUTH 3
    unsigned int authmode;
#define SSL_SERVICE_CERT_MASK 0x1
#define SSL_SERVICE_PRIVKEY_MASK 0x2
#define SSL_SERVICE_CA_MASK 0x4
    unsigned int pkinumber;
    unsigned int canumber;

    char commonName[65];
    char sslSni[65];
    char alpn[256];
    char *palpn[6];
    unsigned int psklen;
    char sslpsk[33];
    char pskhint[33];

    /* client or server sslctx */
    void *ssl;
} ssl_service_cfg_t;

typedef struct {
    mbedtls_net_context netctx;
    mbedtls_ssl_context sslctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config sslcfg;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt svrcert;
    mbedtls_pk_context pkey;
    void *parenet;
} ssl_service_client_t;

typedef struct {
    ssl_service_client_t *sslclient;
    mbedtls_net_context listenctx;
} ssl_service_server_t;

#define SSL_SERVICE_CONDITION_RETURN(condition, ret) \
    {\
        if ((condition) != 0) { \
            os_printf(LM_APP, LL_ERR, "%s[%d] failed with ret:%d\n", __FUNCTION__, __LINE__, ret); \
            return (ret);\
        }\
    }

#define SSL_SERVICE_DEBUG(fmt, ...) os_printf(LM_APP, LL_DBG, fmt, ##__VA_ARGS__)
#define SSL_SERVICE_INFO(fmt, ...) os_printf(LM_APP, LL_INFO, fmt, ##__VA_ARGS__)
#define SSL_SERVICE_ERR(fmt, ...) os_printf(LM_APP, LL_ERR, fmt, ##__VA_ARGS__)

void *ssl_service_sclient_get(ssl_service_cfg_t *sslcfg);
int ssl_service_server_create(ssl_service_cfg_t *sslcfg);
int ssl_service_server_get_fd(ssl_service_cfg_t *sslcfg);
int ssl_service_server_accept(ssl_service_cfg_t *sslcfg);
int ssl_service_server_destory(ssl_service_cfg_t *sslcfg);
int ssl_service_client_create(ssl_service_cfg_t *sslcfg);
int ssl_service_client_close(ssl_service_client_t *client);
int ssl_service_client_pki_check(unsigned int authmode, unsigned int pkinumber, unsigned int canumber, unsigned int mask);
int ssl_service_get_bytes_avail(ssl_service_client_t *client);
/* WANT_READ/WANT_WRITE are transient; zero indicates peer EOF. */
int ssl_service_net_read(ssl_service_client_t *client, unsigned char *data, unsigned int datalen);
int ssl_service_net_write(ssl_service_client_t *client, unsigned char *data, int datalen);

#endif
