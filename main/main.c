#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor/example_sensor.c"
#include "status_led.h"
#include "vigilant.h"
// static const char *TAG = "app_main";

void app_main(void) {
    VigilantConfig VgConfig = {.unique_component_name = "Vigilant ESP Test",
                               .network_mode = NW_MODE_APSTA};
    ESP_ERROR_CHECK(vigilant_init(VgConfig));

    // now start the example sensor task
    example_sensor_init();
}
