#include "voice_chip.h"

static int voice_chip_call0(voice_chip_device_t *chip, int (*fn)(voice_chip_device_t *))
{
    if (!chip || !chip->ops || !fn) {
        return -1;
    }

    return fn(chip);
}

int voice_chip_open(voice_chip_device_t *chip)
{
    return voice_chip_call0(chip, chip ? chip->ops->open : 0);
}

int voice_chip_close(voice_chip_device_t *chip)
{
    return voice_chip_call0(chip, chip ? chip->ops->close : 0);
}

int voice_chip_reset(voice_chip_device_t *chip)
{
    return voice_chip_call0(chip, chip ? chip->ops->reset : 0);
}

int voice_chip_flush(voice_chip_device_t *chip)
{
    return voice_chip_call0(chip, chip ? chip->ops->flush : 0);
}

int voice_chip_abort_all(voice_chip_device_t *chip)
{
    return voice_chip_call0(chip, chip ? chip->ops->abort_all : 0);
}

int voice_chip_set_output_volume_raw(voice_chip_device_t *chip, int volume)
{
    if (!chip || !chip->ops || !chip->ops->set_output_volume_raw) {
        return -1;
    }

    return chip->ops->set_output_volume_raw(chip, volume);
}

int voice_chip_set_output_volume_percent(voice_chip_device_t *chip, int percent)
{
    if (!chip || !chip->ops || !chip->ops->set_output_volume_percent) {
        return -1;
    }

    return chip->ops->set_output_volume_percent(chip, percent);
}

int voice_chip_get_output_volume_percent(voice_chip_device_t *chip)
{
    if (!chip || !chip->ops || !chip->ops->get_output_volume_percent) {
        return -1;
    }

    return chip->ops->get_output_volume_percent(chip);
}

int voice_chip_volume_step_up(voice_chip_device_t *chip)
{
    if (!chip || !chip->ops || !chip->ops->volume_step_up) {
        return -1;
    }

    return chip->ops->volume_step_up(chip);
}

int voice_chip_volume_step_down(voice_chip_device_t *chip)
{
    if (!chip || !chip->ops || !chip->ops->volume_step_down) {
        return -1;
    }

    return chip->ops->volume_step_down(chip);
}

int voice_chip_volume_step_cycle(voice_chip_device_t *chip)
{
    if (!chip || !chip->ops || !chip->ops->volume_step_cycle) {
        return -1;
    }

    return chip->ops->volume_step_cycle(chip);
}

int voice_chip_set_input_gain(voice_chip_device_t *chip, uint8_t gain)
{
    if (!chip || !chip->ops || !chip->ops->set_input_gain) {
        return -1;
    }

    return chip->ops->set_input_gain(chip, gain);
}

const voice_chip_caps_t *voice_chip_get_caps(voice_chip_device_t *chip)
{
    if (!chip || !chip->ops || !chip->ops->get_caps) {
        return 0;
    }

    return chip->ops->get_caps(chip);
}