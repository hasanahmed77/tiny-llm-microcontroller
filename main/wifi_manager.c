#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_mac.h"        // ADD THIS LINE - contains MACSTR and MAC2STR macros
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <string.h>


static const char *TAG = "WiFiManager";

#define AP_SSID "ESP32-Setup"
#define MAX_RETRY 3
#define NVS_NAMESPACE "wifi_config"
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

static httpd_handle_t server = NULL;
static bool connected = false;
static int retry_count = 0;

// HTML for configuration page
static const char config_html[] = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 WiFi Setup</title>"
"<style>body{font-family:Arial;margin:20px;background:#f0f0f0}"
".container{max-width:400px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;text-align:center}input,select,button{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}"
"button{background:#4CAF50;color:white;border:none;cursor:pointer}button:hover{background:#45a049}"
".network{padding:10px;margin:5px 0;background:#f9f9f9;border:1px solid #ddd;border-radius:4px;cursor:pointer}"
".network:hover{background:#e9e9e9}</style></head>"
"<body><div class='container'><h1>WiFi Setup</h1>"
"<div id='networks'><p>Scanning...</p></div>"
"<form id='wifiForm' onsubmit='submitForm(event)'>"
"<input type='text' id='ssid' name='ssid' placeholder='WiFi Name' required>"
"<input type='password' id='pass' name='pass' placeholder='Password (leave empty for open networks)'>"
"<button type='submit'>Connect</button></form>"
"<div id='status'></div></div>"
"<script>"
"function loadNetworks(){fetch('/scan').then(r=>r.json()).then(data=>{"
"let html='<h3>Available Networks:</h3>';data.forEach(n=>{"
"html+=`<div class='network' onclick='selectNetwork(\"${n.ssid}\",${n.auth})'>"
"${n.ssid} ${n.auth>0?'🔒':''}  (${n.rssi}dBm)</div>`});"
"document.getElementById('networks').innerHTML=html;}).catch(()=>{document.getElementById('networks').innerHTML='<p>Scan failed</p>'})}"
"function selectNetwork(ssid,auth){document.getElementById('ssid').value=ssid;"
"if(auth==0)document.getElementById('pass').value='';document.getElementById('pass').focus()}"
"function submitForm(e){e.preventDefault();let formData=new FormData(e.target);let data={};"
"formData.forEach((v,k)=>data[k]=v);document.getElementById('status').innerHTML='<p>Connecting...</p>';"
"fetch('/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})"
".then(r=>r.text()).then(msg=>{document.getElementById('status').innerHTML='<p style=\"color:green\">'+msg+'</p>';"
"setTimeout(()=>{window.location.reload()},3000)}).catch(()=>{document.getElementById('status').innerHTML='<p style=\"color:red\">Connection failed</p>'})}"
"loadNetworks();setInterval(loadNetworks,10000);</script></body></html>";

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station started, connecting...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            connected = false;
            if (retry_count < MAX_RETRY) {
                ESP_LOGI(TAG, "Retrying connection... (%d/%d)", retry_count + 1, MAX_RETRY);
                esp_wifi_connect();
                retry_count++;
            } else {
                ESP_LOGE(TAG, "Failed to connect after %d attempts", MAX_RETRY);
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "Client connected to AP, MAC: " MACSTR, MAC2STR(event->mac));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        connected = true;
        retry_count = 0;
    }
}

// HTTP handlers
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, config_html, strlen(config_html));
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {0};
    esp_wifi_scan_start(&scan_config, true);
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }
    
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    
    char *json = malloc(ap_count * 128 + 10);
    if (!json) {
        free(ap_list);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    strcpy(json, "[");
    for (int i = 0; i < ap_count; i++) {
        char entry[128];
        snprintf(entry, sizeof(entry), 
                "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                i > 0 ? "," : "",
                ap_list[i].ssid,
                ap_list[i].rssi,
                ap_list[i].authmode);
        strcat(json, entry);
    }
    strcat(json, "]");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    free(ap_list);
    return ESP_OK;
}

static esp_err_t connect_handler(httpd_req_t *req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parse JSON manually (simple parsing)
    char ssid[MAX_SSID_LEN] = {0};
    char password[MAX_PASS_LEN] = {0};
    
    char *ssid_start = strstr(content, "\"ssid\":\"");
    if (ssid_start) {
        ssid_start += 8;
        char *ssid_end = strchr(ssid_start, '"');
        if (ssid_end) {
            int len = ssid_end - ssid_start;
            if (len < MAX_SSID_LEN) {
                strncpy(ssid, ssid_start, len);
            }
        }
    }
    
    char *pass_start = strstr(content, "\"pass\":\"");
    if (pass_start) {
        pass_start += 8;
        char *pass_end = strchr(pass_start, '"');
        if (pass_end) {
            int len = pass_end - pass_start;
            if (len < MAX_PASS_LEN) {
                strncpy(password, pass_start, len);
            }
        }
    }
    
    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        nvs_set_str(nvs_handle, "password", password);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Credentials saved: %s", ssid);
    }
    
    httpd_resp_send(req, "Connecting to WiFi...", 21);
    
    // Stop AP and connect to WiFi (delayed to allow response to be sent)
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Reconfigure and connect
    wifi_config_t wifi_config = {0};
    strcpy((char*)wifi_config.sta.ssid, ssid);
    if (strlen(password) > 0) {
        strcpy((char*)wifi_config.sta.password, password);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Stop web server
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    
    // Switch to STA mode
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    retry_count = 0;
    esp_wifi_connect();
    
    return ESP_OK;
}

// Start web server
static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t scan_uri = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);
        
        httpd_uri_t connect_uri = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = connect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &connect_uri);
        
        ESP_LOGI(TAG, "Web server started");
    }
}

// DNS server for captive portal
static void dns_server_task(void *pvParameters) {
    struct sockaddr_in server_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);
    
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS server started");
    
    uint8_t buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (len > 0) {
            // Simple DNS response: redirect all to 192.168.4.1
            uint8_t response[512];
            memcpy(response, buffer, len > 12 ? 12 : len);
            response[2] = 0x81; response[3] = 0x80;  // Response flags
            response[6] = 0x00; response[7] = 0x01;  // 1 answer
            
            memcpy(response + len, buffer + 12, len - 12);
            int pos = len;
            
            response[pos++] = 0xC0; response[pos++] = 0x0C;  // Pointer to name
            response[pos++] = 0x00; response[pos++] = 0x01;  // Type A
            response[pos++] = 0x00; response[pos++] = 0x01;  // Class IN
            response[pos++] = 0x00; response[pos++] = 0x00;  // TTL
            response[pos++] = 0x00; response[pos++] = 0x3C;
            response[pos++] = 0x00; response[pos++] = 0x04;  // Data length
            response[pos++] = 192; response[pos++] = 168;    // 192.168.4.1
            response[pos++] = 4; response[pos++] = 1;
            
            sendto(sock, response, pos, 0, (struct sockaddr *)&client_addr, client_len);
        }
    }
}

// Start Access Point mode
static void start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting Access Point: %s", AP_SSID);
    
    // Set WiFi country code BEFORE setting mode (CRITICAL for visibility)
    wifi_country_t country = {
        .cc = "US",                    // Country code (use "US" for maximum compatibility)
        .schan = 1,                    // Start channel
        .nchan = 11,                   // Number of channels (1-11 for US)
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);

    esp_wifi_set_mode(WIFI_MODE_AP);
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .password = "",
            .ssid_len = strlen(AP_SSID),
            .channel = 6,              // Channel 6 - most compatible, less congested
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 0,          // MUST BE 0 - don't hide SSID
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    strcpy((char*)ap_config.ap.ssid, AP_SSID);
    
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    
    start_webserver();
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "AP started. Connect to '%s' and visit http://192.168.4.1", AP_SSID);
}

// Try to connect with stored credentials
static esp_err_t try_stored_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No stored credentials found");
        return ESP_ERR_NOT_FOUND;
    }
    
    char ssid[MAX_SSID_LEN] = {0};
    char password[MAX_PASS_LEN] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(password);
    
    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    nvs_get_str(nvs_handle, "password", password, &pass_len);  // Password is optional
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Found stored credentials for: %s", ssid);

        // Set WiFi country code BEFORE configuring
    wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 11,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);
    
    if (strlen(password) > 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    
    // Wait for connection (up to 10 seconds)
    for (int i = 0; i < 20; i++) {
        if (connected) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Connection failed - erase credentials
    ESP_LOGE(TAG, "Failed to connect with stored credentials, erasing...");
    nvs_handle_t nvs_write;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_write) == ESP_OK) {
        nvs_erase_all(nvs_write);
        nvs_commit(nvs_write);
        nvs_close(nvs_write);
    }
    
    return ESP_FAIL;
}

// Public functions
esp_err_t wifi_manager_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
    // Initialize network stack
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    // Register event handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    
    // Try stored credentials first
    ret = try_stored_credentials();
    
    if (ret != ESP_OK) {
        // No credentials or connection failed - start AP mode
        ESP_LOGI(TAG, "Starting configuration mode");
        start_ap_mode();
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi connected successfully");
    return ESP_OK;
}

bool wifi_manager_is_connected(void) {
    return connected;
}
