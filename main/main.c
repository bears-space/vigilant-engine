#include <unistd.h>
#include "esp_log.h"
#include "vigilant.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// for ws functionality
#include "http_server.h"
#include "ws_mgr.h"

static const char *TAG = "app_main";

/* ===== Dein eigener Handler ===== */
static esp_err_t ping_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "pong\n");
    return ESP_OK;
}

/* ===== Deine eigene URI ===== */
static const httpd_uri_t ping_uri = {
    .uri     = "/ping",
    .method  = HTTP_GET,
    .handler = ping_get_handler,
    .user_ctx = NULL,
};

void app_main(void)
{
    VigilantConfig VgConfig = {
        .unique_component_name = "Vigliant ESP Test",
        .network_mode = NW_MODE_APSTA
    };
    ESP_ERROR_CHECK(vigilant_init(VgConfig));

    // Wichtig: Server muss laufen, bevor du registrierst.
    // Falls vigilant_init den Server startet, ist das schon okay.
    // Sonst: ESP_ERROR_CHECK(http_server_start());

    ESP_ERROR_CHECK(http_server_register_uri(&ping_uri));
    ESP_LOGI(TAG, "Registered /ping");

    while (1) {
        ESP_ERROR_CHECK(status_led_set_rgb(100, 100, 100));
        ws_mgr_broadcast_text("{\"led\":\"on\"}");
        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_ERROR_CHECK(status_led_off());
        ws_mgr_broadcast_text("{\"led\":\"off\"}");
        vTaskDelay(pdMS_TO_TICKS(300));
        
    }
}
