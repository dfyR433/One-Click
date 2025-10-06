#ifndef CONFIG_H
#define CONFIG_H

const int DNS_PORT = 53;

// ====== Wi-Fi Variables ======
#define WIFI_SSID       "One Click"
#define WIFI_PASS       "12345678"
#define WIFI_CHANNEL    1
#define MAX_CONN        10
#define WIFI_HIDDEN     0   // 1 = hidden SSID, 0 = visible
#define MAX_TX_POWER    127

// ====== Security Settings ======
#define WIFI_AUTH_MODE  WIFI_AUTH_WPA2_PSK

// Protected Management Frames (PMF)
#define PMF false

// Custom MAC
static uint8_t CUSTOM_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x04, 0x33};

// Static IP settings as strings
const char *ip_str = "10.0.0.1";
const char *gw_str = "10.0.0.1";
const char *netmask_str = "255.255.255.0";

// ====== SPIFFS Config ======
#define SPIFFS_BASE_PATH        "/spiffs"
#define SPIFFS_PARTITION        "storage"
#define SPIFFS_MAX_FILES        1
#define SPIFFS_FORMAT_IF_FAILED true

const size_t CHUNK = 1460; // tuned smaller chunk

#endif // CONFIG_H
