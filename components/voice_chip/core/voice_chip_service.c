#include "voice_chip_service.h"
#include <string.h>
#if defined(CONFIG_VOICE_CHIP_ES8311)
#include "voice_es8311_service.h"
#endif

static const voice_chip_service_t *g_voice_chip_service;
int voice_chip_service_play_stream_ex(const void *data, size_t len, int format, int semantic);

static int voice_chip_service_ensure_registered(void)
{
    if (g_voice_chip_service) {
        return 0;
    }

#if defined(CONFIG_VOICE_CHIP_GX8006) || defined(CONFIG_CI1302)
    return voice_chip_service_register_default_backend();
#else
    return -1;
#endif
}

static const voice_chip_service_ops_t *voice_chip_service_get_ops(void)
{
    if (voice_chip_service_ensure_registered() < 0 ||
        !g_voice_chip_service || !g_voice_chip_service->ops) {
        return 0;
    }

    return g_voice_chip_service->ops;
}

static int voice_chip_service_call0(int (*fn)(void))
{
    if (!fn) {
        return -1;
    }

    return fn();
}

int voice_chip_service_register(const voice_chip_service_t *service)
{
    if (!service || !service->ops) {
        return -1;
    }

    if (g_voice_chip_service && g_voice_chip_service != service) {
        return -1;
    }

    g_voice_chip_service = service;
    return 0;
}

int voice_chip_service_unregister(const voice_chip_service_t *service)
{
    if (service && g_voice_chip_service != service) {
        return -1;
    }

    g_voice_chip_service = 0;
    return 0;
}

const voice_chip_service_t *voice_chip_service_current(void)
{
    return g_voice_chip_service;
}

int voice_chip_service_init(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->init : 0);
}

int voice_chip_service_start(void)
{
    return voice_chip_service_init();
}

int voice_chip_service_deinit(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->deinit : 0);
}

int voice_chip_service_start_capture(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->start_capture : 0);
}

int voice_chip_service_stop_capture(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->stop_capture : 0);
}

int voice_chip_service_end_stream(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return voice_chip_service_call0(ops ? ops->end_stream : 0);
}

int voice_chip_service_play_stream(const void *data, size_t len, int format)
{
    return voice_chip_service_play_stream_ex(data,
                                             len,
                                             format,
                                             VOICE_PLAYBACK_STREAM_DEFAULT);
}

int voice_chip_service_play_stream_ex(const void *data, size_t len, int format, int semantic)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->play_stream) {
        return -1;
    }

    if (ops->play_stream_ex) {
        return ops->play_stream_ex(data, len, format, semantic);
    }

    return ops->play_stream(data, len, format);
}

int voice_chip_service_clear_stream(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->clear_stream : 0);
}

int voice_chip_service_cancel_playback(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->cancel_playback : 0);
}

int voice_chip_service_set_output_volume(uint8_t volume)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_output_volume) {
        return -1;
    }

    return ops->set_output_volume(volume);
}

int voice_chip_service_get_output_volume(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->get_output_volume) {
        return -1;
    }

    return ops->get_output_volume();
}

int voice_chip_service_set_idle(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->set_idle : 0);
}

int voice_chip_service_set_active(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->set_active : 0);
}

int voice_chip_service_get_status(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->get_status) {
        return -1;
    }

    return ops->get_status();
}

bool voice_chip_service_is_ready(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return ops && ops->is_ready && ops->is_ready();
}

int voice_chip_service_set_record_callbacks(voice_chip_service_record_start_cb_t start_cb,
                                            voice_chip_service_record_data_cb_t data_cb,
                                            voice_chip_service_record_stop_cb_t stop_cb)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_record_callbacks) {
        return -1;
    }

    return ops->set_record_callbacks(start_cb, data_cb, stop_cb);
}

const voice_chip_caps_t *voice_chip_service_get_caps(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->get_caps) {
        return 0;
    }

    return ops->get_caps();
}

bool voice_chip_service_status_is_capturing(int status)
{
    return status == VOICE_CHIP_SERVICE_STATUS_CAPTURING ||
           status == VOICE_CHIP_SERVICE_STATUS_CAPTURING_AND_PLAYING;
}

bool voice_chip_service_status_is_playing(int status)
{
    return status == VOICE_CHIP_SERVICE_STATUS_PLAYING ||
           status == VOICE_CHIP_SERVICE_STATUS_CAPTURING_AND_PLAYING;
}

int voice_chip_service_set_playback_callbacks(voice_chip_service_playback_cb_t start_cb,
                                              voice_chip_service_playback_cb_t finished_cb)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_playback_callbacks) {
        return -1;
    }

    return ops->set_playback_callbacks(start_cb, finished_cb);
}

int voice_chip_service_set_prompt_player(voice_chip_service_prompt_play_cb_t cb)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_prompt_player) {
        return -1;
    }

    return ops->set_prompt_player(cb);
}

int voice_chip_service_set_audio_res_provider(voice_chip_service_audio_res_provider_t cb)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_audio_res_provider) {
        return -1;
    }

    return ops->set_audio_res_provider(cb);
}

int voice_chip_service_set_powerup_audio_id(int audio_id)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_powerup_audio_id) {
        return -1;
    }

    return ops->set_powerup_audio_id(audio_id);
}

int voice_chip_service_play_local(int audio_id)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->play_local) {
        return -1;
    }

    return ops->play_local(audio_id);
}

int voice_chip_service_clear_audio_queue(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->clear_audio_queue : 0);
}

bool voice_chip_service_is_local_playback_busy(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return ops && ops->is_local_playback_busy ? ops->is_local_playback_busy() : false;
}

int voice_chip_service_get_custom_opus_audio_id(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->get_custom_opus_audio_id : 0);
}

int voice_chip_service_get_custom_opus_resource(const uint8_t **base,
                                                const uint8_t **end,
                                                uint16_t *framesize)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->get_custom_opus_resource) {
        return -1;
    }

    return ops->get_custom_opus_resource(base, end, framesize);
}

int voice_chip_service_set_custom_opus_resource(uint8_t *addr, uint32_t len, uint16_t framesize)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_custom_opus_resource) {
        return -1;
    }

    return ops->set_custom_opus_resource(addr, len, framesize);
}

int voice_chip_service_set_opus_mode(voice_chip_service_opus_mode_t mode)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_opus_mode) {
        return -1;
    }

    return ops->set_opus_mode(mode);
}

int voice_chip_service_set_vad_threshold(uint8_t on_or_threshold)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (!ops || !ops->set_vad_threshold) {
        return -1;
    }

    return ops->set_vad_threshold(on_or_threshold);
}

int voice_chip_service_sync_speaker_config(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->sync_speaker_config : 0);
}

void voice_chip_service_set_output_volume_local(uint8_t volume)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    if (ops && ops->set_output_volume_local) {
        ops->set_output_volume_local(volume);
    }
}

int voice_chip_service_commit_output_volume(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->commit_output_volume : 0);
}

int voice_chip_service_reset_output_volume(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();

    return voice_chip_service_call0(ops ? ops->reset_output_volume : 0);
}

int voice_chip_service_configure(const voice_chip_service_config_t *cfg)
{
    if (!cfg) {
        return -1;
    }
#if defined(CONFIG_VOICE_CHIP_ES8311)
    voice_es8311_service_config_t backend_cfg = {
        .device_addr = cfg->device_addr,
        .volume_min = cfg->volume_min,
        .volume_max = cfg->volume_max,
        .default_volume_index = cfg->default_volume_index,
        .i2c_speed = cfg->i2c_speed,
        .i2c_address_bit = cfg->i2c_address_bit,
        .i2c_mode = cfg->i2c_mode,
        .i2c_dma = cfg->i2c_dma,
        .scl_num = cfg->scl_num,
        .sda_num = cfg->sda_num,
        .volume_store_key = cfg->volume_store_key,
        .storage_ops = cfg->storage_ops,
        .runtime_hooks = {
            .ready_cb = cfg->runtime_hooks.ready_cb,
            .idle_cb = cfg->runtime_hooks.idle_cb,
        },
        .record_upload_cb = cfg->record_upload_cb,
    };
    const voice_chip_service_t *current = voice_chip_service_current();
    if (current && (!current->name || strcmp(current->name, "es8311") != 0)) {
        return -1;
    }
    return voice_es8311_service_register_default(&backend_cfg);
#else
    return -1;
#endif
}

int voice_chip_service_volume_step_up(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return voice_chip_service_call0(ops ? ops->volume_step_up : 0);
}

int voice_chip_service_volume_step_down(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return voice_chip_service_call0(ops ? ops->volume_step_down : 0);
}

int voice_chip_service_volume_step_cycle(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return voice_chip_service_call0(ops ? ops->volume_step_cycle : 0);
}

int voice_chip_service_reset_playback_cancel_latch(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return voice_chip_service_call0(ops ? ops->reset_playback_cancel_latch : 0);
}

int voice_chip_service_set_output_volume_raw(int volume)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return ops && ops->set_output_volume_raw ? ops->set_output_volume_raw(volume) : -1;
}

int voice_chip_service_set_volume_bounds(uint8_t max_volume, uint8_t min_volume)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return ops && ops->set_volume_bounds ? ops->set_volume_bounds(max_volume, min_volume) : -1;
}

int voice_chip_service_set_input_gain(uint8_t gain)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return ops && ops->set_input_gain ? ops->set_input_gain(gain) : -1;
}

int voice_chip_service_get_capture_blocker_state(voice_chip_service_capture_blocker_state_t *state)
{
    const voice_chip_service_ops_t *ops;
    if (!state) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    ops = voice_chip_service_get_ops();
    return ops && ops->get_capture_blocker_state ? ops->get_capture_blocker_state(state) : -1;
}

bool voice_chip_service_capture_force_stop_pending(void)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return ops && ops->capture_force_stop_pending ? ops->capture_force_stop_pending() : false;
}

uint8_t *voice_chip_service_record_frame_data(voice_chip_service_record_frame_t *frame)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return frame && ops && ops->record_frame_data ? ops->record_frame_data(frame) : NULL;
}

bool voice_chip_service_record_frame_is_consumed(const voice_chip_service_record_frame_t *frame)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return !frame || !ops || !ops->record_frame_is_consumed || ops->record_frame_is_consumed(frame);
}

int voice_chip_service_record_frame_consume(voice_chip_service_record_frame_t *frame)
{
    const voice_chip_service_ops_t *ops = voice_chip_service_get_ops();
    return frame && ops && ops->record_frame_consume ? ops->record_frame_consume(frame) : -1;
}
