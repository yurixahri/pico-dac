#include "i2s.h"

static audio_format_t audio_format = {
    .sample_freq = 48000,
    .pcm_format = AUDIO_PCM_FORMAT_S32, // Matches your lib's S32
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_i2s_config_t i2s_config = {
    .data_pin = I2S_PIN_DATA,
    .clock_pin_base = I2S_PIN_BCK,
    .dma_channel0 = 0,
    .dma_channel1 = 1,
    .pio_sm = 0
};

audio_buffer_pool_t *ap = NULL;

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8
};

volatile bool is_i2s_init_pending = false;

void i2s_setup(uint32_t samp_freq){
    is_i2s_init_pending = true;
    if (ap != NULL) {
        i2s_audio_deinit(); // less gap noise if deinit() is done when input is stable
    }
    i2s_audio_init(samp_freq);
    is_i2s_init_pending = false;
}

void i2s_audio_init(uint32_t sample_freq) {
    
    audio_format.sample_freq = sample_freq;
    
    ap = audio_new_producer_pool(&producer_format, 8, SAMPLES_PER_BUFFER);
    
    bool ok;
    const audio_format_t *output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(ap);

    { // initial buffer data
        audio_buffer_t* ab = take_audio_buffer(ap, true);
        int32_t* samples = (int32_t*) ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(ap, ab);
    }
    
    audio_i2s_set_enabled(true);
}

void i2s_audio_deinit(void) {
    if (ap == NULL) return;

    audio_i2s_set_enabled(false);
    audio_i2s_end();
    
    audio_buffer_pool_t *old_ap = ap;
    ap = NULL;

    audio_buffer_t* ab;

    ab = take_audio_buffer(old_ap, false);
    while (ab != NULL) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(old_ap, false);
    }
    ab = get_free_audio_buffer(old_ap, false);
    while (ab != NULL) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(old_ap, false);
    }
    ab = get_full_audio_buffer(old_ap, false);
    while (ab != NULL) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(old_ap, false);
    }
    free(old_ap);
}
