#pragma once
struct wifi_ap_record_t { int unused; };
#define ESP_OK 0
extern bool testAssociated;
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t*) { return testAssociated ? ESP_OK : -1; }
