#include "vigilant.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "lwip/inet.h"
#include "status_led.h"

#include "http_server.h"
#include "websocket.h"

static const char *TAG = "vigilant";

static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap  = NULL;
static VigilantConfig s_cfg = {0};

static wifi_config_t sta_cfg = {
    .sta = {
        .ssid = "aerobear",
        .password = "aerobear",
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
    }
};

static wifi_config_t ap_cfg = {
    .ap = {
        .ssid = "aerobear-SETUP",      //made unique in later
        .ssid_len = 0,
        .password = "aerobear",        // leer => open AP, dann aber auch authmode anpassen
        .channel = 1,
        .max_connection = 4,
        .authmode = WIFI_AUTH_WPA2_PSK 
    }
};

static void ap_cfg_fixup(wifi_config_t *cfg)
{
    // Wenn password leer ist: auth auf OPEN setzen, sonst meckert IDF rum
    if (cfg->ap.password[0] == '\0') {
        cfg->ap.authmode = WIFI_AUTH_OPEN;
    }
}

void reboot_to_recovery(void)
{
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                 ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                 NULL);
    if (!factory) return;

    esp_ota_set_boot_partition(factory);
    esp_restart();
}

static void wifi_evt(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t*)data;
        ESP_LOGW("wifi", "STA disconnected, reason=%d", e->reason);
        // optional: reconnect
        esp_wifi_connect();
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t*)data;
        ESP_LOGI("wifi", "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static esp_err_t wifi_init_once(void)
{
    static bool inited = false;
    if (inited) return ESP_OK;

    // Netifs 
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_evt, NULL));


    inited = true;
    return ESP_OK;
}

static esp_err_t wifi_apply_mode(NW_MODE mode,
                                wifi_config_t *sta,
                                wifi_config_t *ap)
{
    // Stop kann beim ersten Mal "not started" sein -> nicht dramatisch
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK &&
        err != ESP_ERR_WIFI_NOT_STARTED &&
        err != ESP_ERR_WIFI_NOT_INIT) {
        return err;
    }

    wifi_mode_t m;
    if (mode == NW_MODE_STA)      m = WIFI_MODE_STA;
    else if (mode == NW_MODE_AP)  m = WIFI_MODE_AP;
    else if (mode == NW_MODE_APSTA) m = WIFI_MODE_APSTA;
    else return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(esp_wifi_set_mode(m));

    if (mode == NW_MODE_STA || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, sta));
    }
    if (mode == NW_MODE_AP || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, ap));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    if (mode == NW_MODE_STA || mode == NW_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_connect()); // AP-only: NICHT connecten
    }

    return ESP_OK;
}

esp_err_t vigilant_init(VigilantConfig VgConfig)
{
    uint8_t mac[6];
    status_led_config_t led_cfg = {
        .gpio = CONFIG_VE_STATUS_LED_GPIO,
        .resolution_hz = 10 * 1000 * 1000,
        .max_leds = 1,
        .invert_out = false,
        .with_dma = false,
        .mem_block_symbols = 64,
    };
    ESP_ERROR_CHECK(status_led_init(&led_cfg));

    ESP_LOGI(TAG, "Init NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    // Capture ESP-IDF logs early so they can be replayed to websocket clients
    websocket_init_log_capture();

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf((char*)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), 
             "aerobear-%02X%02X", mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Init netif + event loop");
    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    ESP_LOGI(TAG, "Init WiFi driver");
    ESP_ERROR_CHECK(wifi_init_once());

    ap_cfg_fixup(&ap_cfg);

    ESP_LOGI(TAG, "Starting WiFi... mode=%d", (int)VgConfig.network_mode);
    ESP_ERROR_CHECK(wifi_apply_mode(VgConfig.network_mode, &sta_cfg, &ap_cfg));

    ESP_LOGI(TAG, "Registering HTTP server event handlers");
    ESP_ERROR_CHECK(http_server_register_event_handlers());

    ESP_LOGI(TAG, "Starting HTTP server");
    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "Vigilant initialized successfully!");
    ESP_LOGI(TAG, "This node unique name is: %s", VgConfig.unique_component_name);
    s_cfg = VgConfig;
    return ESP_OK;
}

esp_err_t vigilant_get_info(VigilantInfo *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));
    memcpy(info->unique_component_name, s_cfg.unique_component_name, sizeof(info->unique_component_name));
    info->network_mode = s_cfg.network_mode;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(info->mac, sizeof(info->mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(info->ap_ssid, sizeof(info->ap_ssid), "%s", (const char*)ap_cfg.ap.ssid);
    snprintf(info->sta_ssid, sizeof(info->sta_ssid), "%s", (const char*)sta_cfg.sta.ssid);

    esp_netif_ip_info_t ip_sta = {0};
    if (s_netif_sta && esp_netif_get_ip_info(s_netif_sta, &ip_sta) == ESP_OK) {
        inet_ntoa_r(ip_sta.ip, info->ip_sta, sizeof(info->ip_sta));
    } else {
        strcpy(info->ip_sta, "0.0.0.0");
    }

    esp_netif_ip_info_t ip_ap = {0};
    if (s_netif_ap && esp_netif_get_ip_info(s_netif_ap, &ip_ap) == ESP_OK) {
        inet_ntoa_r(ip_ap.ip, info->ip_ap, sizeof(info->ip_ap));
    } else {
        strcpy(info->ip_ap, "0.0.0.0");
    }

    return ESP_OK;
}
