#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize WiFi manager
// Returns: ESP_OK if credentials exist and connection successful
//          ESP_ERR_NOT_FOUND if no credentials stored (AP mode started)
//          ESP_FAIL if connection failed (AP mode started)
esp_err_t wifi_manager_init(void);

// Check if connected to WiFi
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
