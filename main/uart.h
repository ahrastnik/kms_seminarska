#ifndef _UART_H_
#define _UART_H_

#include <stdint.h>


/**
 * List of available commands
 */
typedef enum {
    CMD_RESET,
    CMD_READ,
    CMD_NUM
} command_list_t;

/**
 * UART command processing task
 */
extern void command_task(void*);
/**
 * Command handler
 * 
 * Extracts, validates and executes received commands.
 */
void handle_command(uint8_t *data, int len);

#endif
