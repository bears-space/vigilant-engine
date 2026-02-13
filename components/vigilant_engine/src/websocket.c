#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG_WS = "ws";
#define MAX_WS_PAYLOAD (8 * 1024)

typedef struct {
    httpd_handle_t hd;
    int fd;
} ws_async_resp_arg_t;

static void ws_async_send(void *arg)
{
    ws_async_resp_arg_t *resp_arg = (ws_async_resp_arg_t *)arg;
    if (!resp_arg) {
        return;
    }

    const char *msg = "{\"type\":\"async\",\"msg\":\"Async data from server\"}";
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len = strlen(msg),
    };

    esp_err_t ret = httpd_ws_send_frame_async(resp_arg->hd, resp_arg->fd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_send_frame_async failed: %d", ret);
    }

    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    ws_async_resp_arg_t *resp_arg = malloc(sizeof(ws_async_resp_arg_t));
    if (!resp_arg) {
        return ESP_ERR_NO_MEM;
    }

    resp_arg->hd = handle;
    resp_arg->fd = httpd_req_to_sockfd(req);

    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

static esp_err_t ws_send_text(httpd_req_t *req, const char *text)
{
    httpd_ws_frame_t out = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    return httpd_ws_send_frame(req, &out);
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    httpd_ws_frame_t ws_pkt = {0};
    cJSON *root = NULL;

    // Handshake call
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WS, "WebSocket handshake done");
        return ESP_OK;
    }

    // 1) First pass: query frame length/type
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_recv_frame(len) failed: %d", ret);
        return ret;
    }

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    if (ws_pkt.len > MAX_WS_PAYLOAD) {
        ESP_LOGE(TAG_WS, "WS payload too large: %u", (unsigned)ws_pkt.len);
        return ESP_ERR_INVALID_SIZE;
    }

    // 2) Allocate payload buffer (+1 so we can null-terminate text)
    ws_pkt.payload = (uint8_t *)malloc(ws_pkt.len + 1);
    if (!ws_pkt.payload) {
        return ESP_ERR_NO_MEM;
    }

    // 3) Second pass: actually receive frame data
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_recv_frame(payload) failed: %d", ret);
        free(ws_pkt.payload);
        return ret;
    }

    // Null-terminate for TEXT so strcmp/cJSON_Parse are safe
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ((char *)ws_pkt.payload)[ws_pkt.len] = '\0';
        ESP_LOGI(TAG_WS, "WS rx: type=%d len=%u payload='%s'",
                 (int)ws_pkt.type, (unsigned)ws_pkt.len, (char *)ws_pkt.payload);
    } else {
        ESP_LOGI(TAG_WS, "WS rx: type=%d len=%u (non-text)",
                 (int)ws_pkt.type, (unsigned)ws_pkt.len);
        // ignore non-text in this handler
        free(ws_pkt.payload);
        return ESP_OK;
    }

    // 4) Plain command example
    if (strcmp((char *)ws_pkt.payload, "Trigger async") == 0) {
        ret = trigger_async_send(req->handle, req);
        free(ws_pkt.payload);
        return ret;
    }

    // 5) JSON command example: { "type": "get-logs" }
    root = cJSON_Parse((char *)ws_pkt.payload);
    if (!root) {
        ESP_LOGE(TAG_WS, "Invalid JSON");
        ws_send_text(req, "{\"type\":\"error\",\"msg\":\"invalid json\"}");
        free(ws_pkt.payload);
        return ESP_OK;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "get-logs") == 0) {

        // Build custom response JSON
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "logs");

        cJSON *lines = cJSON_AddArrayToObject(resp, "lines");
        cJSON_AddItemToArray(lines, cJSON_CreateString("log line 1"));
        cJSON_AddItemToArray(lines, cJSON_CreateString("log line 2"));
        cJSON_AddItemToArray(lines, cJSON_CreateString("log line 3"));

        char *resp_str = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);

        if (!resp_str) {
            ws_send_text(req, "{\"type\":\"error\",\"msg\":\"oom\"}");
        } else {
            httpd_ws_frame_t out = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)resp_str,
                .len = strlen(resp_str),
            };

            ret = httpd_ws_send_frame(req, &out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG_WS, "httpd_ws_send_frame failed: %d", ret);
            }
            free(resp_str);
        }
    } else {
        ws_send_text(req, "{\"type\":\"error\",\"msg\":\"unknown or missing type\"}");
    }

    cJSON_Delete(root);
    free(ws_pkt.payload);
    return ESP_OK;
}

/**
 * ✅ This symbol must exist (non-static), because your http_server.c links against it.
 * Call this once after you start the httpd server.
 */
esp_err_t websocket_register_handlers(httpd_handle_t server)
{
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t ret = httpd_register_uri_handler(server, &ws);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "register ws handler failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG_WS, "ws handler registered at %s", ws.uri);
    return ESP_OK;
}
