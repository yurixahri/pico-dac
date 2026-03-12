#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "pico-ssd1306/ssd1306.h"
#include "bme280_pico.h"

// Audio
#include "tusb.h"
#include "uac.h"
#include "i2s.h"
#include "hardware/clocks.h"


#define SDA_PIN 26
#define SCL_PIN 27
#define DISPLAY_ADDRESS 0x3C

ssd1306_t display;
struct bme280_dev bme = {
    .intf = BME280_I2C_INTF,
    .read = bme280_i2c_read,
    .write = bme280_i2c_write,
    .delay_us = bme280_delay_us,
    .intf_ptr = i2c1
};

void setup_i2c() {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

void setup_display(){
    sleep_ms(100);
    
    bool ok;
    display.external_vcc = false;
    ok = ssd1306_init(&display, 128, 64, DISPLAY_ADDRESS, i2c1);
    if (!ok) {
        printf("SSD1306 init FAILED\n");
        while (1) tight_loop_contents();
    }

    printf("SSD1306 init OK\n");
    ssd1306_clear(&display);
    ssd1306_draw_string(&display, 0, 0, 1, "Hello, SSD1306!");
    ssd1306_show(&display);
}

void setup_bme280(){
    struct bme280_settings settings = {
        .osr_h = BME280_OVERSAMPLING_1X,
        .osr_p = BME280_OVERSAMPLING_1X,
        .osr_t = BME280_OVERSAMPLING_1X,
        .filter = BME280_FILTER_COEFF_2,
        .standby_time = BME280_STANDBY_TIME_0_5_MS
    };

    if (bme280_init(&bme) != BME280_OK) {
        printf("BME280 init FAILED\n");
        while (1) tight_loop_contents();
    }
    bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &bme);
    bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &bme);

    printf("BME280 init OK\n");
}

void core1_main(){
    while (true) {
        struct bme280_data data;
        char temp_str[20];
        bme280_get_sensor_data(BME280_ALL, &data, &bme);
        snprintf(temp_str, sizeof(temp_str), "T: %.2f C", data.temperature);
        ssd1306_clear(&display);
        ssd1306_draw_string(&display, 0, 0, 1, temp_str);
        ssd1306_show(&display);
        
        
        sleep_ms(100);
    }
}

int main() {
    set_sys_clock_48mhz();
    setup_i2c();

    setup_display();
    setup_bme280();

    multicore_launch_core1(core1_main);

    gpio_init(3);
    gpio_set_function(3, GPIO_FUNC_SIO);
    gpio_set_dir(3, GPIO_OUT);
    gpio_put(3, 0);

    board_init();
    tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    board_init_after_tusb();
    i2s_setup(41000);
    while (true){
        tud_task();
        
        if (freq_changed){
            if (!is_i2s_init_pending) {
                i2s_setup(get_current_sample_rate());
                tud_audio_clear_ep_out_ff();
                freq_changed = false;
                //multicore_lockout_end_blocking();
            }
        }
        audio_task();
        led_blinking_task();

        #if CFG_AUDIO_DEBUG
            audio_debug_task();
        #endif

    }
    
}