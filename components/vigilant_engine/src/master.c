#include "master.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
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
    VigilantWiFiInfo wifi_info;

    while (true) {
        // get connected wifi devices
        vigilant_get_wifiinfo(&wifi_info);

        if (wifi_info.connected_devices_count > 0) {
            ESP_LOGI(TAG, "Starting polling for %d connected devices",
                     wifi_info.connected_devices_count);
        } else {
            ESP_LOGI(TAG, "No connected WiFi devices");
        }

        for (size_t i = 0; i < wifi_info.connected_devices_count; i++) {
            VigilantWifiDevice* device = &wifi_info.connected_devices[i];
            esp_ip4_addr_t ip = {.addr = device->address};

            ESP_LOGI(TAG, "Connected device %zu: %s at " IPSTR, i + 1,
                     device->name, IP2STR(&ip));

            if (device->is_vigilant_device ||
                device->is_vigilant_device ==
                    0) {  // this doesnt make any sense

                char url_buffer[64];
                // Hier wird die IP-Adresse tatsächlich in den String
                // geschrieben
                snprintf(url_buffer, sizeof(url_buffer),
                         "http://" IPSTR "/info", IP2STR(&ip));

                esp_http_client_config_t config = {
                    .url = url_buffer,
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
                    // we assume the device is not a vigilant device if we fail
                    // to connect to it, but we dont want to mark it as
                    // non-vigilant in case of transient network issues, so we
                    // set it to NULL
                    device->is_vigilant_device = NULL;
                }

                esp_http_client_cleanup(client);
            }
        }

        vTaskDelayUntil(&last_wake, interval);
    }
}

esp_err_t init_master_mode() {
    xTaskCreate(http_fetch_task, "http_fetch_task", 8192, NULL, 5, NULL);
    return ESP_OK;
}
