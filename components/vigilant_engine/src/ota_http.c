#include "ota_http.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "status_led.h"

static const char* TAG_OTA = "ota_http";

// compiler embedded file symbols
extern const unsigned char update_html_start[] asm(
    "_binary_vigilant_html_start");  // HTML Vigilant File Start
extern const unsigned char update_html_end[] asm(
    "_binary_vigilant_html_end");  // HTML Vigilant File End

#define OTA_RECV_BUF_SIZE 1024
#define REBOOT_AFTER_RESPONSE_DELAY_MS 1000

static void deferred_restart_task(void* arg) {
    const char* reason = (const char*)arg;
    ESP_LOGI(TAG_OTA, "Restarting after %s", reason ? reason : "request");
    vTaskDelay(pdMS_TO_TICKS(REBOOT_AFTER_RESPONSE_DELAY_MS));
    esp_restart();
}

static void schedule_restart(const char* reason) {
    BaseType_t created = xTaskCreate(deferred_restart_task, "ve_restart", 2048,
                                     (void*)reason, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGW(TAG_OTA, "Failed to defer restart; restarting immediately");
        esp_restart();
    }
}

static esp_err_t reboot_factory_handler(httpd_req_t* req) {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

    if (!factory) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No factory partition");
        status_led_set_state(STATUS_STATE_ERROR);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_ota_set_boot_partition(factory));
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "OK, rebooting to factory...");
    schedule_restart("factory reboot");
    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "OK, rebooting...");
    schedule_restart("reboot");
    return ESP_OK;
}

static esp_err_t dashboard_get_handler(httpd_req_t* req) {
    size_t html_size = update_html_end - update_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char*)update_html_start, html_size);
    return ESP_OK;
}

esp_err_t ota_http_register_handlers(httpd_handle_t server) {
    // GET /rebootfactory -> Reboot to factory partition
    static const httpd_uri_t ota_reboot_factory_get_uri = {
        .uri = "/rebootfactory",
        .method = HTTP_GET,
        .handler = reboot_factory_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t ota_reboot_post_uri = {
        .uri = "/reboot",
        .method = HTTP_POST,
        .handler = reboot_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t vigilant_get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = dashboard_get_handler,
        .user_ctx = NULL,
    };

    esp_err_t err;

    err = httpd_register_uri_handler(server, &ota_reboot_factory_get_uri);
    if (err == ESP_OK) {
        ESP_LOGI(
            TAG_OTA,
            "Registered Reboot Factory HTTP GET handler at /rebootfactory");
    } else {
        ESP_LOGE(TAG_OTA, "Failed to register Reboot Factory GET handler (%s)",
                 esp_err_to_name(err));
        status_led_set_state(STATUS_STATE_INFO);
        return err;
    }

    err = httpd_register_uri_handler(server, &ota_reboot_post_uri);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA, "Registered Reboot HTTP POST handler at /reboot");
    } else {
        ESP_LOGE(TAG_OTA, "Failed to register Reboot POST handler (%s)",
                 esp_err_to_name(err));
        status_led_set_state(STATUS_STATE_INFO);
        return err;
    }

    err = httpd_register_uri_handler(server, &vigilant_get_uri);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA,
                 "Registered Vigilant Dashboard HTTP GET handler at /");
    } else {
        ESP_LOGE(TAG_OTA,
                 "Failed to register Vigilant Dashboard GET handler (%s)",
                 esp_err_to_name(err));
        status_led_set_state(STATUS_STATE_INFO);
        return err;
    }

    return err;
}
