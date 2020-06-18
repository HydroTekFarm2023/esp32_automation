/**
 * ESP-IDF driver for BME280 digital environmental sensor
 *
 * Forked from <https://github.com/gschorcht/bme280-esp-idf>
 *
 * Copyright (C) 2017 Gunar Schorcht <https://github.com/gschorcht>\n
 * Copyright (C) 2019 Ruslan V. Uss <https://github.com/UncleRus>
 *
 * BSD Licensed as described in the file LICENSE
 */
#include <bme280.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_idf_lib_helpers.h>

#define I2C_FREQ_HZ 1000000 // Up to 3.4MHz, but esp-idf only supports 1MHz

// modes: unfortunatly, only SLEEP_MODE and FORCED_MODE are documented
#define BME280_SLEEP_MODE           0x00    // low power sleeping
#define BME280_FORCED_MODE          0x01    // perform one TPHG cycle (field data 0 filled)
#define BME280_PARALLEL_MODE        0x02    // no information what it does :-(
#define BME280_SQUENTUAL_MODE       0x02    // no information what it does (field data 0+1+2 filled)

// register addresses
#define BME280_REG_RES_HEAT_VAL     0x00
#define BME280_REG_RES_HEAT_RANGE   0x02
#define BME280_REG_RANGE_SW_ERROR   0x06

#define BME280_REG_IDAC_HEAT_BASE   0x50    // 10 regsrs idac_heat_0 ... idac_heat_9
#define BME280_REG_RES_HEAT_BASE    0x5a    // 10 registers res_heat_0 ... res_heat_9
#define BME280_REG_GAS_WAIT_BASE    0x64    // 10 registers gas_wait_0 ... gas_wait_9
#define BME280_REG_CTRL_GAS_0       0x70
#define BME280_REG_CTRL_GAS_1       0x71
#define BME280_REG_CTRL_HUM         0x72
#define BME280_REG_STATUS           0x73
#define BME280_REG_CTRL_MEAS        0x74
#define BME280_REG_CONFIG           0x75
#define BME280_REG_ID               0xd0
#define BME280_REG_RESET            0xe0

// field data 0 registers
#define BME280_REG_MEAS_STATUS_0    0x1d
#define BME280_REG_MEAS_INDEX_0     0x1e
#define BME280_REG_PRESS_MSB_0      0x1f
#define BME280_REG_PRESS_LSB_0      0x20
#define BME280_REG_PRESS_XLSB_0     0x21
#define BME280_REG_TEMP_MSB_0       0x22
#define BME280_REG_TEMP_LSB_0       0x23
#define BME280_REG_TEMP_XLSB_0      0x24
#define BME280_REG_HUM_MSB_0        0x25
#define BME280_REG_HUM_LSB_0        0x26
#define BME280_REG_GAS_R_MSB_0      0x2a
#define BME280_REG_GAS_R_LSB_0      0x2b

// field data 1 registers (not documented, used in SEQUENTIAL_MODE)
#define BME280_REG_MEAS_STATUS_1    0x2e
#define BME280_REG_MEAS_INDEX_1     0x2f

// field data 2 registers (not documented, used in SEQUENTIAL_MODE)
#define BME280_REG_MEAS_STATUS_2    0x3f
#define BME280_REG_MEAS_INDEX_2     0x40

// field data addresses
#define BME280_REG_RAW_DATA_0       BME280_REG_MEAS_STATUS_0    // 0x1d ... 0x2b
#define BME280_REG_RAW_DATA_1       BME280_REG_MEAS_STATUS_1    // 0x2e ... 0x3c
#define BME280_REG_RAW_DATA_2       BME280_REG_MEAS_STATUS_2    // 0x40 ... 0x4d
#define BME280_REG_RAW_DATA_LEN     (BME280_REG_GAS_R_LSB_0 - BME280_REG_MEAS_STATUS_0 + 1)

// calibration data registers
#define BME280_REG_CD1_ADDR         0x89    // 25 byte calibration data
#define BME280_REG_CD1_LEN          25
#define BME280_REG_CD2_ADDR         0xe1    // 16 byte calibration data
#define BME280_REG_CD2_LEN          16
#define BME280_REG_CD3_ADDR         0x00    //  8 byte device specific calibration data
#define BME280_REG_CD3_LEN          8

// register structure definitions
#define BME280_NEW_DATA_BITS        0x80    // BME280_REG_MEAS_STATUS<7>
#define BME280_NEW_DATA_SHIFT       7       // BME280_REG_MEAS_STATUS<7>
#define BME280_GAS_MEASURING_BITS   0x40    // BME280_REG_MEAS_STATUS<6>
#define BME280_GAS_MEASURING_SHIFT  6       // BME280_REG_MEAS_STATUS<6>
#define BME280_MEASURING_BITS       0x20    // BME280_REG_MEAS_STATUS<5>
#define BME280_MEASURING_SHIFT      5       // BME280_REG_MEAS_STATUS<5>
#define BME280_GAS_MEAS_INDEX_BITS  0x0f    // BME280_REG_MEAS_STATUS<3:0>
#define BME280_GAS_MEAS_INDEX_SHIFT 0       // BME280_REG_MEAS_STATUS<3:0>

#define BME280_GAS_R_LSB_BITS       0xc0    // BME280_REG_GAS_R_LSB<7:6>
#define BME280_GAS_R_LSB_SHIFT      6       // BME280_REG_GAS_R_LSB<7:6>
#define BME280_GAS_VALID_BITS       0x20    // BME280_REG_GAS_R_LSB<5>
#define BME280_GAS_VALID_SHIFT      5       // BME280_REG_GAS_R_LSB<5>
#define BME280_HEAT_STAB_R_BITS     0x10    // BME280_REG_GAS_R_LSB<4>
#define BME280_HEAT_STAB_R_SHIFT    4       // BME280_REG_GAS_R_LSB<4>
#define BME280_GAS_RANGE_R_BITS     0x0f    // BME280_REG_GAS_R_LSB<3:0>
#define BME280_GAS_RANGE_R_SHIFT    0       // BME280_REG_GAS_R_LSB<3:0>

#define BME280_HEAT_OFF_BITS        0x04    // BME280_REG_CTRL_GAS_0<3>
#define BME280_HEAT_OFF_SHIFT       3       // BME280_REG_CTRL_GAS_0<3>

#define BME280_RUN_GAS_BITS         0x10    // BME280_REG_CTRL_GAS_1<4>
#define BME280_RUN_GAS_SHIFT        4       // BME280_REG_CTRL_GAS_1<4>
#define BME280_NB_CONV_BITS         0x0f    // BME280_REG_CTRL_GAS_1<3:0>
#define BME280_NB_CONV_SHIFT        0       // BME280_REG_CTRL_GAS_1<3:0>

#define BME280_SPI_3W_INT_EN_BITS   0x40    // BME280_REG_CTRL_HUM<6>
#define BME280_SPI_3W_INT_EN_SHIFT  6       // BME280_REG_CTRL_HUM<6>
#define BME280_OSR_H_BITS           0x07    // BME280_REG_CTRL_HUM<2:0>
#define BME280_OSR_H_SHIFT          0       // BME280_REG_CTRL_HUM<2:0>

#define BME280_OSR_T_BITS           0xe0    // BME280_REG_CTRL_MEAS<7:5>
#define BME280_OSR_T_SHIFT          5       // BME280_REG_CTRL_MEAS<7:5>
#define BME280_OSR_P_BITS           0x1c    // BME280_REG_CTRL_MEAS<4:2>
#define BME280_OSR_P_SHIFT          2       // BME280_REG_CTRL_MEAS<4:2>
#define BME280_MODE_BITS            0x03    // BME280_REG_CTRL_MEAS<1:0>
#define BME280_MODE_SHIFT           0       // BME280_REG_CTRL_MEAS<1:0>

#define BME280_FILTER_BITS          0x1c    // BME280_REG_CONFIG<4:2>
#define BME280_FILTER_SHIFT         2       // BME280_REG_CONFIG<4:2>
#define BME280_SPI_3W_EN_BITS       0x01    // BME280_REG_CONFIG<0>
#define BME280_SPI_3W_EN_SHIFT      0       // BME280_REG_CONFIG<0>

#define BME280_SPI_MEM_PAGE_BITS    0x10    // BME280_REG_STATUS<4>
#define BME280_SPI_MEM_PAGE_SHIFT   4       // BME280_REG_STATUS<4>

#define BME280_GAS_WAIT_BITS        0x3f    // BME280_REG_GAS_WAIT+x<5:0>
#define BME280_GAS_WAIT_SHIFT       0       // BME280_REG_GAS_WAIT+x<5:0>
#define BME280_GAS_WAIT_MULT_BITS   0xc0    // BME280_REG_GAS_WAIT+x<7:6>
#define BME280_GAS_WAIT_MULT_SHIFT  6       // BME280_REG_GAS_WAIT+x<7:6>

// commands
#define BME280_RESET_CMD            0xb6    // BME280_REG_RESET<7:0>
#define BME280_RESET_PERIOD         5       // reset time in ms

#define BME280_RHR_BITS             0x30    // BME280_REG_RES_HEAT_RANGE<5:4>
#define BME280_RHR_SHIFT            4       // BME280_REG_RES_HEAT_RANGE<5:4>
#define BME280_RSWE_BITS            0xf0    // BME280_REG_RANGE_SW_ERROR<7:4>
#define BME280_RSWE_SHIFT           4       // BME280_REG_RANGE_SW_ERROR<7:4>

// calibration data are stored in a calibration data map
#define BME280_CDM_SIZE (BME280_REG_CD1_LEN + BME280_REG_CD2_LEN + BME280_REG_CD3_LEN)
#define BME280_CDM_OFF1 0
#define BME280_CDM_OFF2 BME280_REG_CD1_LEN
#define BME280_CDM_OFF3 BME280_CDM_OFF2 + BME280_REG_CD2_LEN

// calibration parameter offsets in calibration data map
// calibration data from 0x89
#define BME280_CDM_T2   1
#define BME280_CDM_T3   3
#define BME280_CDM_P1   5
#define BME280_CDM_P2   7
#define BME280_CDM_P3   9
#define BME280_CDM_P4   11
#define BME280_CDM_P5   13
#define BME280_CDM_P7   15
#define BME280_CDM_P6   16
#define BME280_CDM_P8   19
#define BME280_CDM_P9   21
#define BME280_CDM_P10  23
// calibration data from 0e1
#define BME280_CDM_H2   25
#define BME280_CDM_H1   26
#define BME280_CDM_H3   28
#define BME280_CDM_H4   29
#define BME280_CDM_H5   30
#define BME280_CDM_H6   31
#define BME280_CDM_H7   32
#define BME280_CDM_T1   33
#define BME280_CDM_GH2  35
#define BME280_CDM_GH1  37
#define BME280_CDM_GH3  38
// device specific calibration data from 0x00
#define BME280_CDM_RHV  41      // 0x00 - res_heat_val
#define BME280_CDM_RHR  43      // 0x02 - res_heat_range
#define BME280_CDM_RSWE 45      // 0x04 - range_sw_error

static const char *TAG = "BME280";

#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define CHECK_LOGE(x, msg, ...) do { \
        esp_err_t __; \
        if ((__ = x) != ESP_OK) { \
            ESP_LOGE(TAG, msg, ## __VA_ARGS__); \
            return __; \
        } \
    } while (0)

/**
 * @brief   Raw data (integer values) read from sensor
 */
typedef struct
{
    uint32_t temperature;    // degree celsius x100
    uint16_t humidity;       // relative humidity x1000 in %

    uint8_t meas_index;

} bme280_raw_data_t;

#define lsb_msb_to_type(t,b,o) (t)(((t)b[o+1] << 8) | b[o])
#define lsb_to_type(t,b,o)     (t)(b[o])
#define bme_set_reg_bit(byte, bitname, bit) ( (byte & ~bitname##_BITS) | \
                                              ((bit << bitname##_SHIFT) & bitname##_BITS) )
#define bme_get_reg_bit(byte, bitname)      ( (byte & bitname##_BITS) >> bitname##_SHIFT )

static inline esp_err_t read_reg_8_nolock(bme280_t *dev, uint8_t reg, uint8_t *data)
{
    return i2c_dev_read_reg(&dev->i2c_dev, reg, data, 1);
}

static inline esp_err_t write_reg_8_nolock(bme280_t *dev, uint8_t reg, uint8_t data)
{
    return i2c_dev_write_reg(&dev->i2c_dev, reg, &data, 1);
}

static esp_err_t read_reg_8(bme280_t *dev, uint8_t reg, uint8_t *data)
{
    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);
    I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, reg, data));
    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    return ESP_OK;
}

static esp_err_t bme280_set_mode(bme280_t *dev, uint8_t mode)
{
    uint8_t reg;

    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);
    I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, BME280_REG_CTRL_MEAS, &reg));
    reg = bme_set_reg_bit(reg, BME280_MODE, mode);
    I2C_DEV_CHECK(&dev->i2c_dev, write_reg_8_nolock(dev, BME280_REG_CTRL_MEAS, reg));
    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    return ESP_OK;
}

#define msb_lsb_xlsb_to_20bit(t,b,o) (t)((t) b[o] << 12 | (t) b[o+1] << 4 | b[o+2] >> 4)
#define msb_lsb_to_type(t,b,o)       (t)(((t)b[o] << 8) | b[o+1])

#define BME280_RAW_P_OFF BME280_REG_PRESS_MSB_0-BME280_REG_MEAS_STATUS_0
#define BME280_RAW_T_OFF (BME280_RAW_P_OFF + BME280_REG_TEMP_MSB_0 - BME280_REG_PRESS_MSB_0)
#define BME280_RAW_H_OFF (BME280_RAW_T_OFF + BME280_REG_HUM_MSB_0 - BME280_REG_TEMP_MSB_0)
#define BME280_RAW_G_OFF (BME280_RAW_H_OFF + BME280_REG_GAS_R_MSB_0 - BME280_REG_HUM_MSB_0)

static esp_err_t bme280_get_raw_data(bme280_t *dev, bme280_raw_data_t *raw_data)
{
    if (!dev->meas_started)
    {
        ESP_LOGE(TAG, "Measurement was not started");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[BME280_REG_RAW_DATA_LEN] = { 0 };

    if (!(dev->meas_status & BME280_NEW_DATA_BITS))
    {
        // read maesurment status from sensor
        CHECK(read_reg_8(dev, BME280_REG_MEAS_STATUS_0, &dev->meas_status));
        // test whether there are new data
        if (!(dev->meas_status & BME280_NEW_DATA_BITS))
        {
            if (dev->meas_status & BME280_MEASURING_BITS)
            {
                ESP_LOGW(TAG, "Measurement is still running");
                return ESP_ERR_INVALID_STATE;
            }
            ESP_LOGW(TAG, "No new data");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    dev->meas_started = false;

    // if there are new data, read raw data from sensor
    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);
    I2C_DEV_CHECK(&dev->i2c_dev, i2c_dev_read_reg(&dev->i2c_dev, BME280_REG_RAW_DATA_0, raw, BME280_REG_RAW_DATA_LEN));
    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    raw_data->temperature    = msb_lsb_xlsb_to_20bit(uint32_t, raw, BME280_RAW_T_OFF);
    raw_data->humidity       = msb_lsb_to_type(uint16_t, raw, BME280_RAW_H_OFF);

    /*
     * BME280_REG_MEAS_STATUS_1, BME280_REG_MEAS_STATUS_2
     * These data are not documented and it is not really clear when they are filled
     */
    ESP_LOGD(TAG, "Raw data: %d %d", raw_data->temperature,
            raw_data->humidity);

    return ESP_OK;
}

/**
 * @brief   Calculate temperature from raw temperature value
 * @ref     BME280 datasheet, page 50
 */
static int16_t bme280_convert_temperature(bme280_t *dev, uint32_t raw_temperature)
{
    bme280_calib_data_t *cd = &dev->calib_data;

    int64_t var1;
    int64_t var2;
    int16_t temperature;

    var1 = ((((raw_temperature >> 3) - ((int32_t) cd->par_t1 << 1))) * ((int32_t) cd->par_t2)) >> 11;
    var2 = (((((raw_temperature >> 4) - ((int32_t) cd->par_t1)) * ((raw_temperature >> 4) - ((int32_t) cd->par_t1))) >> 12)
            * ((int32_t) cd->par_t3)) >> 14;
    cd->t_fine = (int32_t) (var1 + var2);
    temperature = (cd->t_fine * 5 + 128) >> 8;

    return temperature;
}

/**
 * @brief       Calculate humidty from raw humidity data
 * @copyright   Copyright (C) 2017 - 2018 Bosch Sensortec GmbH
 *
 * The algorithm was extracted from the original Bosch Sensortec BME280 driver
 * published as open source. Divisions and multiplications by potences of 2
 * were replaced by shift operations for effeciency reasons.
 *
 * @ref         [BME280_diver](https://github.com/BoschSensortec/BME280_driver)
 */
static uint32_t bme280_convert_humidity(bme280_t *dev, uint16_t raw_humidity)
{
    bme280_calib_data_t *cd = &dev->calib_data;

    int32_t var1;
    int32_t var2;
    int32_t var3;
    int32_t var4;
    int32_t var5;
    int32_t var6;
    int32_t temp_scaled;
    int32_t humidity;

    temp_scaled = (((int32_t) cd->t_fine * 5) + 128) >> 8;
    var1 = (int32_t) (raw_humidity - ((int32_t) ((int32_t) cd->par_h1 << 4)))
            - (((temp_scaled * (int32_t) cd->par_h3) / ((int32_t) 100)) >> 1);
    var2 = ((int32_t) cd->par_h2
            * (((temp_scaled * (int32_t) cd->par_h4) / ((int32_t) 100))
                    + (((temp_scaled * ((temp_scaled * (int32_t) cd->par_h5) / ((int32_t) 100))) >> 6) / ((int32_t) 100))
                    + (int32_t) (1 << 14))) >> 10;
    var3 = var1 * var2;
    var4 = (int32_t) cd->par_h6 << 7;
    var4 = ((var4) + ((temp_scaled * (int32_t) cd->par_h7) / ((int32_t) 100))) >> 4;
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
    var6 = (var4 * var5) >> 1;
    humidity = (((var3 + var6) >> 10) * ((int32_t) 1000)) >> 12;

    if (humidity > 100000) /* Cap at 100%rH */
        humidity = 100000;
    else if (humidity < 0)
        humidity = 0;

    return (uint32_t) humidity;
}

///////////////////////////////////////////////////////////////////////////////

esp_err_t bme280_init_desc(bme280_t *dev, uint8_t addr, i2c_port_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    CHECK_ARG(dev);

    if (addr != BME280_I2C_ADDR_0 &&  addr != BME280_I2C_ADDR_1)
    {
        ESP_LOGE(TAG, "Invalid I2C address");
        return ESP_ERR_INVALID_ARG;
    }

    dev->i2c_dev.port = port;
    dev->i2c_dev.addr = addr;
    dev->i2c_dev.cfg.sda_io_num = sda_gpio;
    dev->i2c_dev.cfg.scl_io_num = scl_gpio;
#if HELPER_TARGET_IS_ESP32
    dev->i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;
#endif

    return i2c_dev_create_mutex(&dev->i2c_dev);
}

esp_err_t bme280_free_desc(bme280_t *dev)
{
    CHECK_ARG(dev);

    return i2c_dev_delete_mutex(&dev->i2c_dev);
}

esp_err_t bme280_init_sensor(bme280_t *dev)
{
    CHECK_ARG(dev);

    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);

    dev->meas_started = false;
    dev->meas_status = 0;
    dev->settings.ambient_temperature = 0;
    dev->settings.osr_temperature = BME280_OSR_NONE;
    dev->settings.osr_humidity = BME280_OSR_NONE;
    dev->settings.filter_size = BME280_IIR_SIZE_0;

    // reset the sensor
    I2C_DEV_CHECK(&dev->i2c_dev, write_reg_8_nolock(dev, BME280_REG_RESET, BME280_RESET_CMD));
    vTaskDelay(pdMS_TO_TICKS(BME280_RESET_PERIOD));

    uint8_t chip_id = 0;
    I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, BME280_REG_ID, &chip_id));
    if (chip_id != 0x61)
    {
        I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);
        ESP_LOGE(TAG, "Chip id %02x is wrong, should be 0x61", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t buf[BME280_CDM_SIZE];
    vTaskDelay(pdMS_TO_TICKS(10));
    I2C_DEV_CHECK(&dev->i2c_dev, i2c_dev_read_reg(&dev->i2c_dev, BME280_REG_CD1_ADDR, buf + BME280_CDM_OFF1, BME280_REG_CD1_LEN));
    I2C_DEV_CHECK(&dev->i2c_dev, i2c_dev_read_reg(&dev->i2c_dev, BME280_REG_CD2_ADDR, buf + BME280_CDM_OFF2, BME280_REG_CD2_LEN));
    I2C_DEV_CHECK(&dev->i2c_dev, i2c_dev_read_reg(&dev->i2c_dev, BME280_REG_CD3_ADDR, buf + BME280_CDM_OFF3, BME280_REG_CD3_LEN));

    dev->calib_data.par_t1 = lsb_msb_to_type(uint16_t, buf, BME280_CDM_T1);
    dev->calib_data.par_t2 = lsb_msb_to_type(int16_t, buf, BME280_CDM_T2);
    dev->calib_data.par_t3 = lsb_to_type(int8_t, buf, BME280_CDM_T3);

    // humidity compensation parameters
    dev->calib_data.par_h1 = (uint16_t) (((uint16_t) buf[BME280_CDM_H1 + 1] << 4) | (buf[BME280_CDM_H1] & 0x0F));
    dev->calib_data.par_h2 = (uint16_t) (((uint16_t) buf[BME280_CDM_H2] << 4) | (buf[BME280_CDM_H2 + 1] >> 4));
    dev->calib_data.par_h3 = lsb_to_type(int8_t, buf, BME280_CDM_H3);
    dev->calib_data.par_h4 = lsb_to_type(int8_t, buf, BME280_CDM_H4);
    dev->calib_data.par_h5 = lsb_to_type(int8_t, buf, BME280_CDM_H5);
    dev->calib_data.par_h6 = lsb_to_type(uint8_t, buf, BME280_CDM_H6);
    dev->calib_data.par_h7 = lsb_to_type(int8_t, buf, BME280_CDM_H7);

    // Set ambient temperature of sensor to default value (25 degree C)
    dev->settings.ambient_temperature = 25;

    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    CHECK(bme280_set_oversampling_rates(dev, BME280_OSR_1X, BME280_OSR_1X, BME280_OSR_1X));
    CHECK(bme280_set_filter_size(dev, BME280_IIR_SIZE_3));

    return ESP_OK;
}

esp_err_t bme280_force_measurement(bme280_t *dev)
{
    CHECK_ARG(dev);
    if (dev->meas_started)
    {
        ESP_LOGE(TAG, "Measurement is already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Set the power mode to forced mode to trigger one TPHG measurement cycle
    CHECK_LOGE(bme280_set_mode(dev, BME280_FORCED_MODE),
            "Could not set forced mode to start TPHG measurement cycle");
    dev->meas_started = true;
    dev->meas_status = 0;

    ESP_LOGD(TAG, "Started measurement");

    return ESP_OK;
}

/**
 * @brief Estimate the measurement duration in RTOS ticks
 *
 * Timing formulas extracted from BME280 datasheet and test in some
 * experiments. They represent the maximum measurement duration.
 */
esp_err_t bme280_get_measurement_duration(const bme280_t *dev, uint32_t *duration)
{
    CHECK_ARG(dev && duration);

    *duration = 0; /* Calculate in us */

    // wake up duration from sleep into forced mode
    *duration += 1250;

    // THP cycle duration which consumes 1963 µs for each measurement at maximum
    if (dev->settings.osr_temperature)
        *duration += (1 << (dev->settings.osr_temperature - 1)) * 2300;
    if (dev->settings.osr_humidity)
        *duration += (1 << (dev->settings.osr_humidity - 1)) * 2300 + 575;

    // round up to next ms (1 us ... 1000 us => 1 ms)
    *duration += 999;
    *duration /= 1000;

    // some ms tolerance
    *duration += 5;

    // ceil to next integer value that is divisible by portTICK_PERIOD_MS and
    // compute RTOS ticks (1 ... portTICK_PERIOD_MS =  1 tick)
    *duration = (*duration + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS;

    // Since first RTOS tick can be shorter than the half of defined tick period,
    // the delay caused by vTaskDelay(duration) might be 1 or 2 ms shorter than
    // computed duration in rare cases. Since the duration is computed for maximum
    // and not for the typical durations and therefore tends to be too long, this
    // should not be a problem. Therefore, only one additional tick used.
    *duration += 1;

    return ESP_OK;
}

esp_err_t bme280_is_measuring(bme280_t *dev, bool *busy)
{
    CHECK_ARG(dev && busy);

    // if measurement wasn't started, it is of course not measuring
    if (!dev->meas_started)
    {
        *busy = false;
        return ESP_OK;
    }

    CHECK(read_reg_8(dev, BME280_REG_MEAS_STATUS_0, &dev->meas_status));
    *busy = dev->meas_status & BME280_MEASURING_BITS ? 1 : 0;

    return ESP_OK;
}

esp_err_t bme280_get_results_fixed(bme280_t *dev, bme280_values_fixed_t *results)
{
    CHECK_ARG(dev && results);

    // fill data structure with invalid values
    results->temperature = INT16_MIN;
    results->humidity = 0;

    bme280_raw_data_t raw;
    CHECK(bme280_get_raw_data(dev, &raw));
    // use compensation algorithms to compute sensor values in fixed point format
    if (dev->settings.osr_temperature) {
        results->temperature = bme280_convert_temperature(dev, raw.temperature);
    }
    if (dev->settings.osr_humidity)
        results->humidity = bme280_convert_humidity(dev, raw.humidity);

    ESP_LOGD(TAG, "Fixed point sensor values - %d/100 deg.C, %d/1000 %%",
            results->temperature, results->humidity);

    return ESP_OK;
}

esp_err_t bme280_get_results_float(bme280_t *dev, bme280_values_float_t *results)
{
    CHECK_ARG(dev && results);

    bme280_values_fixed_t fixed;
    CHECK(bme280_get_results_fixed(dev, &fixed));

    results->temperature = fixed.temperature / 100.0f;
    results->humidity = fixed.humidity / 1000.0f;

    return ESP_OK;
}

esp_err_t bme280_measure_fixed(bme280_t *dev, bme280_values_fixed_t *results)
{
    CHECK_ARG(dev && results);

    uint32_t duration;
    CHECK(bme280_get_measurement_duration(dev, &duration));
    if (duration == 0)
    {
        ESP_LOGE(TAG, "Failed to get measurement duration");
        return ESP_FAIL;
    }

    CHECK(bme280_force_measurement(dev));
    vTaskDelay(duration);

    return bme280_get_results_fixed(dev, results);
}

esp_err_t bme280_measure_float(bme280_t *dev, bme280_values_float_t *results)
{
    CHECK_ARG(dev && results);

    uint32_t duration;
    CHECK(bme280_get_measurement_duration(dev, &duration));
    if (duration == 0)
    {
        ESP_LOGE(TAG, "Failed to get measurement duration");
        return ESP_FAIL;
    }

    CHECK(bme280_force_measurement(dev));
    vTaskDelay(duration);

    return bme280_get_results_float(dev, results);
}

esp_err_t bme280_set_oversampling_rates(bme280_t *dev, bme280_oversampling_rate_t ost,
        bme280_oversampling_rate_t osp, bme280_oversampling_rate_t osh)
{
    CHECK_ARG(dev);

    bool ost_changed = dev->settings.osr_temperature != ost;
    bool osh_changed = dev->settings.osr_humidity != osh;

    if (!ost_changed && !osh_changed)
        return ESP_OK;

    // Set the temperature and humidity oversampling
    dev->settings.osr_temperature = ost;
    dev->settings.osr_humidity = osh;

    uint8_t reg;

    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);
    if (ost_changed)
    {
        // read the current register value
        I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, BME280_REG_CTRL_MEAS, &reg));

        // set changed bit values
        if (ost_changed)
            reg = bme_set_reg_bit(reg, BME280_OSR_T, ost);

        // write back the new register value
        I2C_DEV_CHECK(&dev->i2c_dev, write_reg_8_nolock(dev, BME280_REG_CTRL_MEAS, reg));
    }
    if (osh_changed)
    {
        // read the current register value
        I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, BME280_REG_CTRL_HUM, &reg));

        // set changed bit value
        reg = bme_set_reg_bit(reg, BME280_OSR_H, osh);

        // write back the new register value
        I2C_DEV_CHECK(&dev->i2c_dev, write_reg_8_nolock(dev, BME280_REG_CTRL_HUM, reg));
    }
    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    ESP_LOGD(TAG, "Setting oversampling rates done: osrt=%d osp=%d",
            dev->settings.osr_temperature, dev->settings.osr_humidity);

    return ESP_OK;
}

esp_err_t bme280_set_filter_size(bme280_t *dev, bme280_filter_size_t size)
{
    CHECK_ARG(dev);

    if (dev->settings.filter_size == size)
        return ESP_OK;

    /* Set the temperature and humidity settings */
    dev->settings.filter_size = size;

    uint8_t reg;
    I2C_DEV_TAKE_MUTEX(&dev->i2c_dev);

    // read the current register value
    I2C_DEV_CHECK(&dev->i2c_dev, read_reg_8_nolock(dev, BME280_REG_CONFIG, &reg));
    // set changed bit value
    reg = bme_set_reg_bit(reg, BME280_FILTER, size);
    // write back the new register value
    I2C_DEV_CHECK(&dev->i2c_dev, write_reg_8_nolock(dev, BME280_REG_CONFIG, reg));

    I2C_DEV_GIVE_MUTEX(&dev->i2c_dev);

    ESP_LOGD(TAG, "Setting filter size done: size=%d", dev->settings.filter_size);

    return ESP_OK;
}

esp_err_t bme280_set_ambient_temperature(bme280_t *dev, int16_t ambient)
{
    CHECK_ARG(dev);

    if (dev->settings.ambient_temperature == ambient)
        return ESP_OK;

    // set ambient temperature configuration
    dev->settings.ambient_temperature = ambient; // degree Celsius

    return ESP_OK;
}

