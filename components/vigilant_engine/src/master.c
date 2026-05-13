#include "master.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vigilant.h"

static const char* TAG = "ve-master";

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "Received %.*s", evt->data_len, (char*)evt->data);
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error");
            break;

        default:
            break;
    }

    return ESP_OK;
}

static void http_fetch_task(void* arg) {
    const TickType_t interval = pdMS_TO_TICKS(10000);  // every 10s
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // get connected wifi devices
        VigilantWiFiInfo wifi_info;
        vigilant_get_wifiinfo(&wifi_info);

        esp_http_client_config_t config = {
            .url = "http://example.com/api/status",
            .event_handler = http_event_handler,
            .timeout_ms = 5000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            int length = esp_http_client_get_content_length(client);

            ESP_LOGI(TAG, "HTTP status=%d, length=%d", status, length);
        } else {
            ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);

        vTaskDelayUntil(&last_wake, interval);
    }
}

esp_err_t init_master_mode() {
    xTaskCreate(http_fetch_task, "http_fetch_task", 8192, NULL, 5, NULL);
    return ESP_OK;
}
