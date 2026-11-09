#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tcan4550.h"
#include "vigilant.h"

/* Change this to 20000000 if the board uses a 20 MHz crystal/clock. */
#define TCAN4550_OSCILLATOR_HZ 40000000U
#define TCAN4550_NOMINAL_BITRATE 500000U
#define TCAN4550_DATA_BITRATE 2000000U

static void init_can(void) {
    const tcan4550_config_t config = {
        .spi_host = SPI2_HOST,
        .cs_io = CONFIG_TCAN4550_CS_IO,
        .mosi_io = CONFIG_TCAN4550_MOSI_IO,
        .miso_io = CONFIG_TCAN4550_MISO_IO,
        .sclk_io = CONFIG_TCAN4550_SCLK_IO,
        .spi_clock_hz = 2000000U,
        .oscillator_hz = TCAN4550_OSCILLATOR_HZ,
        .nominal_bitrate = TCAN4550_NOMINAL_BITRATE,
        .data_bitrate = TCAN4550_DATA_BITRATE,
    };

    ESP_ERROR_CHECK(tcan4550_init(&config));
    ESP_ERROR_CHECK(tcan4550_start(false));
}

void app_main(void) {
    VigilantConfig VgConfig = {.unique_component_name = "Vigilant ESP Test",
                               .network_mode = NW_MODE_APSTA};
    ESP_ERROR_CHECK(vigilant_init(VgConfig));
    init_can();
}
