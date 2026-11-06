#ifndef __HTTP_OTA_H__
#define __HTTP_OTA_H__

/*--------------------------------------------------------------------------
* 	                                        	Include files
--------------------------------------------------------------------------*/
#include "ota.h"

/**
 * @brief Optional notification for an accepted HTTP download body fragment.
 *
 * Called synchronously in the task running http_client_download_file(), after
 * the fragment has been buffered as the package header or accepted by ota_write().
 * The data is valid only during the callback and must not be modified.
 * Range downloads report absolute file offsets; retries continue at the next
 * accepted byte. Skipped A-B image ranges and dummy data do not invoke this callback.
 * Receiving the last fragment does not mean final OTA verification has succeeded.
 *
 * @param[in] data Current body fragment.
 * @param[in] len Fragment size in bytes.
 * @param[in] offset Fragment offset in the complete OTA package.
 * @param[in] total Complete package size from HTTP headers, or 0 if unknown.
 * @param[in] user_data Context supplied when registering the callback.
 */
typedef void (*http_ota_body_cb_t)(const unsigned char *data, unsigned int len,
                                 unsigned int offset, unsigned int total, void *user_data);

/*--------------------------------------------------------------------------
* 	                                        	Macros
--------------------------------------------------------------------------*/
/** Description of the macro */

/*--------------------------------------------------------------------------
* 	                                        	Types
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                        	Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                        	Global  Variables
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 *                                 Function Prototypes
 *--------------------------------------------------------------------------*/
/**
 * @brief Register the optional body callback in the HTTP OTA context.
 *
 * Registers one callback by direct assignment. Pass NULL to unregister it.
 * Register before http_client_download_file() and keep the callback and context
 * valid until it returns; unregister before destroying user_data.
 * Download state resets preserve the registration until the caller changes it.
 * With no registered callback, the existing download flow is unchanged.
 */
void http_client_download_body_cb_register(http_ota_body_cb_t callback, void *user_data);

/**
 * @brief Download a firmware image by URL and run the OTA flow.
 *
 * The HTTP body callback writes received fragments through ota_write(). On
 * success the implementation calls ota_done(reboot); on failure it releases
 * the OTA context without requesting a restart.
 *
 * @param[in] url URL of the firmware image to download.
 * @param[in] reboot true to restart after a successful OTA finish, false to
 *                   return after verification and writing the active update flag.
 * @return 0 on success, or -1 on failure.
 */
int http_client_download_file(const char *url, bool reboot);

#endif /* __HTTP_OTA_H__ */
