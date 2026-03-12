#define I2S_PIN_BCK   0
#define I2S_PIN_LRCK  1
#define I2S_PIN_DATA  2

#define SAMPLES_PER_BUFFER            PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH
#define DAC_ZERO                      0

#include "pico/audio_i2s.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include <stdio.h>
#include <math.h>


extern audio_buffer_pool_t *ap;
extern volatile bool is_i2s_init_pending;
void i2s_setup(uint32_t samp_freq);
void i2s_audio_init(uint32_t sample_freq);
void i2s_audio_deinit(void);
