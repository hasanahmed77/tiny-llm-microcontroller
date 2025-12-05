#include <stdio.h>
#include <inttypes.h>
#include "esp_spiffs.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include <time.h>
#include <string>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <cctype>

extern "C"
{
#include "llama.h"
#include "llm.h"
#include "wifi_manager.h" // Add this
}

static const char *TAG = "MAIN";

// I2C OLED
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define OLED_I2C_ADDRESS 0x3C

// I2S Microphone
#define I2S_SCK 47
#define I2S_WS 38
#define I2S_SD 12
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000

// Audio buffer
#define BUFFER_SIZE (SAMPLE_RATE * 5)

// Button
#define BUTTON_PIN GPIO_NUM_21

#define ASSEMBLYAI_API_KEY "6346c441279643418f2c9284f0608a59"

// Output buffer
#define OUTPUT_BUFFER_SIZE 1024
static char output_buffer[OUTPUT_BUFFER_SIZE];
static int output_pos = 0;

// STT state
static char transcribed_text[512];
static char upload_url[512];

// Scroll state
static int scroll_position = 0;
static bool generation_complete = false;

// Font 5x7
const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x56, 0x20, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

// === OLED FUNCTIONS WITH BREATHING ROOM ===

void oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x00, true);
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(handle);
    vTaskDelay(pdMS_TO_TICKS(5)); // Breathing room
}

void oled_init()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = I2C_MASTER_FREQ_HZ},
        .clk_flags = 0};

    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    oled_write_cmd(0xAE);
    oled_write_cmd(0xD5);
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8);
    oled_write_cmd(0x3F);
    oled_write_cmd(0xD3);
    oled_write_cmd(0x00);
    oled_write_cmd(0x40);
    oled_write_cmd(0x8D);
    oled_write_cmd(0x14);
    oled_write_cmd(0x20);
    oled_write_cmd(0x00);
    oled_write_cmd(0xA1);
    oled_write_cmd(0xC8);
    oled_write_cmd(0xDA);
    oled_write_cmd(0x12);
    oled_write_cmd(0x81);
    oled_write_cmd(0xCF);
    oled_write_cmd(0xD9);
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDB);
    oled_write_cmd(0x40);
    oled_write_cmd(0xA4);
    oled_write_cmd(0xA6);
    oled_write_cmd(0xAF);

    ESP_LOGI(TAG, "OLED initialized");
}

void oled_clear()
{
    for (int page = 0; page < 8; page++)
    {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);

        i2c_cmd_handle_t handle = i2c_cmd_link_create();
        i2c_master_start(handle);
        i2c_master_write_byte(handle, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(handle, 0x40, true);

        for (int col = 0; col < 128; col++)
        {
            i2c_master_write_byte(handle, 0x00, true);
        }

        i2c_master_stop(handle);
        i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(handle);
        vTaskDelay(pdMS_TO_TICKS(5)); // Breathing room between pages
    }
}

void oled_draw_string(uint8_t x, uint8_t page, const char *str)
{
    oled_write_cmd(0xB0 + page);
    oled_write_cmd(0x00 + (x & 0x0F));
    oled_write_cmd(0x10 + ((x >> 4) & 0x0F));

    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x40, true);

    for (int i = 0; str[i] != '\0' && x < 122; i++)
    {
        char c = str[i];
        if (c >= 'a' && c <= 'z')
            c = c - 32;
        if (c >= 32 && c <= 90)
        {
            const uint8_t *glyph = font5x7[c - 32];
            i2c_master_write(handle, glyph, 5, true);
            i2c_master_write_byte(handle, 0x00, true);
            x += 6;
        }
    }

    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(handle);
    vTaskDelay(pdMS_TO_TICKS(5));
}

void oled_display_scrolling_text(const char *text)
{
    int text_len = strlen(text);
    int line_chars = 21; // Characters per line
    int total_lines = (text_len + line_chars - 1) / line_chars;

    for (int start_line = 0; start_line < total_lines; start_line++)
    {
        oled_clear();
        vTaskDelay(pdMS_TO_TICKS(10));

        for (int page = 0; page < 8 && (start_line + page) < total_lines; page++)
        {
            char line[22];
            int line_start = (start_line + page) * line_chars;
            int line_len = (text_len - line_start) > line_chars ? line_chars : (text_len - line_start);

            strncpy(line, text + line_start, line_len);
            line[line_len] = '\0';

            oled_draw_string(0, page, line);
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        vTaskDelay(pdMS_TO_TICKS(4000)); // Show each page for 2 seconds
    }
}

// Adjust for SH1106's 132-column RAM: visible panels typically need +2 offset.
static const int SH1106_COL_OFFSET = 2;

// Low-level helper: send a data stream (0x40) to OLED
static void oled_write_data(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x40, true);
    i2c_master_write(handle, (uint8_t *)data, len, true);
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(handle);
    // tiny relax
    vTaskDelay(pdMS_TO_TICKS(2));
}

// Set page/column for SH1106 (adds offset). col in 0..127 visible range.
static void oled_set_column_page(int col, uint8_t page)
{
    int hw_col = col + SH1106_COL_OFFSET; // account for SH1106 internal mapping
    if (hw_col < 0)
        hw_col = 0;
    // low nibble, high nibble and page set commands
    oled_write_cmd(0xB0 + (page & 0x07));
    oled_write_cmd(0x00 + (hw_col & 0x0F));
    oled_write_cmd(0x10 + ((hw_col >> 4) & 0x0F));
}

// Clear a horizontal region (col .. col+width-1) across a specific page
// Only writes zeros to that small region — avoids full-screen clears
static void oled_clear_region(int col, int width, uint8_t page)
{
    if (col < 0)
        col = 0;
    if (col >= 128)
        return;
    if (col + width > 128)
        width = 128 - col;

    oled_set_column_page(col, page);

    // send width bytes of 0x00
    // chunk in pieces to avoid huge I2C writes
    const int chunk = 32;
    uint8_t zeros[chunk];
    memset(zeros, 0x00, chunk);

    int remaining = width;
    while (remaining > 0)
    {
        int w = remaining > chunk ? chunk : remaining;
        oled_write_data(zeros, w);
        remaining -= w;
    }
}

// Draw an 8-byte (8 vertical pixels) "block" at (col, page).
// Patterns are page-oriented bytes (each byte is vertical 8 pixels column in that page).
static void oled_draw_block(int col, uint8_t page, const uint8_t pattern[8])
{
    if (col < 0 || col >= 128)
        return;
    oled_set_column_page(col, page);
    oled_write_data(pattern, 8);
}

// Draw/overwrite a text region: we first clear the small area then draw string
// This uses your existing oled_draw_string but confines to region.
static void oled_draw_text_region(int col, uint8_t start_page, const char *str)
{
    // approximate width: up to 21 chars * 6px = 126 px, but we'll be conservative
    int region_width = 128 - col;
    if (region_width <= 0)
        return;

    // clear pages that text may occupy (we assume up to 2 pages tall)
    for (uint8_t p = start_page; p < start_page + 2 && p < 8; ++p)
    {
        oled_clear_region(col, region_width, p);
    }

    // small delay for device to settle
    vTaskDelay(pdMS_TO_TICKS(4));

    // draw text starting at col,page using existing oled_draw_string
    // but temporarily set column low nibble via oled_set_column_page to respect offset
    // Re-use oled_draw_string logic by calling it (it sets its own columns).
    oled_draw_string(col, start_page, str);
}

// A few compact 8-byte patterns for dot / pulse drawing (vertical bytes for a page)
static const uint8_t PAT_DOT[8] = {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00};
static const uint8_t PAT_BRIGHT[8] = {0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x3C, 0x00, 0x00};
static const uint8_t PAT_OFF[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Simple esp_random wrapper (fast)
static uint32_t fast_rand32(void)
{
    return (uint32_t)esp_random();
}

// --- Drop-in replacement for your oled_show_animation ---
void oled_show_animation(const char *message)
{

    char msg[32];
    strncpy(msg, message, sizeof(msg) - 1);
    msg[31] = '\0';

    // ---------------------------
    // Phase 0: Clean start
    // ---------------------------
    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(10));

    // Dots center math
    const int dot_count = 8;
    const int dot_spacing = 10;
    const int dot_block_width = 8;
    const int total_width = (dot_count - 1) * dot_spacing + dot_block_width;
    const int x_center = 64;
    const int start_x = x_center - total_width / 2;
    const uint8_t page = 2;

    // ---------------------------
    // Phase 1: Apple-like pulsing arc
    // ---------------------------
    const uint8_t DOT[8] = {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00};
    const uint8_t BRIGHT[8] = {0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x3C, 0x00, 0x00};
    const uint8_t OFF[8] = {0};

    for (int cycle = 0; cycle < 3; cycle++)
    {
        for (int f = 0; f < 12; f++)
        {

            int active = f % dot_count; // which dot is brightening

            // draw all dots
            for (int d = 0; d < dot_count; d++)
            {
                const uint8_t *pat = OFF;
                int dist = abs(d - active);

                if (dist == 0)
                    pat = BRIGHT;
                else if (dist == 1)
                    pat = DOT;

                int x = start_x + d * dot_spacing;

                oled_set_column_page(x, page);
                oled_write_data(pat, 8);
            }

            vTaskDelay(pdMS_TO_TICKS(60));
        }
    }

    // ---------------------------
    // Phase 2: Clear screen with your old pattern
    // ---------------------------
    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(10));

    // ---------------------------
    // Phase 3: Slide-in message (simple horizontal ease)
    // ---------------------------
    int len = strlen(msg);
    int msg_px = len * 6;
    int final_x = (128 - msg_px) / 2;
    if (final_x < 0)
        final_x = 0;

    int x_start = 128; // start off-screen right
    int steps = 10;

    for (int s = 0; s <= steps; s++)
    {
        int x = x_start + (final_x - x_start) * s / steps;

        oled_clear();                // <-- old pattern: full screen clear
        oled_draw_string(x, 4, msg); // draw text on page 4
        vTaskDelay(pdMS_TO_TICKS(40));
    }

    // ---------------------------
    // Phase 4: Final settle (breathing dot above text)
    // ---------------------------
    int pulse_x = final_x + msg_px / 2 - 4;
    for (int i = 0; i < 3; i++)
    {
        // draw pulse
        oled_set_column_page(pulse_x, 3);
        oled_write_data(BRIGHT, 8);
        vTaskDelay(pdMS_TO_TICKS(160));

        // clear pulse
        oled_set_column_page(pulse_x, 3);
        oled_write_data(OFF, 8);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // ---------------------------
    // Phase 5: Final display (old behavior)
    // ---------------------------
    oled_clear();
    oled_draw_string(final_x, 4, msg);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// === WIFI FUNCTIONS ===

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi connected!");
    }
}

// void wifi_init() {
//     ESP_LOGI(TAG, "Initializing WiFi...");

//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         nvs_flash_erase();
//         ret = nvs_flash_init();
//     }

//     esp_netif_init();
//     esp_event_loop_create_default();
//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     esp_wifi_init(&cfg);

//     esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
//     esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

//     wifi_config_t wifi_config = {};
//     wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
//     wifi_config.sta.pmf_cfg.capable = true;
//     wifi_config.sta.pmf_cfg.required = false;

//     memcpy(wifi_config.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
//     memcpy(wifi_config.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));

//     esp_wifi_set_mode(WIFI_MODE_STA);
//     esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
//     esp_wifi_start();

//     vTaskDelay(pdMS_TO_TICKS(5000));
//     ESP_LOGI(TAG, "WiFi initialized");
// }

// === ASSEMBLYAI FUNCTIONS ===

bool upload_audio_to_assemblyai(int16_t *audio, size_t len)
{
    ESP_LOGI(TAG, "Uploading %d samples...", len);

    esp_http_client_config_t config = {};
    config.url = "https://api.assemblyai.com/v2/upload";
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "authorization", ASSEMBLYAI_API_KEY);

    uint8_t wav[44];
    uint32_t data_size = len * 2;
    uint32_t file_size = data_size + 36;

    memcpy(wav, "RIFF", 4);
    memcpy(wav + 4, &file_size, 4);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    memcpy(wav + 16, &fmt_size, 4);
    uint16_t audio_format = 1;
    memcpy(wav + 20, &audio_format, 2);
    uint16_t num_channels = 1;
    memcpy(wav + 22, &num_channels, 2);
    uint32_t sample_rate = SAMPLE_RATE;
    memcpy(wav + 24, &sample_rate, 4);
    uint32_t byte_rate = SAMPLE_RATE * 2;
    memcpy(wav + 28, &byte_rate, 4);
    uint16_t block_align = 2;
    memcpy(wav + 32, &block_align, 2);
    uint16_t bits_per_sample = 16;
    memcpy(wav + 34, &bits_per_sample, 2);
    memcpy(wav + 36, "data", 4);
    memcpy(wav + 40, &data_size, 4);

    esp_err_t err = esp_http_client_open(client, 44 + data_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open connection: %d", err);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_write(client, (char *)wav, 44);
    esp_http_client_write(client, (char *)audio, data_size);

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "Upload failed: %d", status_code);
        esp_http_client_cleanup(client);
        return false;
    }

    char response[1024];
    int response_len = esp_http_client_read_response(client, response, sizeof(response) - 1);
    esp_http_client_cleanup(client);

    if (response_len > 0)
    {
        response[response_len] = '\0';
        char *url_start = strstr(response, "\"upload_url\":\"");
        if (url_start)
        {
            url_start += 14;
            char *url_end = strchr(url_start, '"');
            if (url_end)
            {
                int url_len = url_end - url_start;
                strncpy(upload_url, url_start, url_len);
                upload_url[url_len] = '\0';
                return true;
            }
        }
    }

    return false;
}

bool request_transcription()
{
    char body[1024];
    snprintf(body, sizeof(body), "{\"audio_url\":\"%s\"}", upload_url);

    esp_http_client_config_t config = {};
    config.url = "https://api.assemblyai.com/v2/transcript";
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "authorization", ASSEMBLYAI_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_write(client, body, strlen(body));
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200 && status_code != 201)
    {
        esp_http_client_cleanup(client);
        return false;
    }

    char response[2048];
    int total_read = 0;
    int read_len;

    while (total_read < content_length && total_read < sizeof(response) - 1)
    {
        read_len = esp_http_client_read(client, response + total_read, sizeof(response) - total_read - 1);
        if (read_len <= 0)
            break;
        total_read += read_len;
    }

    response[total_read] = '\0';
    esp_http_client_cleanup(client);

    if (total_read > 0)
    {
        char *id_start = strstr(response, "\"id\":");
        if (id_start)
        {
            id_start += 5;
            while (*id_start == ' ' || *id_start == '\t')
                id_start++;
            if (*id_start == '"')
                id_start++;

            char *id_end = strchr(id_start, '"');
            if (id_end)
            {
                int id_len = id_end - id_start;
                strncpy(transcribed_text, id_start, id_len);
                transcribed_text[id_len] = '\0';
                return true;
            }
        }
    }

    return false;
}

bool get_transcription_result()
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.assemblyai.com/v2/transcript/%s", transcribed_text);

    for (int i = 0; i < 30; i++)
    {
        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = 10000;
        config.crt_bundle_attach = esp_crt_bundle_attach;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "authorization", ASSEMBLYAI_API_KEY);

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK)
        {
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int content_length = esp_http_client_fetch_headers(client);

        char response[4096];
        int total_read = 0;
        int read_len;

        while (total_read < content_length && total_read < sizeof(response) - 1)
        {
            read_len = esp_http_client_read(client, response + total_read, sizeof(response) - total_read - 1);
            if (read_len <= 0)
                break;
            total_read += read_len;
        }

        response[total_read] = '\0';
        esp_http_client_cleanup(client);

        if (strstr(response, "\"status\": \"completed\"") || strstr(response, "\"status\":\"completed\""))
        {
            char *text_start = strstr(response, "\"text\":");
            if (text_start)
            {
                text_start += 7;
                while (*text_start == ' ' || *text_start == '\t')
                    text_start++;
                if (*text_start == '"')
                    text_start++;

                char *text_end = strchr(text_start, '"');
                if (text_end)
                {
                    int text_len = text_end - text_start;
                    if (text_len < 500)
                    {
                        strncpy(transcribed_text, text_start, text_len);
                        transcribed_text[text_len] = '\0';
                        return true;
                    }
                }
            }
        }
        else if (strstr(response, "\"status\": \"error\"") || strstr(response, "\"status\":\"error\""))
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    return false;
}

// === I2S AND BUTTON ===

void init_i2s_microphone()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD};

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);

    ESP_LOGI(TAG, "I2S initialized");
}

void init_button()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Button initialized");
}

// === STORAGE ===

void init_storage()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false};

    esp_vfs_spiffs_register(&conf);

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d / %d", used, total);
}

// === LLM CALLBACKS ===

void generate_complete_cb(float tk_s)
{
    printf("\n\nSpeed: %.2f tok/s\n\n", tk_s);
    generation_complete = true;
}

void output_cb(char *token)
{
    printf("%s", token);
    fflush(stdout);

    int token_len = strlen(token);
    if (output_pos + token_len < OUTPUT_BUFFER_SIZE - 1)
    {
        strncpy(output_buffer + output_pos, token, OUTPUT_BUFFER_SIZE - output_pos - 1);
        output_pos += token_len;
        output_buffer[output_pos] = '\0';
    }
}

void removePunctuationInPlace(char *text)
{
    int read = 0;
    int write = 0;

    while (text[read] != '\0')
    {
        char c = text[read];

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c == ' '))
        {
            // Keep alphanumeric + space
            text[write++] = c;
        }
        else if (c == '\n' || c == '\t')
        {
            // Normalize these to a space
            text[write++] = ' ';
        }

        // Skip punctuation and weird symbols
        read++;
    }

    text[write] = '\0'; // Final null terminator
}

// === VOICE ASSISTANT ===
void voice_assistant_mode(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler)
{
    ESP_LOGI(TAG, "Voice assistant mode");

    int16_t *audio_buffer = (int16_t *)malloc(BUFFER_SIZE * sizeof(int16_t));
    if (!audio_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }

    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(20));
    oled_draw_string(0, 2, "PRESS & HOLD");
    oled_draw_string(0, 4, "TO SPEAK");

    while (gpio_get_level(BUTTON_PIN) == 1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Simple recording display - NO ANIMATION
    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(20));
    oled_draw_string(0, 3, "RECORDING...");
    vTaskDelay(pdMS_TO_TICKS(20));

    size_t bytes_read = 0, total = 0;
    int32_t temp[1024];

    // NOW record while button held
    while (gpio_get_level(BUTTON_PIN) == 0 && total < BUFFER_SIZE)
    {
        i2s_read(I2S_PORT, temp, sizeof(temp), &bytes_read, pdMS_TO_TICKS(100));
        for (int i = 0; i < bytes_read / 4 && total < BUFFER_SIZE; i++)
        {
            audio_buffer[total++] = (int16_t)(temp[i] >> 14);
        }
    }

    ESP_LOGI(TAG, "Recorded %d samples", total);

    if (total > SAMPLE_RATE)
    {
        oled_show_animation("UPLOADING");

        // Check WiFi before upload
        esp_err_t wifi_status = esp_wifi_connect();
        if (wifi_status != ESP_OK)
        {
            ESP_LOGI(TAG, "Reconnecting WiFi...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }

        if (upload_audio_to_assemblyai(audio_buffer, total))
        {
            oled_show_animation("TRANSCRIBING");

            if (request_transcription())
            {
                oled_show_animation("PROCESSING");

                if (get_transcription_result())
                {
                    oled_clear();
                    vTaskDelay(pdMS_TO_TICKS(20));
                    oled_draw_string(0, 0, "YOU SAID:");
                    vTaskDelay(pdMS_TO_TICKS(10));

                    char display_text[100];
                    int len = strlen(transcribed_text);
                    if (len > 90)
                        len = 90;
                    strncpy(display_text, transcribed_text, len);
                    display_text[len] = '\0';

                    int line = 2;
                    for (int i = 0; i < len && line < 8; i += 21)
                    {
                        char line_text[22];
                        int line_len = (len - i) > 21 ? 21 : (len - i);
                        strncpy(line_text, display_text + i, line_len);
                        line_text[line_len] = '\0';
                        oled_draw_string(0, line++, line_text);
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }

                    vTaskDelay(pdMS_TO_TICKS(3000));

                    oled_show_animation("THINKING");

                    output_pos = 0;
                    output_buffer[0] = '\0';
                    generation_complete = false;

                    removePunctuationInPlace(transcribed_text);
                    generate(transformer, tokenizer, sampler, transcribed_text, 128, &generate_complete_cb, &output_cb);

                    while (!generation_complete)
                    {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }

                    vTaskDelay(pdMS_TO_TICKS(500));
                    oled_display_scrolling_text(output_buffer);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                else
                {
                    oled_clear();
                    vTaskDelay(pdMS_TO_TICKS(20));
                    oled_draw_string(0, 3, "TRANS FAILED");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
            else
            {
                oled_clear();
                vTaskDelay(pdMS_TO_TICKS(20));
                oled_draw_string(0, 3, "REQ FAILED");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        else
        {
            oled_clear();
            vTaskDelay(pdMS_TO_TICKS(20));
            oled_draw_string(0, 3, "UPLOAD FAILED");
            oled_draw_string(0, 5, "CHECK WIFI");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
    else
    {
        oled_clear();
        vTaskDelay(pdMS_TO_TICKS(20));
        char msg[32];
        snprintf(msg, sizeof(msg), "TOO SHORT:%d", total);
        oled_draw_string(0, 3, msg);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    free(audio_buffer);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void waveform_loop(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler)
{
    static bool last_button_state = true;
    static uint32_t last_press_time = 0;

    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(20));
    oled_draw_string(0, 2, "READY");
    oled_draw_string(0, 4, "PRESS BUTTON");

    while (true)
    {
        bool button_pressed = (gpio_get_level(BUTTON_PIN) == 0);
        uint32_t now = esp_timer_get_time() / 1000;

        if (button_pressed && last_button_state && (now - last_press_time > 500))
        {
            last_press_time = now;
            vTaskDelay(pdMS_TO_TICKS(100));
            voice_assistant_mode(transformer, tokenizer, sampler);

            // Show ready screen again
            oled_clear();
            vTaskDelay(pdMS_TO_TICKS(20));
            oled_draw_string(0, 2, "READY");
            oled_draw_string(0, 4, "PRESS BUTTON");

            last_button_state = false;
        }

        last_button_state = button_pressed;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// === TASK ===

struct VoiceTaskParams
{
    Transformer *transformer;
    Tokenizer *tokenizer;
    Sampler *sampler;
};

void voice_task(void *params)
{
    VoiceTaskParams *data = (VoiceTaskParams *)params;

    oled_show_animation("WIFI INIT");

    // WiFi Manager - handles everything automatically
    esp_err_t wifi_status = wifi_manager_init();

    if (wifi_status == ESP_OK)
    {
        ESP_LOGI(TAG, "Connected to WiFi, continuing with main app");
    }
    else
    {
        ESP_LOGI(TAG, "WiFi setup mode active - waiting for configuration");
        oled_clear();
        vTaskDelay(pdMS_TO_TICKS(20));
        oled_draw_string(0, 2, "WIFI SETUP MODE");
        oled_draw_string(0, 4, "Connect to:");
        oled_draw_string(0, 5, "ESP32-Setup");

        // Wait indefinitely for WiFi configuration
        while (!wifi_manager_is_connected())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGI(TAG, "WiFi configured! Restarting...");
        esp_restart();
    }

    oled_show_animation("I2S INIT");
    init_i2s_microphone();

    waveform_loop(data->transformer, data->tokenizer, data->sampler);
}

// === MAIN ===

extern "C" void app_main()
{
    printf("\nESP32 Voice Assistant\n");

    oled_init();
    vTaskDelay(pdMS_TO_TICKS(50));

    init_button();

    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(20));
    oled_draw_string(20, 2, "ESP32");
    oled_draw_string(10, 4, "VOICE AI");
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

    oled_show_animation("LOADING");
    init_storage();

    static Transformer transformer;
    oled_show_animation("LOAD LLM");
    build_transformer(&transformer, (char *)"/data/stories260K.bin");

    static Tokenizer tokenizer;
    build_tokenizer(&tokenizer, (char *)"/data/tok512.bin", transformer.config.vocab_size);

    static Sampler sampler;
    build_sampler(&sampler, transformer.config.vocab_size, 0.0f, 0.9f, (unsigned int)time(NULL));

    oled_clear();
    vTaskDelay(pdMS_TO_TICKS(20));
    oled_draw_string(30, 3, "READY!");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Ready for voice input");

    static VoiceTaskParams params = {&transformer, &tokenizer, &sampler};

    xTaskCreate(voice_task, "voice_task", 16384, &params, 5, NULL);

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
