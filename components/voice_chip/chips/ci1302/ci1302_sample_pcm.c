#include "ci1302_sample_pcm.h"

#define ADS_LOCAL_AUDIO(name, id, type, data)
#include "cli/piu_pcm.h"
#undef ADS_LOCAL_AUDIO

const uint8_t *ci1302_sample_pcm_data(void)
{
    return piu_raw;
}

size_t ci1302_sample_pcm_size(void)
{
    return sizeof(piu_raw);
}
