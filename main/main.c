#include <unistd.h>
#include "esp_log.h"
#include "vigilant.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// static const char *TAG = "app_main";

void app_main(void)
{
    VigilantConfig VgConfig = {
        .unique_component_name = "Vigliant ESP Test",
        .network_mode = NW_MODE_APSTA
    };
    ESP_ERROR_CHECK(vigilant_init(VgConfig));

    while (1) {
        ESP_ERROR_CHECK(status_led_set_rgb(100, 100, 100));
        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_ERROR_CHECK(status_led_off());
        vTaskDelay(pdMS_TO_TICKS(300));
        
    }
}
