#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "Wifi.h"

// Configuration macros
#define ESP_WIFI_SSID       CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS       CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_MAC        CONFIG_ESP_WIFI_APMAC
#define ESP_WIFI_CH        CONFIG_ESP_WIFI_CHANNEL
#define ESP_MAXIMUM_RETRY   CONFIG_ESP_MAXIMUM_RETRY

// The event group allows multiple bits for each event, but we only care about two events:
// - we are connected to the AP with an IP
// - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

namespace TenderOni::Wifi
{

    /// <summary>
    /// FreeRTOS event group to signal when we are connected
    /// </summary>
    static EventGroupHandle_t s_wifi_event_group;

    /// <summary>
    /// Wifi tag
    /// </summary>
    static const char *TAG = "TenderOni";

    /// <summary>
    /// Cached retry count
    /// </summary>
    static int s_retry_num = 0;

    /// <summary>
    /// Cached init status
    /// </summary>
    static bool is_initialized = false;

    /// <summary>
    /// Basic wifi event handler
    /// </summary>
    static void event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
    {
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_num < ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            ESP_LOGI(TAG,"connect to the AP fail");
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }

    /// <summary>
    /// Converts a string to a mac address unless that mac address is invalid or all 0
    /// </summary>
    /// <param name="mac">the input string</param>
    /// <param name="values">the output mac int array</param>
    /// <returns>the success status of the conversion</returns>
    bool str2mac(const char* mac, uint8_t* values)
    {
        if( 6 == sscanf(
                mac, 
                "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &values[0],
                &values[1], 
                &values[2],
                &values[3],
                &values[4],
                &values[5])){
            return true;
        }else{
            return false;
        }
    }

    /// <summary>
    /// Initialize wifi if not done
    /// </summary>
    void Initialize(void)
    {
        if(is_initialized)
            return;

        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES 
                || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

        s_wifi_event_group = xEventGroupCreate();

        ESP_ERROR_CHECK(esp_netif_init());

        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &event_handler,
            NULL,
            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &event_handler,
            NULL,
            &instance_got_ip));

        // Wifi configuration
        uint8_t bssid[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        bool shouldSetBSSID = str2mac(ESP_WIFI_MAC, bssid);
        uint8_t channel = ESP_WIFI_CH > 0 && ESP_WIFI_CH <= 13
            ? ESP_WIFI_CH 
            : 0;

        wifi_config_t wifi_config =
        {
            .sta = 
            {
                {.ssid = ESP_WIFI_SSID},
                {.password = ESP_WIFI_PASS},
                .scan_method = WIFI_FAST_SCAN,
                .bssid_set = shouldSetBSSID,
                .bssid = { bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5] },
                .channel = channel,
                .listen_interval = 0,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold =
                {
                    .rssi = 0,
                    .authmode = WIFI_AUTH_WPA2_PSK
                },
                .pmf_cfg = 
                {
                    .capable = true,
                    .required = false
                }
            }
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        ESP_ERROR_CHECK(esp_wifi_start() );

        ESP_LOGI(TAG, "wifi_init_sta finished.");

        // Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        // number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler()
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);

        // xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        // happened.
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                     ESP_WIFI_SSID, ESP_WIFI_PASS);
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                     ESP_WIFI_SSID, ESP_WIFI_PASS);
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
        }

        // The event will not be processed after unregister
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
        vEventGroupDelete(s_wifi_event_group);

        is_initialized = true;
    }
}