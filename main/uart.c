#include "uart.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/uart.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_log.h"

#include "adc.h"

#define TAG     "uart"

#define BUF_SIZE            1024

#define MAX_COMMAND_LEN     32
#define MIN_COMMAND_LEN     2
#define CMD_DELIMITER       " "

/**
 * Command list
 * 
 * Note - The Commands(and their order) in this list must EXACTLY MATCH
 * the commands in the "command_list_t" enumerator.
 */
const char* COMMANDS[] = {
    "reset",
    "read",
    NULL
};

void command_task(void *ignore) {
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);

    // UART buffer
    uint8_t data[BUF_SIZE];

    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE, 20 / portTICK_RATE_MS);
        // Attempt to decode a command from the received data
        handle_command(data, len);
    }
    vTaskDelete(NULL);
}

void handle_command(uint8_t *data, int len) {
    // No data received, so simply continue...
    if(len == 0) return;

    if(len < 0) {
        ESP_LOGE(TAG, "UART transmission error!");
        return;
    }
    // Length safey check
    if(len < MIN_COMMAND_LEN || len > MAX_COMMAND_LEN) {
        ESP_LOGE(TAG, "Invalid command length!");
        return;
    }
    // Check for CR/LF
    if(data[len-2] != '\r' || data[len-1] != '\n') {
        ESP_LOGE(TAG, "Invalid command format! Commands must end with CLRF.");
        return;
    }
    // Move the null termination to the start of CR/LF
    data[len-2] = '\0';
    // Convert the command to lowercase
    uint16_t i;
    for(i = 0; data[i]; i++) {
        data[i] = tolower(data[i]);
    }
    i = 0;
    int16_t cmd_index = -1;
    // Get arguments, if they exist
    char *cmd = strtok((char *)data, CMD_DELIMITER);
    char *args = strtok(NULL, CMD_DELIMITER);
    // Check if the received string is a command
    while(COMMANDS[i] != NULL) {
        if(strcmp(cmd, COMMANDS[i]) == 0) {
            cmd_index = i;
            break;
        }
        i++;
    }

    // Handle the received command
    switch (cmd_index) {
        case CMD_RESET:
            // Reset the CPU
            ESP_LOGI(TAG, "Reseting!");
            fflush(stdout);
            esp_restart();
            break;

        case CMD_READ:
            if (args == NULL || is_sampling()) {
                break;
            }
            // Parse the sample number
            uint16_t sample_number = strtoul(args, NULL, 10);
            if (errno == ERANGE) {
                ESP_LOGE(TAG, "Failed to convert the sample number!");
                break;
            }
            ESP_LOGI(TAG, "Reading samples! %d", sample_number);
            sampler_start(sample_number);
            break;
        
        default:
            if(cmd_index < 0) {
                ESP_LOGE(TAG, "Invalid command!");
            } else {
                ESP_LOGE(TAG, "Command wasn't yet implemented!");
            }
            break;
    }
}
