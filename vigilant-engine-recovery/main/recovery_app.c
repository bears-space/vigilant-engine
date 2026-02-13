#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_http_server.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ve_recovery";

// ---- AP Config ----
#define RECOVERY_AP_SSID      "VE-Recovery"
#define RECOVERY_AP_PASS      "aerobear"   // >= 8 chars for WPA2; set "" for open AP
#define RECOVERY_AP_CHANNEL   6
#define RECOVERY_MAX_CONN     2

// ---- OTA ----
#define OTA_BUF_SIZE 2048

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[]   asm("_binary_index_html_end");

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static const esp_partition_t *find_ota0_partition(void)
{
    // Hard-pin to ota_0 as you requested
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        NULL
    );
    return p;
}

static esp_err_t boot_post_handler(httpd_req_t *req)
{
    const esp_partition_t *ota0 = find_ota0_partition();
    if (!ota0) {
        ESP_LOGE(TAG, "ota_0 partition not found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_0 not found");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_set_boot_partition(ota0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot_partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Boot partition set to ota_0. Rebooting…");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK. Rebooting to ota_0...\n");

    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = find_ota0_partition();
    if (!update_partition) {
        ESP_LOGE(TAG, "ota_0 partition not found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_0 not found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA target: label=%s subtype=0x%02x offset=0x%lx size=0x%lx",
             update_partition->label,
             update_partition->subtype,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    if (req->content_len > update_partition->size) {
        ESP_LOGE(TAG, "Image too large: %d > 0x%lx", req->content_len, (unsigned long)update_partition->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image too large for ota_0");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int written_total = 0;

    while (remaining > 0) {
        int to_read = remaining > OTA_BUF_SIZE ? OTA_BUF_SIZE : remaining;
        int r = httpd_req_recv(req, (char *)buf, to_read);

        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue; // retry
        }
        if (r < 0) {
            ESP_LOGE(TAG, "httpd_req_recv error: %d", r);
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        if (r == 0) {
            ESP_LOGE(TAG, "client closed connection early");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "connection closed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }

        remaining -= r;
        written_total += r;
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot_partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA OK: wrote %d bytes. Rebooting to ota_0…", written_total);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK. Rebooting to ota_0...\n");

    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

static httpd_handle_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t update_uri = {
        .uri      = "/update",
        .method   = HTTP_POST,
        .handler  = ota_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t boot_uri = {
        .uri      = "/boot",
        .method   = HTTP_POST,
        .handler  = boot_post_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &update_uri);
    httpd_register_uri_handler(server, &boot_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        abort();
    }


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, RECOVERY_AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(RECOVERY_AP_SSID);
    ap_cfg.ap.channel = RECOVERY_AP_CHANNEL;
    ap_cfg.ap.max_connection = RECOVERY_MAX_CONN;

    if (strlen(RECOVERY_AP_PASS) == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strncpy((char *)ap_cfg.ap.password, RECOVERY_AP_PASS, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID='%s' PASS='%s' (IP usually 192.168.4.1)",
             RECOVERY_AP_SSID, strlen(RECOVERY_AP_PASS) ? RECOVERY_AP_PASS : "<open>");
}

void app_main(void)
{
    // NVS required for WiFi on many setups
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running from: label=%s subtype=0x%02x offset=0x%lx",
             running->label, running->subtype, (unsigned long)running->address);

    wifi_init_softap();
    start_http_server();

    // Keep main task alive
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
