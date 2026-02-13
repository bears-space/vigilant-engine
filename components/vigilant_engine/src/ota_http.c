#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "ota_http.h"
#include "status_led.h"


static const char *TAG_OTA = "ota_http";

// compiler embedded file symbols
extern const unsigned char update_html_start[] asm("_binary_vigilant_html_start"); // HTML Vigilant File Start
extern const unsigned char update_html_end[]   asm("_binary_vigilant_html_end");  // HTML Vigilant File End

#define OTA_RECV_BUF_SIZE 1024

static esp_err_t reboot_factory_handler(httpd_req_t *req)
{
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                 ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                 NULL);

    if (!factory) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No factory partition");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK, rebooting to factory...");

    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(factory));
    ESP_ERROR_CHECK(status_led_off());
    esp_restart();
    return ESP_OK;
}

static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    size_t html_size = update_html_end - update_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)update_html_start, html_size);
    return ESP_OK;
}

esp_err_t ota_http_register_handlers(httpd_handle_t server)
{
    // GET /rebootfactory -> Reboot to factory partition
    static const httpd_uri_t ota_reboot_factory_get_uri = {
        .uri       = "/rebootfactory",
        .method    = HTTP_GET,
        .handler   = reboot_factory_handler,
        .user_ctx  = NULL,
    };

    static const httpd_uri_t vigilant_get_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = dashboard_get_handler,
        .user_ctx  = NULL,
    };

    esp_err_t err;

    err = httpd_register_uri_handler(server, &ota_reboot_factory_get_uri);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA, "Registered Reboot Factory HTTP GET handler at /rebootfactory");
    } else {
        ESP_LOGE(TAG_OTA, "Failed to register Reboot Factory GET handler (%s)", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &vigilant_get_uri);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA, "Registered Vigilant Dashboard HTTP GET handler at /");
    } else {
        ESP_LOGE(TAG_OTA, "Failed to register Vigilant Dashboard GET handler (%s)", esp_err_to_name(err));
        return err;
    }

    return err;
}
