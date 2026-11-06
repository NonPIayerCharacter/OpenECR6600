#ifndef VOICE_CI1302_SERVICE_H
#define VOICE_CI1302_SERVICE_H

#include "voice_chip_service.h"

/* Normal applications use voice_chip_service_*; this header exposes diagnostics.
 * CI1302 service record callbacks run in its RX task. Each data callback receives
 * an allocated PCM block and MUST free it with os_free(), like GX8006 service.
 * Do not block or call control APIs from those callbacks. Register callbacks only
 * while idle. A service error latches until start_capture/play_stream begins a
 * new session. Stop/cancel does not synthesize a successful completion callback.
 * Service volume accepts 1..100 (rounded to seven levels); 0/mute is unsupported.
 */
int voice_ci1302_service_register(void);
int voice_ci1302_service_last_error(void);

#endif
