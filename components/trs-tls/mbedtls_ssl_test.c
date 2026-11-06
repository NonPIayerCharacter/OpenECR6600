#include "cli.h"
#include "oshal.h"
#include "mbedtls_ssl_service.h"
#include "lwip/sockets.h"

char *cacert =
"-----BEGIN CERTIFICATE-----\r\n"\
"MIIC6zCCAdMCFB6KQ6JQBZUwriFYGseZNTnJrlPbMA0GCSqGSIb3DQEBBQUAMDIx\r\n"\
"MDAuBgNVBAoMJ1RMUyBQcm9qZWN0IERvZGd5IENlcnRpZmljYXRlIEF1dGhvcml0\r\n"\
"eTAeFw0yMzExMjQwOTQwMzFaFw0zMzExMjEwOTQwMzFaMDIxMDAuBgNVBAoMJ1RM\r\n"\
"UyBQcm9qZWN0IERvZGd5IENlcnRpZmljYXRlIEF1dGhvcml0eTCCASIwDQYJKoZI\r\n"\
"hvcNAQEBBQADggEPADCCAQoCggEBAKbo4MND+V/amZ+xVBAQ30pi/vw2YGT/fUsU\r\n"\
"lo59h7QG1tdPfs03AS29Y537SALTdkYPZN1ntY5EwLPEtMBWt3S9aR8trkY5iILJ\r\n"\
"zectBxjXRGZIr+p5apWrZvst9Gs60Mh2XCaRRiaygu++4RVkITpTu0Qwbh1mIxkh\r\n"\
"zNkiUR3XcdEw6zLBaSbGSVeDkMm+1+ur8tlOJ128V0uviAuY2BL614DmjjZ1IdAy\r\n"\
"EZ9B/C9YfQ9zWTZ1pDNMQm0r0n7wmZ1d6+VXamxqYfV5GF1eTgVg0YX75zidrQRq\r\n"\
"z5h49jI8xqtN+5tpW6S/hYz11i0btpmleUI/RYF/TXg1VOMNa7sCAwEAATANBgkq\r\n"\
"hkiG9w0BAQUFAAOCAQEAKF0FaHNgT6KYvgtfxZc7HK9khtV8dLO6DqOzXioyqtqg\r\n"\
"a/CudFylLShVrgJPUcs5Sk0b0J20QckShnZ3D9apAEPGPvxqzU6M10rLkbri3U7l\r\n"\
"SZs5TBkWQsZn8MX4YOEBxti0YVWVFuIUpJhL9AIpQr61uSBx0crnCqh+91BUd7AP\r\n"\
"qu/4zg3gY249ZX1oaxZNQ2879nZZegUZr8xhypPKHsGfSrc6vmaQ2Ip+Sqpm9sRZ\r\n"\
"tTq5t5AlkbRG0dr/mEdf0pyiQZvoxy4Ui8RAD7LdcUqlvwZvmDqTSxbR7RNtsNR8\r\n"\
"9vhJyLbyjsTY0SS10Vs6DTebXVLnR8LYpfhEwgkKdQ==\r\n"\
"-----END CERTIFICATE-----\r\n";


char *clientcert =
"-----BEGIN CERTIFICATE-----\r\n"\
"MIIC4jCCAcoCFEEyu0d4Gn/+bumzFDpAIUDNs5vrMA0GCSqGSIb3DQEBBQUAMDIx\r\n"\
"MDAuBgNVBAoMJ1RMUyBQcm9qZWN0IERvZGd5IENlcnRpZmljYXRlIEF1dGhvcml0\r\n"\
"eTAeFw0yMzExMjQwOTQwMzJaFw0zMzExMjEwOTQwMzJaMCkxJzAlBgNVBAoMHlRM\r\n"\
"UyBQcm9qZWN0IERldmljZSBDZXJ0aWZpY2F0ZTCCASIwDQYJKoZIhvcNAQEBBQAD\r\n"\
"ggEPADCCAQoCggEBALsxPY1cmS87/BHt3w6YBTtMdWIxO8VNZr+Q8or07vO7fQ3x\r\n"\
"kkm1W9RLbveWm22rddf/b+m1FTNhtzvRqZFkzyGXr1xacJgOLiw2r4OtwCTNya/F\r\n"\
"H46/ZAKU9ShiZ0Opr59JRLhDjUBth/jYwpQ9dLojBuQMC4W2v2K23fcN+cTCbqWv\r\n"\
"3+z/mit6KZk7RKI2W0DJqqp6v53FbMByFjeJaEPHglp3bfm3p/Rncj2BDqcUEcnm\r\n"\
"jgaMHLvlbUNPY7zTMBwDbFLVLzvzOtzHjWa3ioxpEpVX6/aJYSvl8m4VTmtW6Y4s\r\n"\
"z7kGBZUZViscKBCxlyaKx1XpS/XHM51V27qL5g8CAwEAATANBgkqhkiG9w0BAQUF\r\n"\
"AAOCAQEACrM78AaJDPJlFQV+aG9oDpk0/Dg5JdPB0g7caop7T+Hytm6vdbtcTNu2\r\n"\
"Jif7joLNb9SFoU58jXxJqJRf9qvb/m2Az8hBga3+yvN4C6R1vqa8GTJx3/lQCuZZ\r\n"\
"emOYAJmYVvHEaPwdJTK/B5/vcZVUSpdsaRuDaoBnLKkZteeJiVZB12EKsFGV3OO7\r\n"\
"ukC1jF0E3rrr6hgCjEjK+UTY0eiCpajXSUNWpEbjv4D77J8m3GinybcJ7w7wremT\r\n"\
"s0rK4UDeno6eB/MoBidDYUFYDIVz8qyFdp61uTD2mVxyENew9zDWaz+IDAnIFi5M\r\n"\
"yY5WR1yFytPQLIj7NGUiIl57palvHQ==\r\n"\
"-----END CERTIFICATE-----\r\n";

char *clientkey =
"-----BEGIN RSA PRIVATE KEY-----\r\n"\
"MIIEpAIBAAKCAQEAuzE9jVyZLzv8Ee3fDpgFO0x1YjE7xU1mv5DyivTu87t9DfGS\r\n"\
"SbVb1Etu95abbat11/9v6bUVM2G3O9GpkWTPIZevXFpwmA4uLDavg63AJM3Jr8Uf\r\n"\
"jr9kApT1KGJnQ6mvn0lEuEONQG2H+NjClD10uiMG5AwLhba/Yrbd9w35xMJupa/f\r\n"\
"7P+aK3opmTtEojZbQMmqqnq/ncVswHIWN4loQ8eCWndt+ben9GdyPYEOpxQRyeaO\r\n"\
"Bowcu+VtQ09jvNMwHANsUtUvO/M63MeNZreKjGkSlVfr9olhK+XybhVOa1bpjizP\r\n"\
"uQYFlRlWKxwoELGXJorHVelL9ccznVXbuovmDwIDAQABAoIBAAoppDPWgSKFWSwI\r\n"\
"BAIg0rfjcMQB9ELD7v4bem982LQ2y5VENjL2O6QM7FtYu9t6YY12erbvc9U0SuRe\r\n"\
"f5w7ItuqBoU3C8HVk0AnP+QgPLxCx95ZD1uKnH1fZOfqX7zWq4g0TBzM3hBPYgBq\r\n"\
"lDXEGGA6opci6EU8JKvRTb+p7JN3+xDT8EIh3h0oDYz8rD0vm4aQ3J4DjeFmAvxS\r\n"\
"Yjp5YB6BTegSRGjr4leTmMwnFFnOH+dmTibB4EeUKynX5htTSSENZzccc0HEjxcc\r\n"\
"PK7YLgNa3QuAgaCeJVwSSXsqsCkiA6JN65Qxew46fSs7Xk2q9jOqEtBG+OLrJ9p+\r\n"\
"wZiJDDECgYEA24Ln+mixnElBXSK9xkc8txFHfMdsGFE3sKhYBqhC2+RKJwOj77Ts\r\n"\
"nAI5mkYNXGhmePe6VDaGIkxjDv6/3NraQHw43oJuMNsEU0D6iodOHPjDaGNYeH1w\r\n"\
"XuhSCklA7DseKeP/9kvDbLq0a+AYcCaHVPGqPTheQYW26atQgN+2mUUCgYEA2k8H\r\n"\
"teFmSxW5+JDO2nZT0O0kah5VB1s9RDNYL7tWeomxtQjRu1dV0f4gk+tfg46nJHBy\r\n"\
"6d78sakua6ZWQgKJqvWEmUoF1X675umzH7gQlFHSPtGdzgzBqZrzfUPBlLXkp8EW\r\n"\
"aL587lnsXpXN7s9zzSuA9G7c1jWjAyvTRVIUtUMCgYEAqRlhB0Bbgl/UYKEAvJvo\r\n"\
"I8bF4VPm65y1X/tvREGer7JfhUqCoYgotc3o/fV1GXi7Z/i+q10NW6KotMZ+mu5l\r\n"\
"L0QCszabGDvASzpFRy4QeLUSI3pUjw1i4VnqyU3BG7o29GJVrjCTLVFu56NMcCje\r\n"\
"HAvG7z5bLfKKA9wbOgwcqFkCgYABOBvE6kNV/AMT3kspcSu6qPhhF1ZGOj43WUoO\r\n"\
"yi8LdCVn62qwbyZti8vGx7O62lsYmwApSjODEnzN4RnuOHFoYQKikIVD0fderMNx\r\n"\
"U+fTzDkuXKFS+0NBnTnq4yv7lqB2R1/Gqfmsoxfs101VHhd5d9q6grv8/kOwytfd\r\n"\
"NT4kXQKBgQCqsWOzx05DcheNNDIWYblNv3NKjZlsJ2lY432Nlyw0sJugZd0lVcqn\r\n"\
"VFlu/4AKT5ZIboCYyKG0k1YzgswG5+GEXDQTEHQmjRjpYUGGIra09ZXKi6cGKpUl\r\n"\
"mBfapT3uP3qC+/Ks5mSd/TsXuQyn8bEkEO6v9L/YbJmHFi94iNo2SA==\r\n"\
"-----END RSA PRIVATE KEY-----\r\n";

char *servercrt =
"-----BEGIN CERTIFICATE-----\r\n"\
"MIIC5DCCAcwCFEEyu0d4Gn/+bumzFDpAIUDNs5vqMA0GCSqGSIb3DQEBBQUAMDIx\r\n"\
"MDAuBgNVBAoMJ1RMUyBQcm9qZWN0IERvZGd5IENlcnRpZmljYXRlIEF1dGhvcml0\r\n"\
"eTAeFw0yMzExMjQwOTQwMzJaFw0zMzExMjEwOTQwMzJaMCsxFDASBgNVBAoMC1RM\r\n"\
"UyBQcm9qZWN0MRMwEQYDVQQDDAoxMC4xMC4xMC4xMIIBIjANBgkqhkiG9w0BAQEF\r\n"\
"AAOCAQ8AMIIBCgKCAQEAv/+hhssM2H3XTZhMJN86svwmLEqxwfu7nu5BMtPVabQb\r\n"\
"42kqbM9cD2fTQm/RoJ91aiCwOi6kyXPQRh93TYQJpuOTU6xw6Xb3Ijvn7IwwNbiR\r\n"\
"YK3VI6W/Bwi9dDN5XO6c4hsSB6Ivzh+WA5h7YtkVS3KB8Twh9S2zsDnrQFFY4Xwv\r\n"\
"7X9ZPbUDrrkniJCgMngI+Zo0U9kS62TcQd7NZ/cnruopEdHueTkEBfZ9DwkJ4mqW\r\n"\
"/ELxBK+F/aIxbL/43MqYk9UFx+CyspIH2+UKJQpnFRIs5NJ0GtbHanvk9iQXEZ3+\r\n"\
"RwupdNM52eHpZXcRkfrApgnM6zEGrbFVNEF9m+JyxQIDAQABMA0GCSqGSIb3DQEB\r\n"\
"BQUAA4IBAQALLgmoKA2xJANyFvKoT7L31nrCeDEBzguF1fb/grYG0Fba7u4wQ687\r\n"\
"lQNb+xNRWo3Hw2lh1RiWT4OiN6XdsTadIXQcJbl5r87HtKP1LpX81u2LWF6bmwLV\r\n"\
"2IO+wxHuEq5tvyQvPUX500IsYCe8J78BeSxSgrP10f3y14cs/NtKa+Fli8dKduGu\r\n"\
"Z9ps0OGvoB/4II2ABgS77gJJGpTxJFFH2nZi9H7g96kyEiFMMawERwW9jMFLEsem\r\n"\
"rxNigiKXDKS5fJCAbKZijibcLYpksvJlCyhbjVpJZzN6qNYSCS3GtND4vb9UP+Uz\r\n"\
"VbsOKcw4h51seK24C0v4LCw+jkdgK72i\r\n"\
"-----END CERTIFICATE-----\r\n";

char *serverkey =
"-----BEGIN RSA PRIVATE KEY-----\r\n"\
"MIIEowIBAAKCAQEAv/+hhssM2H3XTZhMJN86svwmLEqxwfu7nu5BMtPVabQb42kq\r\n"\
"bM9cD2fTQm/RoJ91aiCwOi6kyXPQRh93TYQJpuOTU6xw6Xb3Ijvn7IwwNbiRYK3V\r\n"\
"I6W/Bwi9dDN5XO6c4hsSB6Ivzh+WA5h7YtkVS3KB8Twh9S2zsDnrQFFY4Xwv7X9Z\r\n"\
"PbUDrrkniJCgMngI+Zo0U9kS62TcQd7NZ/cnruopEdHueTkEBfZ9DwkJ4mqW/ELx\r\n"\
"BK+F/aIxbL/43MqYk9UFx+CyspIH2+UKJQpnFRIs5NJ0GtbHanvk9iQXEZ3+Rwup\r\n"\
"dNM52eHpZXcRkfrApgnM6zEGrbFVNEF9m+JyxQIDAQABAoIBAAvMLmW09l4bJW6s\r\n"\
"LAmb+eRjoAjash+EU1wJsdHouFVdbtVDcgrrlvGPZKjRbCUdhz51Mb7xfMiwKB0W\r\n"\
"8+K8eUda9oOOtgVyfO8KlU6wsomqhKxXIiCln55UCCgqvYtbUiKGnU4AxkU5JaOk\r\n"\
"6/f/9NIFAD0Nf9yr6T2r4Wu7U+XO+3fuU7OviMBHx75Mb3DhMeSrGqMpI4KBa8sG\r\n"\
"oKUEc5B9Ac371peSdFOYdteJgejQJr9QD6VyN4eNMOhbp+QO+2x3ZB2lRBegpBmz\r\n"\
"Hiqlx5c20aos82c9sOtpvrL7Ye30ehKFH9SvG9n1bcC/g7TEtnroeRySM7S7ndg4\r\n"\
"d9gGTCUCgYEA3rJFsexGg9W8y8DCUcPtpt9xvohhvX51K3dRRqM/T7LwhSY4rGLh\r\n"\
"4Nj3NMaZnGO5pxoj/UEjH17ZsendSVgh6QyHqfmxikfzlZxEzczIpxDCm0C8j5Po\r\n"\
"uPzG1K4ZwWz2rdNZL/tn1UpJj1YKeuOyP4ATb6PfGsRqp4ZVcjYFocMCgYEA3LYe\r\n"\
"6eBCq9m3AEwFoS5bazOWKgBfEhAgjsCb6ha37xB45AAXWKD8TRDZPTi2Bs65GiDg\r\n"\
"p8OvLPnl5vhD3qRz09zRGqSVppyJ/maUvOZnn4hST2aCf6C70zzHba818yaAyd9f\r\n"\
"rtKMu8ahNNwrRbyv6yUjj+DS46ayZPbx89hoiNcCgYAz4YxjGwyCgjTtswaSTfb7\r\n"\
"vWqeXN+IagGaEr4YmseRJbrrCjCB2LeOzn0pCmuXB5Sk3W4s/hPUpyBIVpd7EtEz\r\n"\
"nfutGcLG2GRjInjVCTRl132+9aH4QRkin6Eiwzy011HQxUAt8YALLLdecUkXwBwy\r\n"\
"tv/QLnk3nzLJvH4DkZpWSQKBgQC4cN1OlqFAmVTrw5vkwSeWY2q2uM65+o4qnVzt\r\n"\
"94RIHJzZ9BWOdCHDiTPxjGXZNIHNCvKSX94RGNh99rRI52cAAoUf3bBNpn1PAoyD\r\n"\
"e4vW7oXSYpjlbP3pHd6Ok5/jDScNDEUahEslVG09Dkx/H5pnSAb64md55MEpzJAP\r\n"\
"NabNywKBgHEX6M1u7b2KRlLRU4RwfoyupgmUbfJ6jlN2kvPZB2wZ/4vpgQjVONS1\r\n"\
"u0KlMcXnN3LuM3ibfDcW/8oYsf6VWQ6OVf2jXRLX1NRQm8C3qNi1WyDf9+FlD6S5\r\n"\
"pEw9Etl7Fsp1HbVeq8wVSoA12/ekvepR24A0Uqg7Vy1BD8s++tLU\r\n"\
"-----END RSA PRIVATE KEY-----\r\n";

void ssl_service_client_task(void *arg)
{
    int ret;
    char *buff = "abcd1234567890";
    int fd;
    fd_set reads;
    int index;

    struct timeval timeout = {0};
    timeout.tv_sec = 1;

    ssl_service_cfg_t sslcfg = {0};
    sslcfg.cacert = cacert;
    sslcfg.calen = strlen(cacert) + 1;
    sslcfg.pemmodel = SSL_SERVICE_PEM_STRING;
    sslcfg.privkey = clientkey;
    sslcfg.keylen = strlen(clientkey) + 1;
    sslcfg.svrcert = clientcert;
    sslcfg.svrcertlen = strlen(clientcert) + 1;
    sslcfg.protocols = MBEDTLS_NET_PROTO_TCP;
    sslcfg.hostCNname = "10.10.10.1";
    sslcfg.hostlen = strlen(sslcfg.hostCNname);
    sslcfg.port = 52001;
    int loop = 10;

    ret = ssl_service_client_create(&sslcfg);
    if (ret != 0) {
        ssl_service_client_close(&sslcfg);
        return;
    }

    fd = ssl_service_client_get_fd(&sslcfg);
    do {
        FD_ZERO(&reads);
        FD_SET(fd, &reads);

        ret = select(fd + 1, &reads, NULL, NULL, &timeout);
        if (ret > 0) {
            if (FD_ISSET(fd, &reads)) {
                unsigned char buff[1024];
                ret = ssl_service_net_read((ssl_service_client_t *)sslcfg.ssl, buff, sizeof(buff));
                if (ret < 0) {
                    break;
                }
                os_printf(LM_APP, LL_ERR, "recvdata:%u \n", ret);
                for (index = 0; index < ret; index++) {
                    os_printf(LM_APP, LL_ERR, "0x%02x ", buff[index]);
                }
                os_printf(LM_APP, LL_ERR, "\n");
            }
        }

        os_msleep(10000);
        ssl_service_net_write((ssl_service_client_t *)sslcfg.ssl, (unsigned char *)buff, strlen(buff));
    } while (loop--);

    ssl_service_client_close(&sslcfg);
}

int ssl_service_client_test(cmd_tbl_t *t, int argc, char *argv[])
{
    os_task_create("sslservice-clienttask", 5, (8*1024), (task_entry_t)ssl_service_client_task, NULL);

    return 0;
}

CLI_CMD(ssl_client, ssl_service_client_test, "ssl_service_test", "ssl_service_test");

void ssl_service_server_task(void *arg)
{
    ssl_service_cfg_t sslcfg = {0};
    ssl_service_base_t *client[12] = {0};
    int listenfd;
    int ret;
    fd_set reads;
    int index, inx;
    int fdmax = -1;

    sslcfg.cacert = cacert;
    sslcfg.calen = strlen(cacert) + 1;
    sslcfg.pemmodel = SSL_SERVICE_PEM_STRING;
    sslcfg.privkey = serverkey;
    sslcfg.keylen = strlen(serverkey) + 1;
    sslcfg.svrcert = servercrt;
    sslcfg.svrcertlen = strlen(servercrt) + 1;
    sslcfg.protocols = MBEDTLS_NET_PROTO_TCP;
    sslcfg.port = 52001;

    if (ssl_service_server_create(&sslcfg) != 0) {
        ssl_service_server_destory(&sslcfg);
    }
    listenfd = ssl_service_server_get_fd(&sslcfg);

    do {
        FD_ZERO(&reads);
        FD_SET(listenfd, &reads);
        fdmax = (fdmax < listenfd) ? listenfd : fdmax;

        for (index = 0; index < 12; index++) {
            if (client[index] != NULL) {
                FD_SET(client[index]->netctx.fd, &reads);
                fdmax = (fdmax < client[index]->netctx.fd) ? client[index]->netctx.fd : fdmax;
            }
        }

        ret = select(fdmax + 1, &reads, NULL, NULL, NULL);
        if (ret > 0) {
            if (FD_ISSET(listenfd, &reads)) {
                ret = ssl_service_server_accept(&sslcfg);
                if (ret != 0) {
                    return;
                }
                for (index = 0; index < 12; index++) {
                    if (client[index] == NULL) {
                        client[index] = ssl_service_sclient_get(&sslcfg);
                        break;
                    }
                }
            }

            for (index = 0; index < 12; index++) {
                if (client[index] == NULL) {
                    continue;
                }

                if (FD_ISSET(client[index]->netctx.fd, &reads)) {
                    unsigned char buff[1024] = {0};
                    ret = ssl_service_net_read(client[index], buff, sizeof(buff));
                    if (ret <= 0) {
                        ssl_service_sclient_destory(client[index]);
                        client[index] = NULL;
                        break;
                    }
                    for (inx = 0; inx < ret; inx++) {
                        os_printf(LM_APP, LL_ERR, "0x%02x ", buff[inx]);
                    }
                    os_printf(LM_APP, LL_ERR, "\n");

                    ssl_service_net_write(client[index], buff, ret);
                }
            }
        }
    } while (1);

    ssl_service_server_destory(&sslcfg);
}

int ssl_service_server_test(cmd_tbl_t *t, int argc, char *argv[])
{
    os_task_create("sslservice-servertask", 5, (8*1024), (task_entry_t)ssl_service_server_task, NULL);

    return 0;
}

CLI_CMD(ssl_server, ssl_service_server_test, "ssl_service_server_test", "ssl_service_server_test");


