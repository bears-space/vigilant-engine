#include <string.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "websocket.h"
#include "cJSON.h"

static const char *TAG_WS = "websocket";

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

static void ws_async_send(void *arg)
{
    static const char *data = "Async data";
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }

    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);

    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

static esp_err_t ws_echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WS, "WebSocket handshake complete");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    ESP_LOGI(TAG_WS, "WS frame len: %d", ws_pkt.len);
    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG_WS, "Failed to calloc memory for WS payload");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WS, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG_WS, "WS message: %s", ws_pkt.payload);
    }

    ESP_LOGI(TAG_WS, "WS packet type: %d", ws_pkt.type);
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        ws_pkt.payload != NULL &&
        strcmp((char *)ws_pkt.payload, "Trigger async") == 0) {
        free(buf);
        return trigger_async_send(req->handle, req);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

static esp_err_t ws_logs_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WS, "/ws-logs WebSocket handshake complete");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *payload = NULL;
    char *json_buf = NULL;
    cJSON *root = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        goto cleanup;
    }

    ESP_LOGI(TAG_WS, "WS frame len: %d", ws_pkt.len);
    if (ws_pkt.len) {
        payload = calloc(1, ws_pkt.len + 1);
        if (payload == NULL) {
            ESP_LOGE(TAG_WS, "Failed to calloc memory for WS payload");
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }

        ws_pkt.payload = payload;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WS, "httpd_ws_recv_frame failed with %d", ret);
            goto cleanup;
        }
        ESP_LOGI(TAG_WS, "WS message: %s", ws_pkt.payload);
        if (ws_pkt.payload && ws_pkt.len > 0) {
            // ws_pkt.payload might not be null-terminated -> copy into a safe buffer
            json_buf = malloc(ws_pkt.len + 1);
            if (!json_buf) {
                ret = ESP_ERR_NO_MEM;
                goto cleanup;
            }
            memcpy(json_buf, ws_pkt.payload, ws_pkt.len);
            json_buf[ws_pkt.len] = '\0';

            root = cJSON_Parse(json_buf);
            if (!root) {
                ESP_LOGI(TAG_WS, "INVALID JSON message: %s", ws_pkt.payload);
                ret = ESP_FAIL;
                goto cleanup;
            }

            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            if (cJSON_IsString(type) && type->valuestring) {
                if (strcmp(type->valuestring, "get-logs") == 0) {
                    // type matches -> do your "get logs" handling here
                    ESP_LOGI(TAG_WS, "WS packet type: %d", ws_pkt.type);
                    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
                        ws_pkt.payload != NULL &&
                        strcmp((char *)ws_pkt.payload, "Trigger async") == 0) {
                        ret = trigger_async_send(req->handle, req);
                        goto cleanup;
                    }

                    ret = httpd_ws_send_frame(req, &ws_pkt);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG_WS, "httpd_ws_send_frame failed with %d", ret);
                    }
                }
            }
        }
        
    }

cleanup:
    if (root) {
        cJSON_Delete(root);
    }
    free(json_buf);
    free(payload);
    return ret;
}


static const httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_echo_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static const httpd_uri_t ws_logs_uri = {
    .uri        = "/ws-logs",
    .method     = HTTP_GET,
    .handler    = ws_logs_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static const httpd_uri_t ws_auth_uri = {
    .uri        = "/auth",
    .method     = HTTP_GET,
    .handler    = ws_echo_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

esp_err_t websocket_register_handlers(httpd_handle_t server)
{
    esp_err_t err;

    err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to register /ws WebSocket handler (%s)", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &ws_auth_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to register /auth WebSocket handler (%s)", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &ws_logs_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to register /ws-logs WebSocket handler (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_WS, "Registered WebSocket handlers at /ws and /auth");
    return ESP_OK;
}
