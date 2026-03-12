#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "bme280.h"
#include "bme280_defs.h"

#define BME280_ADDRESS  0x76

int8_t bme280_i2c_read(uint8_t reg, uint8_t *data, uint32_t len, void *intf_ptr) {
    i2c_inst_t *i2c = (i2c_inst_t *)intf_ptr;

    if (i2c_write_blocking(i2c, BME280_ADDRESS, &reg, 1, true) < 0)
        return BME280_E_COMM_FAIL;

    if (i2c_read_blocking(i2c, BME280_ADDRESS, data, len, false) < 0)
        return BME280_E_COMM_FAIL;

    return BME280_OK;
}

int8_t bme280_i2c_write(uint8_t reg, const uint8_t *data, uint32_t len, void *intf_ptr) {
    uint8_t buf[len + 1];
    buf[0] = reg;
    for (uint32_t i = 0; i < len; i++)
        buf[i + 1] = data[i];

    if (i2c_write_blocking((i2c_inst_t *)intf_ptr, BME280_ADDRESS, buf, len + 1, false) < 0)
        return BME280_E_COMM_FAIL;

    return BME280_OK;
}

void bme280_delay_us(uint32_t period, void *intf_ptr) {
    sleep_us(period);
}