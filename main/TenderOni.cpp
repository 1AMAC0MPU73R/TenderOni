#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "TenderOni.h"
#include "Wifi.h"

namespace TenderOni
{
    /// <summary>
    /// Start the application
    /// </summary>
    void Startup(void)
    {
        // Sanity check
        printf("Hello world!\n");

        // Wifi connect
        Wifi::Initialize();

        // Standby and shutdown
        for (int i = 10; i >= 0; i--) {
            printf("Restarting in %d seconds...\n", i);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        printf("Restarting now.\n");
        fflush(stdout);
        esp_restart();
    }
}