// Copyright 2018-2019 Transa-Semi Technology Inc. and its subsidiaries.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef _TRS_TLS_H_
#define _TRS_TLS_H_

#include <stdbool.h>
#include <fcntl.h>

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
//#include "trs_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "lwip/netdb.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************* error define ********************/
typedef int32_t trs_err_t; 

#define TRS_OK 0
#define TRS_ERR -1
#define TRS_ERR_INVALID_ARG -1
#define TRS_ERR_NO_MEM -2
#define TRS_ERR_NOT_FOUND -3
#define TRS_ERR_OP_FLASH -4
#define TRS_ERR_TIMEOUT -5
/************************** end *************************/

/**
 *  @brief TRS-TLS Connection State
 */
typedef enum trs_tls_conn_state {
    TRS_TLS_INIT = 0,
    TRS_TLS_CONNECTING,
    TRS_TLS_HANDSHAKE,
    TRS_TLS_FAIL,
    TRS_TLS_DONE,
} trs_tls_conn_state_t;

/**
 * @brief      TRS-TLS configuration parameters 
 */ 
typedef struct trs_tls_cfg {
    const char **alpn_protos;               /*!< Application protocols required for HTTP2.
                                                 If HTTP2/ALPN support is required, a list
                                                 of protocols that should be negotiated. 
                                                 The format is length followed by protocol
                                                 name. 
                                                 For the most common cases the following is ok:
                                                 "\x02h2"
                                                 - where the first '2' is the length of the protocol and
                                                 - the subsequent 'h2' is the protocol name */
 
    const unsigned char *cacert_pem_buf;    /*!< Certificate Authority's certificate in a buffer */
 
    unsigned int cacert_pem_bytes;          /*!< Size of Certificate Authority certificate
                                                 pointed to by cacert_pem_buf */

    const unsigned char *clientcert_pem_buf;/*!< Client certificate in a buffer */
 
    unsigned int clientcert_pem_bytes;      /*!< Size of client certificate pointed to by
                                                 clientcert_pem_buf */

    const unsigned char *clientkey_pem_buf; /*!< Client key in a buffer */

    unsigned int clientkey_pem_bytes;       /*!< Size of client key pointed to by
                                                 clientkey_pem_buf */

    const unsigned char *clientkey_password;/*!< Client key decryption password string */

    unsigned int clientkey_password_len;    /*!< String length of the password pointed to by
                                                 clientkey_password */

    bool non_block;                         /*!< Configure non-blocking mode. If set to true the 
                                                 underneath socket will be configured in non 
                                                 blocking mode after tls session is established */

    int timeout_ms;                         /*!< Network timeout in milliseconds */

    bool use_global_ca_store;               /*!< Use a global ca_store for all the connections in which
                                                 this bool is set. */
} trs_tls_cfg_t;

/**
 * @brief      TRS-TLS Connection Handle 
 */
typedef struct trs_tls {
    mbedtls_ssl_context ssl;                                                    /*!< TLS/SSL context */
 
    mbedtls_entropy_context entropy;                                            /*!< mbedTLS entropy context structure */
 
    mbedtls_ctr_drbg_context ctr_drbg;                                          /*!< mbedTLS ctr drbg context structure.
                                                                                     CTR_DRBG is deterministic random 
                                                                                     bit generation based on AES-256 */
 
    mbedtls_ssl_config conf;                                                    /*!< TLS/SSL configuration to be shared 
                                                                                     between mbedtls_ssl_context 
                                                                                     structures */
 
    mbedtls_net_context server_fd;                                              /*!< mbedTLS wrapper type for sockets */
 
    mbedtls_x509_crt cacert;                                                    /*!< Container for the X.509 CA certificate */

    mbedtls_x509_crt *cacert_ptr;                                               /*!< Pointer to the cacert being used. */

    mbedtls_x509_crt clientcert;                                                /*!< Container for the X.509 client certificate */

    mbedtls_pk_context clientkey;                                               /*!< Container for the private key of the client
                                                                                     certificate */

    int sockfd;                                                                 /*!< Underlying socket file descriptor. */
 
    ssize_t (*trs_tls_read)(struct trs_tls  *tls, char *data, size_t datalen);          /*!< Callback function for reading data from TLS/SSL
                                                                                     connection. */
 
    ssize_t (*trs_tls_write)(struct trs_tls *tls, const char *data, size_t datalen);    /*!< Callback function for writing data to TLS/SSL
                                                                                     connection. */

    trs_tls_conn_state_t  conn_state;                                           /*!< TRS-TLS Connection state */

    fd_set rset;                                                                /*!< read file descriptors */

    fd_set wset;                                                                /*!< write file descriptors */
} trs_tls_t;

/**
 * @brief      Create a new blocking TLS/SSL connection
 *
 * This function establishes a TLS/SSL connection with the specified host in blocking manner.
 * 
 * @param[in]  hostname  Hostname of the host.
 * @param[in]  hostlen   Length of hostname.
 * @param[in]  port      Port number of the host.
 * @param[in]  cfg       TLS configuration as trs_tls_cfg_t. If you wish to open 
 *                       non-TLS connection, keep this NULL. For TLS connection,
 *                       a pass pointer to trs_tls_cfg_t. At a minimum, this
 *                       structure should be zero-initialized.
 * @return pointer to trs_tls_t, or NULL if connection couldn't be opened.
 */
trs_tls_t *trs_tls_conn_new(const char *hostname, int hostlen, int port, const trs_tls_cfg_t *cfg);

/**
 * @brief      Create a new blocking TLS/SSL connection with a given "HTTP" url
 *
 * The behaviour is same as trs_tls_conn_new() API. However this API accepts host's url.
 * 
 * @param[in]  url  url of host.
 * @param[in]  cfg  TLS configuration as trs_tls_cfg_t. If you wish to open
 *                  non-TLS connection, keep this NULL. For TLS connection,
 *                  a pass pointer to 'trs_tls_cfg_t'. At a minimum, this
 *                  structure should be zero-initialized.
 * @return pointer to trs_tls_t, or NULL if connection couldn't be opened.
 */
// trs_tls_t *trs_tls_conn_http_new(const char *url, const trs_tls_cfg_t *cfg);
   
/**
 * @brief      Create a new non-blocking TLS/SSL connection
 *
 * This function initiates a non-blocking TLS/SSL connection with the specified host, but due to
 * its non-blocking nature, it doesn't wait for the connection to get established.
 *
 * @param[in]  hostname  Hostname of the host.
 * @param[in]  hostlen   Length of hostname.
 * @param[in]  port      Port number of the host.
 * @param[in]  cfg       TLS configuration as trs_tls_cfg_t. `non_block` member of
 *                       this structure should be set to be true.
 * @param[in]  tls       pointer to trs-tls as trs-tls handle.
 *
 * @return
 *             - -1      If connection establishment fails.
 *             -  0      If connection establishment is in progress.
 *             -  1      If connection establishment is successful.
 */
int trs_tls_conn_new_async(const char *hostname, int hostlen, int port, const trs_tls_cfg_t *cfg, trs_tls_t *tls);

/**
 * @brief      Create a new non-blocking TLS/SSL connection with a given "HTTP" url
 *
 * The behaviour is same as trs_tls_conn_new() API. However this API accepts host's url.
 *
 * @param[in]  url     url of host.
 * @param[in]  cfg     TLS configuration as trs_tls_cfg_t.
 * @param[in]  tls     pointer to trs-tls as trs-tls handle.
 *
 * @return
 *             - -1     If connection establishment fails.
 *             -  0     If connection establishment is in progress.
 *             -  1     If connection establishment is successful.
 */
// int trs_tls_conn_http_new_async(const char *url, const trs_tls_cfg_t *cfg, trs_tls_t *tls);

/**
 * @brief      Write from buffer 'data' into specified tls connection.
 * 
 * @param[in]  tls      pointer to trs-tls as trs-tls handle.
 * @param[in]  data     Buffer from which data will be written.
 * @param[in]  datalen  Length of data buffer.
 * 
 * @return 
 *             - >0  if write operation was successful, the return value is the number 
 *                   of bytes actually written to the TLS/SSL connection.  
 *             -  0  if write operation was not successful. The underlying
 *                   connection was closed.
 *             - <0  if write operation was not successful, because either an 
 *                   error occured or an action must be taken by the calling process.   
 */
static inline ssize_t trs_tls_conn_write(trs_tls_t *tls, const void *data, size_t datalen)
{
    return tls->trs_tls_write(tls, (char *)data, datalen);
}

/**
 * @brief      Read from specified tls connection into the buffer 'data'.
 * 
 * @param[in]  tls      pointer to trs-tls as trs-tls handle.
 * @param[in]  data     Buffer to hold read data.
 * @param[in]  datalen  Length of data buffer. 
 *
 * @return
 *             - >0  if read operation was successful, the return value is the number
 *                   of bytes actually read from the TLS/SSL connection.
 *             -  0  if read operation was not successful. The underlying
 *                   connection was closed.
 *             - <0  if read operation was not successful, because either an
 *                   error occured or an action must be taken by the calling process.
 */
static inline ssize_t trs_tls_conn_read(trs_tls_t *tls, void  *data, size_t datalen)
{
    return tls->trs_tls_read(tls, (char *)data, datalen);
}

/**
 * @brief      Close the TLS/SSL connection and free any allocated resources.
 * 
 * This function should be called to close each tls connection opened with trs_tls_conn_new() or
 * trs_tls_conn_http_new() APIs. 
 *
 * @param[in]  tls  pointer to trs-tls as trs-tls handle.    
 */
void trs_tls_conn_delete(trs_tls_t *tls);

/**
 * @brief      Return the number of application data bytes remaining to be
 *             read from the current record
 *
 * This API is a wrapper over mbedtls's mbedtls_ssl_get_bytes_avail() API.
 *
 * @param[in]  tls  pointer to trs-tls as trs-tls handle.
 *
 * @return
 *            - -1  in case of invalid arg
 *            - bytes available in the application data
 *              record read buffer
 */
size_t trs_tls_get_bytes_avail(trs_tls_t *tls);

/**
 * @brief      Create a global CA store with the buffer provided in cfg.
 *
 * This function should be called if the application wants to use the same CA store for
 * multiple connections. The application must call this function before calling trs_tls_conn_new().
 *
 * @param[in]  cacert_pem_buf    Buffer which has certificates in pem format. This buffer
 *                               is used for creating a global CA store, which can be used
 *                               by other tls connections.
 * @param[in]  cacert_pem_bytes  Length of the buffer.
 *
 * @return
 *             - TRS_OK  if creating global CA store was successful.
 *             - Other   if an error occured or an action must be taken by the calling process.
 */
trs_err_t trs_tls_set_global_ca_store(const unsigned char *cacert_pem_buf, const unsigned int cacert_pem_bytes);

/**
 * @brief      Get the pointer to the global CA store currently being used.
 *
 * The application must first call trs_tls_set_global_ca_store(). Then the same
 * CA store could be used by the application for APIs other than trs_tls.
 *
 * @note       Modifying the pointer might cause a failure in verifying the certificates.
 *
 * @return
 *             - Pointer to the global CA store currently being used    if successful.
 *             - NULL                                                   if there is no global CA store set.
 */
mbedtls_x509_crt *trs_tls_get_global_ca_store();

/**
 * @brief      Free the global CA store currently being used.
 *
 * The memory being used by the global CA store to store all the parsed certificates is
 * freed up. The application can call this API if it no longer needs the global CA store.
 */
void trs_tls_free_global_ca_store();



#ifdef __cplusplus
}
#endif

#endif /* ! _TRS_TLS_H_ */
