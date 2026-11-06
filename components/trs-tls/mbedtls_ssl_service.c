#include "mbedtls_ssl_service.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "easyflash.h"

#if defined(MBEDTLS_DEBUG_C)
static void ssl_service_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    os_printf(LM_APP, LL_DBG, "mbedtls: %s:%d %s\n", file, line, str);
}
#endif

static int ssl_service_sclient_init(ssl_service_cfg_t *sslcfg)
{
    int ret, vlen;
    char sslbuff[2048] = {0};
    MFG_KEY_INFO_T key = {0};
    ssl_service_server_t *sslserver = (ssl_service_server_t *)sslcfg->ssl;

    mbedtls_net_init(&sslserver->sslclient->netctx);
    mbedtls_pk_init(&sslserver->sslclient->pkey);
    mbedtls_ssl_init(&sslserver->sslclient->sslctx);
    mbedtls_ctr_drbg_init(&sslserver->sslclient->drbg);
    mbedtls_ssl_config_init(&sslserver->sslclient->sslcfg);
    mbedtls_entropy_init(&sslserver->sslclient->entropy);
    mbedtls_x509_crt_init(&sslserver->sslclient->cacert);
    mbedtls_x509_crt_init(&sslserver->sslclient->svrcert);
#if defined(MBEDTLS_DEBUG_C)
    mbedtls_debug_set_threshold(3);
    mbedtls_ssl_conf_dbg(&sslserver->sslclient->sslcfg, ssl_service_debug, NULL);
#endif
    ret = mbedtls_ctr_drbg_seed(&sslserver->sslclient->drbg, mbedtls_entropy_func, &sslserver->sslclient->entropy, NULL, 0);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    ret = mbedtls_ssl_config_defaults(&sslserver->sslclient->sslcfg, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    mbedtls_ssl_conf_rng(&sslserver->sslclient->sslcfg, mbedtls_ctr_drbg_random, &sslserver->sslclient->drbg);

    if (sslcfg->timeoutms != 0) {
        mbedtls_ssl_conf_read_timeout(&sslserver->sslclient->sslcfg, sslcfg->timeoutms);
        mbedtls_ssl_set_bio(&sslserver->sslclient->sslctx, &sslserver->sslclient->netctx, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    } else {
        mbedtls_ssl_set_bio(&sslserver->sslclient->sslctx, &sslserver->sslclient->netctx, mbedtls_net_send, mbedtls_net_recv, NULL);
    }

    mbedtls_ssl_conf_authmode(&sslserver->sslclient->sslcfg, MBEDTLS_SSL_VERIFY_NONE);
    if (sslcfg->authmode == SSL_SERVICE_SERVER_AUTH) {
        memset(sslbuff, 0, sizeof(sslbuff));
        key.name_space = "server_ca";
        key.name_key = "server_ca";
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        ret = mbedtls_x509_crt_parse(&sslserver->sslclient->cacert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
        mbedtls_ssl_conf_ca_chain(&sslserver->sslclient->sslcfg, &sslserver->sslclient->cacert, NULL);
        mbedtls_ssl_conf_authmode(&sslserver->sslclient->sslcfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    }

    memset(sslbuff, 0, sizeof(sslbuff));
    key.name_space = "server_cert";
    key.name_key = "server_cert";
    key.type = MFG_KEY_TYPE_BINARY;
    ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
    ret = mbedtls_x509_crt_parse(&sslserver->sslclient->svrcert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    memset(sslbuff, 0, sizeof(sslbuff));
    key.name_space = "server_key";
    key.name_key = "server_key";
    key.type = MFG_KEY_TYPE_BINARY;
    ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init( &ctr_drbg );
    ret = mbedtls_pk_parse_key(&sslserver->sslclient->pkey, (const unsigned char *)sslbuff, strlen(sslbuff) + 1, NULL, 0,mbedtls_ctr_drbg_random, &ctr_drbg);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    ret = mbedtls_ssl_conf_own_cert(&sslserver->sslclient->sslcfg, &sslserver->sslclient->svrcert, &sslserver->sslclient->pkey);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    ret = mbedtls_ssl_setup(&sslserver->sslclient->sslctx, &sslserver->sslclient->sslcfg);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    return 0;
}

void *ssl_service_sclient_get(ssl_service_cfg_t *sslcfg)
{
    ssl_service_server_t *server = (ssl_service_server_t *)sslcfg->ssl;
    SSL_SERVICE_CONDITION_RETURN(server == NULL, NULL);
    ssl_service_client_t *sclient = server->sslclient;
    SSL_SERVICE_CONDITION_RETURN(sclient == NULL, NULL);

    return sclient;
}

int ssl_service_server_accept(ssl_service_cfg_t *sslcfg)
{
    ssl_service_server_t *server = (ssl_service_server_t *)sslcfg->ssl;
    int ret;

    SSL_SERVICE_CONDITION_RETURN(server == NULL, -1);
    server->sslclient = os_zalloc(sizeof(ssl_service_client_t));
    SSL_SERVICE_CONDITION_RETURN(server->sslclient == NULL, -1);
    ssl_service_sclient_init(sslcfg);
    ret = mbedtls_net_accept(&server->listenctx, &server->sslclient->netctx, NULL, 0, NULL);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);

    do {
        ret = mbedtls_ssl_handshake(&server->sslclient->sslctx);
        SSL_SERVICE_CONDITION_RETURN(ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE, ret);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    if((ret = mbedtls_ssl_get_verify_result(&server->sslclient->sslctx)) != 0) {
        char vrfy_buf[128];
        int flags = mbedtls_ssl_get_verify_result(&server->sslclient->sslctx);
        mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ), "  ! ", flags );
    }
    sslcfg->sockfd = server->sslclient->netctx.fd;

    return 0;
}

int ssl_service_server_create(ssl_service_cfg_t *sslcfg)
{
    ssl_service_server_t *server = NULL;
    char serverport[12] = {0};
    int ret;

    snprintf(serverport, sizeof(serverport), "%d", sslcfg->port);
    server = (ssl_service_server_t *)os_zalloc(sizeof(ssl_service_server_t));
    SSL_SERVICE_CONDITION_RETURN(server == NULL, -1);
    mbedtls_net_init(&server->listenctx);
    sslcfg->ssl = server;

    os_printf(LM_APP, LL_DBG, "%s[%d] serverport %s protocol %d family %d\n", __FUNCTION__, __LINE__, serverport, sslcfg->protocols, sslcfg->family);
    ret = mbedtls_net_bind(&server->listenctx, NULL, (const char *)serverport, sslcfg->protocols, sslcfg->family);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    sslcfg->sockfd = server->listenctx.fd;
    return 0;
}

int ssl_service_server_destory(ssl_service_cfg_t *sslcfg)
{
    ssl_service_server_t *server = (ssl_service_server_t *)sslcfg->ssl;

    SSL_SERVICE_CONDITION_RETURN(server == NULL, -1);
    mbedtls_net_free(&server->listenctx);
    os_free(server);
    sslcfg->ssl = NULL;

    return 0;
}

static int ssl_service_client_ca_check(unsigned int authmode, unsigned int canumber, ssl_service_client_t *client)
{
    mbedtls_x509_crt_init(&client->cacert);
    mbedtls_x509_crt_init(&client->svrcert);
    mbedtls_pk_init(&(client->pkey));

    if (authmode == SSL_SERVICE_CLIENT_AUTH || authmode == SSL_SERVICE_BOTH_AUTH) {
        int ret;
        char nvkey[32] = {0};
        char sslbuff[2048] = {0};
        MFG_KEY_INFO_T key = {0};
        int vlen;
        /* 加载CA证书 */
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_ca.%d", canumber);
        key.name_space = "client_ca";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        ret = mbedtls_x509_crt_parse(&client->cacert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    return 0;
}

static int ssl_service_client_cert_check(unsigned int authmode, unsigned int pkinumber, ssl_service_client_t *client)
{
    mbedtls_x509_crt_init(&client->cacert);
    mbedtls_x509_crt_init(&client->svrcert);
    mbedtls_pk_init(&(client->pkey));

    if (authmode == SSL_SERVICE_SERVER_AUTH || authmode == SSL_SERVICE_BOTH_AUTH) {
        int ret;
        char nvkey[32] = {0};
        char sslbuff[2048] = {0};
        MFG_KEY_INFO_T key = {0};
        int vlen;
        /* 加载证书 */
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_cert.%d", pkinumber);
        key.name_space = "client_cert";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        ret = mbedtls_x509_crt_parse(&client->svrcert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    return 0;
}

static int ssl_service_client_privkey_check(unsigned int authmode, unsigned int pkinumber, ssl_service_client_t *client)
{
    mbedtls_x509_crt_init(&client->cacert);
    mbedtls_x509_crt_init(&client->svrcert);
    mbedtls_pk_init(&(client->pkey));

    if (authmode == SSL_SERVICE_SERVER_AUTH || authmode == SSL_SERVICE_BOTH_AUTH) {
        int ret;
        char sslbuff[2048] = {0};
        char nvkey[32] = {0};
        MFG_KEY_INFO_T key = {0};
        int vlen;
        /* 加载密钥和密码 */
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_key.%d", pkinumber);
        key.name_space = "client_key";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ctr_drbg_init( &ctr_drbg );
        ret = mbedtls_pk_parse_key(&client->pkey, (const unsigned char *)sslbuff, strlen(sslbuff) + 1, NULL, 0,mbedtls_ctr_drbg_random, &ctr_drbg);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    return 0;
}

int ssl_service_client_pki_check(unsigned int authmode, unsigned int pkinumber, unsigned int canumber, unsigned int mask)
{
    ssl_service_client_t *client = NULL;
    int ret = 0;

    if (authmode == SSL_SERVICE_NOAUTH) {
        return 0;
    }

    client = (ssl_service_client_t *)os_malloc(sizeof(ssl_service_client_t));
    SSL_SERVICE_CONDITION_RETURN(client == NULL, -1);
    memset(client, 0, sizeof(ssl_service_client_t));

    switch (mask) {
        case SSL_SERVICE_CERT_MASK:
            ret = ssl_service_client_cert_check(authmode, pkinumber, client);
            break;
        case SSL_SERVICE_PRIVKEY_MASK:
            ret = ssl_service_client_privkey_check(authmode, pkinumber, client);
            break;
        case SSL_SERVICE_CA_MASK:
            ret = ssl_service_client_ca_check(authmode, canumber, client);
        default:
            break;
    }

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_free(&client->cacert);
    mbedtls_x509_crt_free(&client->svrcert);
    mbedtls_pk_free(&client->pkey);
#endif
    os_free(client);

    return ret;
}

int ssl_service_client_init(ssl_service_client_t *client, ssl_service_cfg_t *sslcfg)
{
    int ret;
    char sslbuff[2048] = {0};
    char nvkey[32] = {0};
    MFG_KEY_INFO_T key = {0};
    int vlen;

#ifdef MBEDTLS_SSL_PROTO_TLS1_3
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        os_printf(LM_APP, LL_INFO, "Failed to initialize PSA crypto, returned %d", (int) status);
        return -1;
    }
#endif // MBEDTLS_SSL_PROTO_TLS1_3

    mbedtls_net_init(&client->netctx);
    mbedtls_ssl_init(&client->sslctx);
    mbedtls_ctr_drbg_init(&client->drbg);
    mbedtls_ssl_config_init(&client->sslcfg);
    mbedtls_entropy_init(&client->entropy);
    mbedtls_x509_crt_init(&client->cacert);
    mbedtls_x509_crt_init(&client->svrcert);
    mbedtls_pk_init(&(client->pkey));
    sslcfg->ssl = client;

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_debug_set_threshold(3);
    mbedtls_ssl_conf_dbg(&client->sslcfg, ssl_service_debug, NULL);
#endif
    ret = mbedtls_ctr_drbg_seed(&client->drbg, mbedtls_entropy_func, &client->entropy, NULL, 0);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    if (sslcfg->protocols == MBEDTLS_SSL_TRANSPORT_DATAGRAM) {
        ret = mbedtls_ssl_config_defaults(&client->sslcfg, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
    } else {
        ret = mbedtls_ssl_config_defaults(&client->sslcfg, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    }
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    mbedtls_ssl_conf_rng(&client->sslcfg, mbedtls_ctr_drbg_random, &client->drbg);
#ifdef CONFIG_MBEDTLS_SSL_ALPN
    if (sslcfg->palpn[0] != NULL) {
        mbedtls_ssl_conf_alpn_protocols(&client->sslcfg, (const char **)sslcfg->palpn);
    }
#endif

#if defined(MBEDTLS_SSL_HANDSHAKE_WITH_PSK_ENABLED)
    if (sslcfg->psklen != 0) {
        ret = mbedtls_ssl_conf_psk(&client->sslcfg, (const unsigned char *)sslcfg->sslpsk, sslcfg->psklen,
                                   (const unsigned char *)sslcfg->pskhint, strlen(sslcfg->pskhint));
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }
#endif

    mbedtls_ssl_conf_authmode(&client->sslcfg, MBEDTLS_SSL_VERIFY_NONE);
    if (sslcfg->authmode == SSL_SERVICE_CLIENT_AUTH || sslcfg->authmode == SSL_SERVICE_BOTH_AUTH) {
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_ca.%d", sslcfg->canumber);
        key.name_space = "client_ca";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        ret = mbedtls_x509_crt_parse(&client->cacert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
        mbedtls_ssl_conf_ca_chain(&client->sslcfg, &client->cacert, NULL);
        mbedtls_ssl_conf_authmode(&client->sslcfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    }

    if (sslcfg->authmode == SSL_SERVICE_SERVER_AUTH || sslcfg->authmode == SSL_SERVICE_BOTH_AUTH) {
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_cert.%d", sslcfg->pkinumber);
        key.name_space = "client_cert";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        ret = mbedtls_x509_crt_parse(&client->svrcert, (const unsigned char *)sslbuff, strlen(sslbuff) + 1);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

        /* 加载密钥和密码 */
        memset(sslbuff, 0, sizeof(sslbuff));
        snprintf(nvkey, sizeof(nvkey), "client_key.%d", sslcfg->pkinumber);
        key.name_space = "client_key";
        key.name_key = nvkey;
        key.type = MFG_KEY_TYPE_BINARY;
        ret = mcfg_get_env_blob(&key, sslbuff, sizeof(sslbuff), &vlen);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, -1);
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ctr_drbg_init( &ctr_drbg );
        ret = mbedtls_pk_parse_key(&client->pkey, (const unsigned char *)sslbuff, strlen(sslbuff) + 1, NULL, 0,mbedtls_ctr_drbg_random, &ctr_drbg);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
        ret = mbedtls_ssl_conf_own_cert(&client->sslcfg, &client->svrcert, &client->pkey);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    if (strlen(sslcfg->commonName) != 0) {
        ret = mbedtls_ssl_set_hostname(&client->sslctx, sslcfg->commonName);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    if (strlen(sslcfg->sslSni) != 0) {
        ret = mbedtls_ssl_set_hostname(&client->sslctx, sslcfg->sslSni);
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }

    ret = mbedtls_ssl_setup(&client->sslctx, &client->sslcfg);
    SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);

    if (sslcfg->timeoutms != 0) {
        mbedtls_ssl_conf_read_timeout(&client->sslcfg, sslcfg->timeoutms);
        mbedtls_ssl_set_bio(&client->sslctx, &client->netctx, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    } else {
        mbedtls_ssl_set_bio(&client->sslctx, &client->netctx, mbedtls_net_send, mbedtls_net_recv, NULL);
    }

    return 0;
}

int ssl_service_client_create(ssl_service_cfg_t *sslcfg)
{
    int ret = -1;
    ssl_service_client_t *client = (ssl_service_client_t *)os_malloc(sizeof(ssl_service_client_t));

    if (client == NULL) {
        SSL_SERVICE_CONDITION_RETURN(client == NULL, -1);
    }
    memset(client, 0, sizeof(ssl_service_client_t));

    ret = ssl_service_client_init(client, sslcfg);
    if (ret != 0) {
        SSL_SERVICE_CONDITION_RETURN(ret != 0, ret);
    }
    client->netctx.fd = sslcfg->sockfd;

    do {
        ret = mbedtls_ssl_handshake(&client->sslctx);
        SSL_SERVICE_CONDITION_RETURN(ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE, ret);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    return ret;
}

int ssl_service_client_close(ssl_service_client_t *client)
{
    int ret;

    if (client == NULL) {
        return 0;
    }

    do {
        ret = mbedtls_ssl_close_notify(&(client->sslctx));
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    mbedtls_ssl_session_reset(&(client->sslctx));
    mbedtls_net_free(&(client->netctx));
    mbedtls_ssl_config_free(&(client->sslcfg));
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_free(&client->cacert);
    mbedtls_x509_crt_free(&client->svrcert);
    mbedtls_pk_free(&client->pkey);
#endif
    mbedtls_ctr_drbg_free(&(client->drbg));
    mbedtls_entropy_free(&(client->entropy));
    mbedtls_ssl_free(&(client->sslctx));
    os_free(client);

    return 0;
}

int ssl_service_get_bytes_avail(ssl_service_client_t *client)
{
    SSL_SERVICE_CONDITION_RETURN(client == NULL, -1);
    return mbedtls_ssl_get_bytes_avail(&client->sslctx);
}
int ssl_service_net_read(ssl_service_client_t *client, unsigned char *data, unsigned int datalen)
{
    int ret;

    SSL_SERVICE_CONDITION_RETURN(client == NULL, -1);

    ret = mbedtls_ssl_read(&client->sslctx, data, datalen);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3) && defined(MBEDTLS_SSL_SESSION_TICKETS)
    /* Finish buffered ticket processing without waiting for application data. */
    while ((ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) &&
           mbedtls_ssl_check_pending(&client->sslctx)) {
        ret = mbedtls_ssl_read(&client->sslctx, data, datalen);
    }
    if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
#endif
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return ret;
    }
    SSL_SERVICE_CONDITION_RETURN(ret < 0, ret);

    return ret;
}

int ssl_service_net_write(ssl_service_client_t *client, unsigned char *data, int datalen)
{
    int ret;
    int writeofft = 0;

    SSL_SERVICE_CONDITION_RETURN(client == NULL, -1);

    do {
        ret = mbedtls_ssl_write(&client->sslctx, data + writeofft, datalen - writeofft);
        if (ret > 0) {
            writeofft += ret;
        } else {
            SSL_SERVICE_CONDITION_RETURN(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE, ret);
        }
    } while(writeofft < datalen && (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE));

    return writeofft;
}
