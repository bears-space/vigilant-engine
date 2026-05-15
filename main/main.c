#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "vigilant.h"

// static const char *TAG = "app_main";

#if CONFIG_VE_ENABLE_WIFI_MASTER
#define VE_APP_IS_MASTER true
#else
#define VE_APP_IS_MASTER false
#endif

void app_main(void) {
    VigilantConfig VgConfig = {.unique_component_name = "Vigilant ESP Test",
                               .network_mode = NW_MODE_APSTA,
                               .is_master = VE_APP_IS_MASTER};
    ESP_ERROR_CHECK(vigilant_init(VgConfig));
}
