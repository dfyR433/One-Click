#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "soc/soc_caps.h"
#include "lwip/sockets.h"
#include "arpa/inet.h"
#include "config.h"
#if CONFIG_TINYUSB_ENABLED
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#endif

static const char *TAG = "One Click";

// ---------- SPIFFS ----------
static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_PARTITION,
        .max_files = SPIFFS_MAX_FILES,
        .format_if_mount_failed = SPIFFS_FORMAT_IF_FAILED
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(SPIFFS_PARTITION, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %s (total: %d, used: %d)", SPIFFS_BASE_PATH, (int)total, (int)used);
    return ESP_OK;
}

// ---------- Stream file.zip (attachment) ----------
static esp_err_t stream_file_zip(httpd_req_t *req)
{
    char path[128];
    snprintf(path, sizeof(path), SPIFFS_BASE_PATH "/file.zip");

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "file not found: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"file.zip\"");
    httpd_resp_set_hdr(req, "Content-Transfer-Encoding", "binary");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    // Using chunked transfer; do not set Content-Length when using httpd_resp_send_chunk

    char *buf = malloc(CHUNK);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }

    size_t r;
    while ((r = fread(buf, 1, CHUNK, f)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buf, r);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "chunk send failed: %s", esp_err_to_name(err));
            break;
        }
    }

    httpd_resp_send_chunk(req, NULL, 0); // finish
    free(buf);
    fclose(f);
    return ESP_OK;
}

// ---------- Redirect helper (no HTML) ----------
static esp_err_t send_redirect_to_download(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/download");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    // no body
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// probe endpoints: return 302 -> /download (no HTML)
static esp_err_t probe_redirect_handler(httpd_req_t *req)
{
    return send_redirect_to_download(req);
}

// root handler: redirect to /download (no HTML)
static esp_err_t root_get_handler(httpd_req_t *req)
{
    return send_redirect_to_download(req);
}

// download endpoint: stream the file
static esp_err_t download_get_handler(httpd_req_t *req)
{
    return stream_file_zip(req);
}

// HEAD on /download -> headers only with Content-Length
static esp_err_t download_head_handler(httpd_req_t *req)
{
    char path[128];
    snprintf(path, sizeof(path), SPIFFS_BASE_PATH "/file.zip");

    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }

    // Determine file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"file.zip\"");
    httpd_resp_set_hdr(req, "Content-Transfer-Encoding", "binary");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    if (file_size > 0) {
        char clen[32];
        snprintf(clen, sizeof(clen), "%ld", file_size);
        httpd_resp_set_hdr(req, "Content-Length", clen);
    }
    return httpd_resp_send(req, NULL, 0);
}

// 404 handler: redirect to /download (no HTML)
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return send_redirect_to_download(req);
}

// favicon handler: 204 No Content
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---------- Start webserver (tuned) ----------
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.stack_size = 16384;      // bigger stack for streaming & many handlers
    config.max_uri_handlers = 32;   // allow many probe handlers + others
    config.max_open_sockets = 6;    // <= allowed LWIP sockets in your build
#ifdef CONFIG_HTTPD_ENABLE_WILDCARD
    config.uri_match_fn = httpd_uri_match_wildcard;
#endif

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    // Captive portal probe endpoints -- return 302 redirect to /download
    const char *detectionPaths[] = {
        // Apple
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/success.html",
        "/hotspot-detect",
    
        // Android / ChromeOS
        "/generate_204",
        "/generate_200",
    
        // Windows
        "/ncsi.txt",
        "/connecttest.txt",
        "/redirect",
        "/fwlink",
    
        // Kindle / Amazon
        "/success.txt",
        "/wifistub.html",
    
        // Linux / NetworkManager
        "/nm-inet-test.txt",
        "/check_network_status.txt",
    
        // Generic
        "/index.html",
        "/kindle-wifi/wifistub.html",
        "/success"
    };
    for (size_t i = 0; i < sizeof(detectionPaths)/sizeof(detectionPaths[0]); ++i) {
        httpd_uri_t uri = {
            .uri = detectionPaths[i],
            .method = HTTP_GET,
            .handler = probe_redirect_handler,
            .user_ctx = NULL
        };
        if (httpd_register_uri_handler(server, &uri) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register probe URI: %s", detectionPaths[i]);
        }
    }

    // Root -> redirect
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &root) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register root handler");
    }

    // /download -> stream file.zip
    httpd_uri_t download = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &download) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register download handler");
    }

    // HEAD /download -> headers only
    httpd_uri_t download_head = {
        .uri = "/download",
        .method = HTTP_HEAD,
        .handler = download_head_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &download_head);

#ifdef CONFIG_HTTPD_ENABLE_WILDCARD
    // Catch-all GET -> serve download directly
    httpd_uri_t all_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = download_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &all_get);

    // Catch-all HEAD -> headers only
    httpd_uri_t all_head = {
        .uri = "/*",
        .method = HTTP_HEAD,
        .handler = download_head_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &all_head);
#endif

    // favicon
    httpd_uri_t fav = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &fav);

    // 404 -> redirect to /download
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    ESP_LOGI(TAG, "HTTP server started (max_uri=%d sockets=%d)", config.max_uri_handlers, config.max_open_sockets);
    return server;
}

// ---------- Minimal DNS responder (UDP port 53) ----------
static void dns_responder_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket create failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DNS_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed: %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS responder running on port %d", DNS_PORT);

    uint8_t req[512];
    uint8_t resp[600];

    while (1) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int len = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&client, &client_len);
        if (len <= 0) continue;

        if (len > (int)sizeof(resp)) len = sizeof(resp);
        memcpy(resp, req, len);

        // Set response flags (QR=1, RA=1)
        resp[2] = 0x81;
        resp[3] = 0x80;
        // ANCOUNT = 1
        resp[6] = 0x00;
        resp[7] = 0x01;

        // Find end of question
        int pos = 12;
        while (pos < len && resp[pos] != 0) pos += (resp[pos] + 1);
        pos += 1; // null
        pos += 4; // QTYPE + QCLASS

        int out_pos = pos;

        // Name pointer to offset 12 (0xC00C)
        resp[out_pos++] = 0xC0;
        resp[out_pos++] = 0x0C;
        // Type A
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x01;
        // Class IN
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x01;
        // TTL (60s)
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x3C;
        // RDLENGTH = 4
        resp[out_pos++] = 0x00;
        resp[out_pos++] = 0x04;

        // Fill answer with AP_IP_ADDR
        struct in_addr a;
        if (inet_pton(AF_INET, ip_str, &a) == 1) {
            memcpy(&resp[out_pos], &a.s_addr, 4);
        } else {
            uint32_t fallback = htonl((192 << 24) | (168 << 16) | (4 << 8) | 1);
            memcpy(&resp[out_pos], &fallback, 4);
        }
        out_pos += 4;

        sendto(sock, resp, out_pos, 0, (struct sockaddr *)&client, client_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

// ====== TinyUSB CDC-ACM ======
#if CONFIG_TINYUSB_ENABLED && SOC_USB_OTG_SUPPORTED
static void cdc_rx_cb(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[64];
    uint32_t n;
    // Read all available and echo back
    while ((n = tud_cdc_n_read(itf, buf, sizeof(buf))) > 0) {
        tud_cdc_n_write(itf, buf, n);
    }
    tud_cdc_n_write_flush(itf);
}

static void init_usb_cdc(void)
{
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    const tinyusb_config_cdcacm_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = cdc_rx_cb,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&cdc_cfg));
    ESP_LOGI(TAG, "TinyUSB CDC-ACM initialized");
}
#else
static inline void init_usb_cdc(void) {
#if !CONFIG_TINYUSB_ENABLED
    ESP_LOGI(TAG, "TinyUSB not enabled; skipping CDC init");
#else
    ESP_LOGI(TAG, "USB OTG device not supported on this target");
#endif
}
#endif

// ====== Wi-Fi Event Handler ======
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client connected: %02x:%02x:%02x:%02x:%02x:%02x AID=%d",
                 evt->mac[0], evt->mac[1], evt->mac[2],
                 evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client disconnected: %02x:%02x:%02x:%02x:%02x:%02x AID=%d",
                 evt->mac[0], evt->mac[1], evt->mac[2],
                 evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
    }
}

// ====== Helper: parse IP string into esp_ip4_addr_t ======
static bool parse_ip(const char *str, esp_ip4_addr_t *out_ip)
{
    int ip[4];
    if (sscanf(str, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
        return false;
    }
    IP4_ADDR(out_ip, ip[0], ip[1], ip[2], ip[3]);
    return true;
}

void startAP(void)
{
    esp_err_t ret;

    // ====== Init NVS, NETIF, Event loop ======
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default AP netif
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi AP netif");
        return;
    }

    // ====== Set static IP ======
    esp_netif_ip_info_t ipInfo;
    memset(&ipInfo, 0, sizeof(ipInfo));

    if (!parse_ip(ip_str, &ipInfo.ip) ||
        !parse_ip(gw_str, &ipInfo.gw) ||
        !parse_ip(netmask_str, &ipInfo.netmask)) {
        ESP_LOGE(TAG, "Invalid IP configuration");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ipInfo));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    // ====== Wi-Fi Init ======
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Set Wi-Fi mode and custom MAC
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, CUSTOM_MAC));

    // Configure AP
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, WIFI_SSID, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, WIFI_PASS, sizeof(wifi_config.ap.password) - 1);

    wifi_config.ap.ssid_len        = strlen(WIFI_SSID);
    wifi_config.ap.channel         = WIFI_CHANNEL;
    wifi_config.ap.max_connection  = MAX_CONN;
    wifi_config.ap.ssid_hidden     = WIFI_HIDDEN;
    wifi_config.ap.authmode        = (strlen(WIFI_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_MODE;
    wifi_config.ap.pmf_cfg.required = PMF;
    wifi_config.ap.pmf_cfg.capable  = PMF;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    esp_wifi_set_max_tx_power(MAX_TX_POWER);
    ESP_ERROR_CHECK(esp_wifi_start());

    // ====== Log Info ======
    ESP_LOGI(TAG, "Wi-Fi AP started");
}

// ---------- app_main ----------
void app_main(void)
{
    ESP_LOGI(TAG, "Starting firmware");

    // Initialize TinyUSB CDC-ACM (USB device CDC) early to avoid host enumeration failure under load
    init_usb_cdc();
#if CONFIG_TINYUSB_ENABLED && SOC_USB_OTG_SUPPORTED
    // Give the host some time to enumerate before starting Wi-Fi/HTTP
    for (int i = 0; i < 150; ++i) { // up to ~1500 ms
        if (tud_mounted()) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif

    // SPIFFS
    if (init_spiffs() != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS init failed (continuing)");
    } else {
        // log if missing file.zip
        char path[128];
        snprintf(path, sizeof(path), SPIFFS_BASE_PATH "/file.zip");
        FILE *f = fopen(path, "rb");
        if (!f) ESP_LOGW(TAG, "%s not found in SPIFFS. Upload file.zip to SPIFFS partition.", path);
        else fclose(f);
    }

    // Start AP
    startAP();

    // Start HTTP server
    if (start_webserver() == NULL) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    // Start DNS responder
    xTaskCreate(dns_responder_task, "dns_responder", 4096, NULL, tskIDLE_PRIORITY + 5, NULL);

}
