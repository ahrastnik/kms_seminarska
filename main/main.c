/* Seminarska naloga pri predmetu Kompleksni merilni sistemi

   This code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   Author: Adam Hrastnik
   Date: 6.1.2020
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "adc.h"
#include "uart.h"


void app_main() {
    adc_init();

    // Start the UART task
    xTaskCreate(&command_task, "command_task", 4096, NULL, 10, NULL);
}
