#ifndef CI1302_AUDIO_H
#define CI1302_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include "ci1302.h"

/* Hardware endpoint only. Platform queues, thresholds and codecs stay upstream.
 * read/write run in the existing platform tasks, never in the UART RX callback.
 * write borrows the caller's PCM until it returns. */
int ci1302_audio_init(void);
int ci1302_audio_capture_begin(void);
void ci1302_audio_capture_stop(void);
int ci1302_audio_read(void *data, unsigned int length);
int ci1302_audio_write(const void *data, unsigned int length);
void ci1302_audio_drain(void);
void ci1302_audio_cancel(void);
int ci1302_audio_status(void);
uint32_t ci1302_audio_rx_overflow(void);
void ci1302_audio_on_event(ci1302_event_callback_t callback, void *user_data);

#endif
