/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "config_http_server.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "basic_demo_settings.h"
#include "basic_demo_wifi.h"
#include "cap_im_wechat.h"
#include "claw_core.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "config_http";

#define CONFIG_HTTP_CTRL_PORT         32769
#define CONFIG_HTTP_SCRATCH_SIZE      4096
#define CONFIG_HTTP_PATH_MAX          256
#define CONFIG_HTTP_UPLOAD_MAX_SIZE   (512 * 1024)

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[] asm("_binary_styles_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t lean_qr_min_mjs_start[] asm("_binary_lean_qr_min_mjs_start");
extern const uint8_t lean_qr_min_mjs_end[] asm("_binary_lean_qr_min_mjs_end");

typedef struct {
    httpd_handle_t server;
    char storage_base_path[CONFIG_HTTP_PATH_MAX];
} config_http_server_ctx_t;

static config_http_server_ctx_t s_ctx = {0};

static char *alloc_scratch_buffer(void)
{
    return heap_caps_malloc_prefer(CONFIG_HTTP_SCRATCH_SIZE,
                                   2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static bool path_is_safe(const char *path)
{
    return path && path[0] == '/' && strstr(path, "..") == NULL;
}

static void url_decode_inplace(char *value)
{
    if (!value) {
        return;
    }

    char *src = value;
    char *dst = value;
    while (*src) {
        if (src[0] == '%' && src[1] && src[2]) {
            char hi = src[1];
            char lo = src[2];
            uint8_t decoded = 0;

            if (hi >= '0' && hi <= '9') {
                decoded = (uint8_t)(hi - '0') << 4;
            } else if (hi >= 'A' && hi <= 'F') {
                decoded = (uint8_t)(hi - 'A' + 10) << 4;
            } else if (hi >= 'a' && hi <= 'f') {
                decoded = (uint8_t)(hi - 'a' + 10) << 4;
            } else {
                *dst++ = *src++;
                continue;
            }

            if (lo >= '0' && lo <= '9') {
                decoded |= (uint8_t)(lo - '0');
            } else if (lo >= 'A' && lo <= 'F') {
                decoded |= (uint8_t)(lo - 'A' + 10);
            } else if (lo >= 'a' && lo <= 'f') {
                decoded |= (uint8_t)(lo - 'a' + 10);
            } else {
                *dst++ = *src++;
                continue;
            }

            *dst++ = (char)decoded;
            src += 3;
            continue;
        }

        if (*src == '+') {
            *dst++ = ' ';
            src++;
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static esp_err_t query_get(httpd_req_t *req, const char *key, char *value, size_t value_size)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    char *query = calloc(1, query_len + 1);
    if (!query) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = httpd_req_get_url_query_str(req, query, query_len + 1);
    if (err == ESP_OK) {
        err = httpd_query_key_value(query, key, value, value_size);
        if (err == ESP_OK) {
            url_decode_inplace(value);
        }
    }

    free(query);
    return err;
}

static esp_err_t send_embedded_file(httpd_req_t *req,
                                    const uint8_t *start,
                                    const uint8_t *end,
                                    const char *content_type)
{
    size_t content_len = (size_t)(end - start);
    if (content_len > 0 && start[content_len - 1] == '\0') {
        content_len--;
    }
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, (const char *)start, content_len);
}

static void json_add_string(cJSON *root, const char *key, const char *value)
{
    cJSON_AddStringToObject(root, key, value ? value : "");
}

static esp_err_t send_json_response(httpd_req_t *req, cJSON *root)
{
    char *payload = NULL;
    esp_err_t err;

    if (!req || !root) {
        return ESP_ERR_INVALID_ARG;
    }

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    err = httpd_resp_sendstr(req, payload);
    free(payload);
    return err;
}

static esp_err_t settings_to_json(httpd_req_t *req)
{
    basic_demo_settings_t settings;
    ESP_RETURN_ON_ERROR(basic_demo_settings_load(&settings), TAG, "Failed to load settings");

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    json_add_string(root, "wifi_ssid", settings.wifi_ssid);
    json_add_string(root, "wifi_password", settings.wifi_password);
    json_add_string(root, "llm_api_key", settings.llm_api_key);
    json_add_string(root, "llm_backend_type", settings.llm_backend_type);
    json_add_string(root, "llm_profile", settings.llm_profile);
    json_add_string(root, "llm_model", settings.llm_model);
    json_add_string(root, "llm_base_url", settings.llm_base_url);
    json_add_string(root, "llm_auth_type", settings.llm_auth_type);
    json_add_string(root, "llm_timeout_ms", settings.llm_timeout_ms);
    json_add_string(root, "qq_app_id", settings.qq_app_id);
    json_add_string(root, "qq_app_secret", settings.qq_app_secret);
    json_add_string(root, "feishu_app_id", settings.feishu_app_id);
    json_add_string(root, "feishu_app_secret", settings.feishu_app_secret);
    json_add_string(root, "tg_bot_token", settings.tg_bot_token);
    json_add_string(root, "wechat_token", settings.wechat_token);
    json_add_string(root, "wechat_base_url", settings.wechat_base_url);
    json_add_string(root, "wechat_cdn_base_url", settings.wechat_cdn_base_url);
    json_add_string(root, "wechat_account_id", settings.wechat_account_id);
    json_add_string(root, "search_brave_key", settings.search_brave_key);
    json_add_string(root, "search_tavily_key", settings.search_tavily_key);
    json_add_string(root, "time_timezone", settings.time_timezone);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    esp_err_t err = httpd_resp_sendstr(req, payload);
    free(payload);
    return err;
}

static esp_err_t parse_json_body(httpd_req_t *req, cJSON **out_root)
{
    if (!out_root || req->content_len <= 0 || req->content_len > 8192) {
        return ESP_ERR_INVALID_ARG;
    }

    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            return (ret == HTTPD_SOCK_ERR_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_root = root;
    return ESP_OK;
}

static void json_read_string(cJSON *root, const char *key, char *buffer, size_t buffer_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item)) {
        strlcpy(buffer, item->valuestring, buffer_size);
    }
}

static esp_err_t index_handler(httpd_req_t *req)
{
    return send_embedded_file(req, index_html_start, index_html_end, "text/html; charset=utf-8");
}

static esp_err_t styles_handler(httpd_req_t *req)
{
    return send_embedded_file(req, styles_css_start, styles_css_end, "text/css; charset=utf-8");
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    return send_embedded_file(req, app_js_start, app_js_end, "application/javascript; charset=utf-8");
}

static esp_err_t lean_qr_handler(httpd_req_t *req)
{
    return send_embedded_file(req,
                              lean_qr_min_mjs_start,
                              lean_qr_min_mjs_end,
                              "application/javascript; charset=utf-8");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(root, "wifi_connected", basic_demo_wifi_is_connected());
    json_add_string(root, "ip", basic_demo_wifi_get_ip());
    json_add_string(root, "storage_base_path", s_ctx.storage_base_path);
    cJSON_AddBoolToObject(root, "ap_active", basic_demo_wifi_is_ap_active());
    json_add_string(root, "ap_ssid", basic_demo_wifi_get_ap_ssid());
    json_add_string(root, "ap_ip", basic_demo_wifi_get_ap_ip());
    json_add_string(root, "wifi_mode", basic_demo_wifi_get_mode_string());

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    esp_err_t err = httpd_resp_sendstr(req, payload);
    free(payload);
    return err;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    return settings_to_json(req);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    basic_demo_settings_t settings;
    ESP_RETURN_ON_ERROR(basic_demo_settings_load(&settings), TAG, "Failed to load settings");

    cJSON *root = NULL;
    esp_err_t err = parse_json_body(req, &root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
        return err;
    }

    json_read_string(root, "wifi_ssid", settings.wifi_ssid, sizeof(settings.wifi_ssid));
    json_read_string(root, "wifi_password", settings.wifi_password, sizeof(settings.wifi_password));
    json_read_string(root, "llm_api_key", settings.llm_api_key, sizeof(settings.llm_api_key));
    json_read_string(root, "llm_backend_type", settings.llm_backend_type, sizeof(settings.llm_backend_type));
    json_read_string(root, "llm_profile", settings.llm_profile, sizeof(settings.llm_profile));
    json_read_string(root, "llm_model", settings.llm_model, sizeof(settings.llm_model));
    json_read_string(root, "llm_base_url", settings.llm_base_url, sizeof(settings.llm_base_url));
    json_read_string(root, "llm_auth_type", settings.llm_auth_type, sizeof(settings.llm_auth_type));
    json_read_string(root, "llm_timeout_ms", settings.llm_timeout_ms, sizeof(settings.llm_timeout_ms));
    json_read_string(root, "qq_app_id", settings.qq_app_id, sizeof(settings.qq_app_id));
    json_read_string(root, "qq_app_secret", settings.qq_app_secret, sizeof(settings.qq_app_secret));
    json_read_string(root, "feishu_app_id", settings.feishu_app_id, sizeof(settings.feishu_app_id));
    json_read_string(root, "feishu_app_secret", settings.feishu_app_secret, sizeof(settings.feishu_app_secret));
    json_read_string(root, "tg_bot_token", settings.tg_bot_token, sizeof(settings.tg_bot_token));
    json_read_string(root, "wechat_token", settings.wechat_token, sizeof(settings.wechat_token));
    json_read_string(root, "wechat_base_url", settings.wechat_base_url, sizeof(settings.wechat_base_url));
    json_read_string(root, "wechat_cdn_base_url", settings.wechat_cdn_base_url, sizeof(settings.wechat_cdn_base_url));
    json_read_string(root, "wechat_account_id", settings.wechat_account_id, sizeof(settings.wechat_account_id));
    json_read_string(root, "search_brave_key", settings.search_brave_key, sizeof(settings.search_brave_key));
    json_read_string(root, "search_tavily_key", settings.search_tavily_key, sizeof(settings.search_tavily_key));
    json_read_string(root, "time_timezone", settings.time_timezone, sizeof(settings.time_timezone));

    cJSON_Delete(root);

    err = basic_demo_settings_save(&settings);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Saved. Restart the device to apply Wi-Fi and core LLM changes.\"}");
}

/* ═══════════════════════════════════════════════════
   Chat page + API
   ═══════════════════════════════════════════════════ */

static const char chat_html[] =
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP-Claw Chat</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:#1a1a2e;color:#e0e0e0;height:100vh;display:flex;flex-direction:column}"
"#header{background:#16213e;padding:12px 16px;font-size:18px;font-weight:600;"
"border-bottom:1px solid #0f3460;display:flex;align-items:center;gap:8px}"
"#header span{color:#e94560}"
"#messages{flex:1;overflow-y:auto;padding:16px;display:flex;flex-direction:column;gap:12px}"
".msg{max-width:80%;padding:10px 14px;border-radius:12px;line-height:1.5;"
"word-break:break-word;white-space:pre-wrap;font-size:15px}"
".msg.user{background:#0f3460;align-self:flex-end;color:#fff}"
".msg.bot{background:#222244;align-self:flex-start;color:#e0e0e0}"
".msg.error{background:#3a1010;color:#ff6b6b}"
".msg.system{background:transparent;color:#888;align-self:center;font-size:13px;text-align:center}"
"#input-area{padding:12px 16px;background:#16213e;border-top:1px solid #0f3460;"
"display:flex;gap:8px}"
"#msg-input{flex:1;padding:10px 14px;border:1px solid #0f3460;border-radius:8px;"
"background:#1a1a2e;color:#e0e0e0;font-size:15px;resize:none;outline:none}"
"#msg-input:focus{border-color:#e94560}"
"#send-btn{padding:10px 20px;background:#e94560;color:#fff;border:none;"
"border-radius:8px;font-size:15px;cursor:pointer;font-weight:600}"
"#send-btn:disabled{opacity:0.5;cursor:not-allowed}"
"#send-btn:hover:not(:disabled){background:#c73e54}"
"</style></head><body>"
"<div id='header'>ESP-Claw <span>Chat</span></div>"
"<div id='messages'></div>"
"<div id='input-area'>"
"<textarea id='msg-input' rows='1' placeholder='Type a message...'></textarea>"
"<button id='send-btn'>Send</button>"
"</div>"
"<script>"
"const msgBox=document.getElementById('messages');"
"const input=document.getElementById('msg-input');"
"const btn=document.getElementById('send-btn');"
"let busy=false;"
"function addMsg(cls,text){"
"const d=document.createElement('div');d.className='msg '+cls;"
"d.textContent=text;msgBox.appendChild(d);msgBox.scrollTop=msgBox.scrollHeight;"
"}"
"async function send(){"
"const text=input.value.trim();if(!text||busy)return;"
"busy=true;btn.disabled=true;input.value='';addMsg('user',text);"
"try{"
"const r=await fetch('/api/ask',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({message:text})});"
"const data=await r.json();"
"if(data.ok)addMsg('bot',data.response);else addMsg('error',data.error||'Error');"
"}catch(e){addMsg('error','Network error: '+e.message)}"
"busy=false;btn.disabled=false;input.focus();"
"}"
"btn.onclick=send;"
"input.onkeydown=function(e){if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();send()}};"
"addMsg('system','Ready. Type a message to start chatting.');"
"input.focus();"
"</script></body></html>";

static esp_err_t chat_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, chat_html, sizeof(chat_html) - 1);
}

static uint32_t s_chat_request_id = 0;

static esp_err_t chat_ask_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_ERR_INVALID_ARG;
    }

    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < (int)req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            return (ret == HTTPD_SOCK_ERR_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *msg_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsString(msg_item) || !msg_item->valuestring[0]) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'message' field");
        return ESP_ERR_INVALID_ARG;
    }

    char *message = strdup(msg_item->valuestring);
    cJSON_Delete(root);
    if (!message) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    claw_core_request_t request = {
        .user_text = message,
        .session_id = "web_chat",
        .request_id = s_chat_request_id++,
    };

    esp_err_t err = claw_core_submit(&request, pdMS_TO_TICKS(5000));
    free(message);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Submit failed\"}");
        return err;
    }

    claw_core_response_t response = {0};
    err = claw_core_receive_for(request.request_id, &response, pdMS_TO_TICKS(120000));
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Timeout\"}");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");

    if (response.status == CLAW_CORE_RESPONSE_STATUS_OK && response.text) {
        cJSON *escaped = cJSON_CreateString(response.text);
        char *json = cJSON_PrintUnformatted(escaped);
        cJSON_Delete(escaped);
        char *resp = NULL;
        asprintf(&resp, "{\"ok\":true,\"response\":%s}", json);
        free(json);
        if (resp) {
            httpd_resp_sendstr(req, resp);
            free(resp);
        } else {
            httpd_resp_sendstr(req, "{\"ok\":true,\"response\":\"\"}");
        }
    } else {
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"LLM error\"}");
    }

    claw_core_response_free(&response);
    return ESP_OK;
}

static esp_err_t wechat_login_persist_if_needed(cap_im_wechat_qr_login_status_t *status)
{
    basic_demo_settings_t settings;
    esp_err_t err;

    if (!status || !status->completed || status->persisted || !status->token[0]) {
        return ESP_OK;
    }

    err = basic_demo_settings_load(&settings);
    if (err != ESP_OK) {
        return err;
    }

    strlcpy(settings.wechat_token, status->token, sizeof(settings.wechat_token));
    strlcpy(settings.wechat_base_url,
            status->base_url[0] ? status->base_url : settings.wechat_base_url,
            sizeof(settings.wechat_base_url));
    strlcpy(settings.wechat_account_id,
            status->account_id[0] ? status->account_id : settings.wechat_account_id,
            sizeof(settings.wechat_account_id));

    err = basic_demo_settings_save(&settings);
    if (err != ESP_OK) {
        return err;
    }

    return cap_im_wechat_qr_login_mark_persisted();
}

static esp_err_t wechat_login_start_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *resp = NULL;
    const char *account_id = NULL;
    bool force = false;
    cap_im_wechat_qr_login_status_t status = {0};
    esp_err_t err;

    if (req->content_len > 0) {
        err = parse_json_body(req, &root);
        if (err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
            return err;
        }
        account_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "account_id"));
        force = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "force"));
    }

    err = cap_im_wechat_qr_login_start(account_id, force);
    if (root) {
        cJSON_Delete(root);
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start WeChat login");
        return err;
    }

    ESP_RETURN_ON_ERROR(cap_im_wechat_qr_login_get_status(&status),
                        TAG,
                        "Failed to fetch WeChat login status");
    resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(resp, "ok", true);
    json_add_string(resp, "session_key", status.session_key);
    json_add_string(resp, "status", status.status);
    json_add_string(resp, "message", status.message);
    json_add_string(resp, "qr_data_url", status.qr_data_url);
    return send_json_response(req, resp);
}

static esp_err_t wechat_login_status_handler(httpd_req_t *req)
{
    cap_im_wechat_qr_login_status_t status = {0};
    cJSON *resp = NULL;
    esp_err_t err;

    err = cap_im_wechat_qr_login_get_status(&status);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read WeChat login status");
        return err;
    }

    err = wechat_login_persist_if_needed(&status);
    if (err == ESP_OK && status.completed && !status.persisted) {
        status.persisted = true;
        strlcpy(status.message,
                "微信登录成功，凭据已保存。重启设备后生效。",
                sizeof(status.message));
    }

    resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "active", status.active);
    cJSON_AddBoolToObject(resp, "completed", status.completed);
    cJSON_AddBoolToObject(resp, "persisted", status.persisted);
    cJSON_AddBoolToObject(resp, "configured", status.configured);
    json_add_string(resp, "session_key", status.session_key);
    json_add_string(resp, "status", status.status);
    json_add_string(resp, "message", status.message);
    json_add_string(resp, "qr_data_url", status.qr_data_url);
    json_add_string(resp, "account_id", status.account_id);
    json_add_string(resp, "user_id", status.user_id);
    json_add_string(resp, "base_url", status.base_url);
    cJSON_AddBoolToObject(resp, "restart_required", status.persisted);
    return send_json_response(req, resp);
}

static esp_err_t wechat_login_cancel_handler(httpd_req_t *req)
{
    cJSON *resp = NULL;
    esp_err_t err = cap_im_wechat_qr_login_cancel();

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to cancel WeChat login");
        return err;
    }

    resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    json_add_string(resp, "message", "已取消微信登录。");
    return send_json_response(req, resp);
}

static esp_err_t resolve_storage_path(const char *relative_path, char *full_path, size_t full_path_size)
{
    if (!path_is_safe(relative_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(full_path, full_path_size, "%s%s", s_ctx.storage_base_path, relative_path);
    if (written <= 0 || (size_t)written >= full_path_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static bool build_child_relative_path(const char *base_path,
                                      const char *entry_name,
                                      char *out_path,
                                      size_t out_path_size)
{
    if (!base_path || !entry_name || !out_path || out_path_size == 0) {
        return false;
    }

    if (strcmp(base_path, "/") == 0) {
        if (strlcpy(out_path, "/", out_path_size) >= out_path_size) {
            return false;
        }
    } else if (strlcpy(out_path, base_path, out_path_size) >= out_path_size) {
        return false;
    }

    if (strcmp(base_path, "/") != 0 && strlcat(out_path, "/", out_path_size) >= out_path_size) {
        return false;
    }

    return strlcat(out_path, entry_name, out_path_size) < out_path_size;
}

static esp_err_t files_list_handler(httpd_req_t *req)
{
    char relative_path[CONFIG_HTTP_PATH_MAX] = "/";
    if (query_get(req, "path", relative_path, sizeof(relative_path)) != ESP_OK) {
        strlcpy(relative_path, "/", sizeof(relative_path));
    }

    char full_path[CONFIG_HTTP_PATH_MAX];
    if (resolve_storage_path(relative_path, full_path, sizeof(full_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(full_path);
    if (!dir) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory not found");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    if (!root || !entries) {
        closedir(dir);
        cJSON_Delete(root);
        cJSON_Delete(entries);
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    json_add_string(root, "path", relative_path);
    cJSON_AddItemToObject(root, "entries", entries);

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_relative[CONFIG_HTTP_PATH_MAX];
        if (!build_child_relative_path(relative_path, entry->d_name, child_relative, sizeof(child_relative))) {
            continue;
        }

        char child_full[CONFIG_HTTP_PATH_MAX];
        if (resolve_storage_path(child_relative, child_full, sizeof(child_full)) != ESP_OK) {
            continue;
        }

        struct stat st = {0};
        if (stat(child_full, &st) != 0) {
            continue;
        }

        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }

        json_add_string(item, "name", entry->d_name);
        json_add_string(item, "path", child_relative);
        cJSON_AddBoolToObject(item, "is_dir", S_ISDIR(st.st_mode));
        cJSON_AddNumberToObject(item, "size", (double)st.st_size);
        cJSON_AddItemToArray(entries, item);
    }

    closedir(dir);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    esp_err_t err = httpd_resp_sendstr(req, payload);
    free(payload);
    return err;
}

static esp_err_t file_download_handler(httpd_req_t *req)
{
    const char *relative_path = req->uri + strlen("/files");
    if (!path_is_safe(relative_path)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[CONFIG_HTTP_PATH_MAX];
    if (resolve_storage_path(relative_path, full_path, sizeof(full_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st = {0};
    if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    char *scratch = alloc_scratch_buffer();
    if (!scratch) {
        fclose(file);
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    while (!feof(file)) {
        size_t read_bytes = fread(scratch, 1, CONFIG_HTTP_SCRATCH_SIZE, file);
        if (read_bytes > 0 && httpd_resp_send_chunk(req, scratch, read_bytes) != ESP_OK) {
            free(scratch);
            fclose(file);
            return ESP_FAIL;
        }
    }

    free(scratch);
    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t files_upload_handler(httpd_req_t *req)
{
    char relative_path[CONFIG_HTTP_PATH_MAX] = {0};
    if (query_get(req, "path", relative_path, sizeof(relative_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
        return ESP_ERR_INVALID_ARG;
    }

    if (req->content_len <= 0 || req->content_len > CONFIG_HTTP_UPLOAD_MAX_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid upload size");
        return ESP_ERR_INVALID_SIZE;
    }

    char full_path[CONFIG_HTTP_PATH_MAX];
    if (resolve_storage_path(relative_path, full_path, sizeof(full_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    char parent_path[CONFIG_HTTP_PATH_MAX];
    strlcpy(parent_path, full_path, sizeof(parent_path));
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }
    *slash = '\0';

    struct stat st = {0};
    if (stat(parent_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parent directory not found");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(full_path, "wb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    char *scratch = alloc_scratch_buffer();
    if (!scratch) {
        fclose(file);
        unlink(full_path);
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int chunk = remaining > CONFIG_HTTP_SCRATCH_SIZE ? CONFIG_HTTP_SCRATCH_SIZE : remaining;
        int received = httpd_req_recv(req, scratch, chunk);
        if (received <= 0) {
            free(scratch);
            fclose(file);
            unlink(full_path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }

        if (fwrite(scratch, 1, received, file) != (size_t)received) {
            free(scratch);
            fclose(file);
            unlink(full_path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    free(scratch);
    fclose(file);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t files_delete_handler(httpd_req_t *req)
{
    char relative_path[CONFIG_HTTP_PATH_MAX] = {0};
    if (query_get(req, "path", relative_path, sizeof(relative_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[CONFIG_HTTP_PATH_MAX];
    if (resolve_storage_path(relative_path, full_path, sizeof(full_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st = {0};
    if (stat(full_path, &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Path not found");
        return ESP_ERR_NOT_FOUND;
    }

    int rc = S_ISDIR(st.st_mode) ? rmdir(full_path) : unlink(full_path);
    if (rc != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t files_mkdir_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    if (parse_json_body(req, &root) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *path_item = cJSON_GetObjectItemCaseSensitive(root, "path");
    if (!cJSON_IsString(path_item) || !path_is_safe(path_item->valuestring)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[CONFIG_HTTP_PATH_MAX];
    esp_err_t err = resolve_storage_path(path_item->valuestring, full_path, sizeof(full_path));
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return err;
    }

    if (mkdir(full_path, 0775) != 0 && errno != EEXIST) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t captive_404_handler(httpd_req_t *req, httpd_err_code_t error)
{
    if (!basic_demo_wifi_is_ap_active()) {
        return httpd_resp_send_err(req, error, NULL);
    }

    const char *ap_ip = basic_demo_wifi_get_ap_ip();
    if (!ap_ip || !ap_ip[0]) {
        ap_ip = "192.168.4.1";
    }

    char location[40];
    snprintf(location, sizeof(location), "http://%s/", ap_ip);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t config_http_server_init(const char *storage_base_path)
{
    if (!storage_base_path || storage_base_path[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(s_ctx.storage_base_path, storage_base_path, sizeof(s_ctx.storage_base_path));
    return ESP_OK;
}

esp_err_t config_http_server_start(void)
{
    if (s_ctx.server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = BASIC_DEMO_HTTP_SERVER_PORT;
    config.ctrl_port = CONFIG_HTTP_CTRL_PORT;
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_ctx.server, &config), TAG, "Failed to start HTTP server");

    httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/index.html", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/styles.css", .method = HTTP_GET, .handler = styles_handler },
        { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_handler },
        { .uri = "/lean-qr.min.mjs", .method = HTTP_GET, .handler = lean_qr_handler },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler },
        { .uri = "/api/config", .method = HTTP_GET, .handler = config_get_handler },
        { .uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler },
        { .uri = "/api/wechat/login/start", .method = HTTP_POST, .handler = wechat_login_start_handler },
        { .uri = "/api/wechat/login/status", .method = HTTP_GET, .handler = wechat_login_status_handler },
        { .uri = "/api/wechat/login/cancel", .method = HTTP_POST, .handler = wechat_login_cancel_handler },
        { .uri = "/api/files", .method = HTTP_GET, .handler = files_list_handler },
        { .uri = "/api/files", .method = HTTP_DELETE, .handler = files_delete_handler },
        { .uri = "/api/files/upload", .method = HTTP_POST, .handler = files_upload_handler },
        { .uri = "/api/files/mkdir", .method = HTTP_POST, .handler = files_mkdir_handler },
        { .uri = "/files/*", .method = HTTP_GET, .handler = file_download_handler },
        { .uri = "/chat", .method = HTTP_GET, .handler = chat_page_handler },
        { .uri = "/api/ask", .method = HTTP_POST, .handler = chat_ask_handler },
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_ctx.server, &handlers[i]),
                            TAG,
                            "Failed to register URI handler");
    }

    ESP_RETURN_ON_ERROR(httpd_register_err_handler(s_ctx.server,
                                                   HTTPD_404_NOT_FOUND,
                                                   captive_404_handler),
                        TAG, "Failed to register captive 404 handler");

    ESP_LOGI(TAG, "HTTP server started on port %d", BASIC_DEMO_HTTP_SERVER_PORT);
    return ESP_OK;
}

esp_err_t config_http_server_stop(void)
{
    if (!s_ctx.server) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(httpd_stop(s_ctx.server), TAG, "Failed to stop HTTP server");
    s_ctx.server = NULL;
    return ESP_OK;
}
