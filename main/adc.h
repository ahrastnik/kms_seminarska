#ifndef _ADC_H_
#define _ADC_H_


#include <stdint.h>
#include <stdbool.h>

#include "driver/adc.h"

#define DEFAULT_SAMPLE_BUFFER_SIZE     32768

/**
 * Initialize the ADC
 */
void adc_init(void);

/**
 * Is the sampler currently running
 */
bool is_sampling(void);

/**
 * Start the sampler
 */
void sampler_start(uint16_t sample_number);

/**
 * Stop the sampler
 */
void sampler_stop(void);

#endif
