#include "adc.h"

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "driver/periph_ctrl.h"
#include "esp_adc_cal.h"

#define TAG     "adc"

#define DEFAULT_VREF    3300        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling
#define SAMPLER_FREQUENCY   12000 // [Hz]
#define TIMER_DIVIDER   (TIMER_BASE_CLK / SAMPLER_FREQUENCY)

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_bits_width_t bit_width = ADC_WIDTH_BIT_12;
static const adc_channel_t channel = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;

static uint16_t *sample_buffer = NULL;
static uint16_t sample_buffer_size = DEFAULT_SAMPLE_BUFFER_SIZE;
static volatile uint16_t sample_buffer_number = 0;
static volatile uint16_t expected_samples = 0;
static bool is_sampler_running = false;

static xQueueHandle sampler_queue = NULL;

/*
 * Timer group0 ISR handler
 *
 * Note:
 * We don't call the timer API here because they are not declared with IRAM_ATTR.
 * If we're okay with the timer irq not being serviced while SPI flash cache is disabled,
 * we can allocate this interrupt without the ESP_INTR_FLAG_IRAM flag and use the normal API.
 */
void IRAM_ATTR sampler_isr(void *para)
{
    int timer_idx = (int) para;

    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    // uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[timer_idx].update = 1;

    /* Clear the interrupt
       and update the alarm time for the timer with without reload */
    TIMERG0.int_clr_timers.t0 = 1;

    // Read ADC
    uint16_t sample = adc1_get_raw(ADC_CHANNEL_6);
    sample_buffer_number++;

    if (sample_buffer_number < expected_samples) {
        /* After the alarm has been triggered
        we need enable it again, so it is triggered the next time */
        TIMERG0.hw_timer[timer_idx].config.alarm_en = TIMER_ALARM_EN;
    } else {
        // Stop the sampler, when all samples were received
        sampler_stop();
    }

    /* Now just send the sample back to the main program task */
    xQueueSendFromISR(sampler_queue, &sample, NULL);
}

/*
 * Initialize selected timer of the timer group 0
 *
 * timer_idx - the timer number to initialize
 * auto_reload - should the timer auto reload on alarm?
 * timer_interval_sec - the interval of alarm to set
 */
static void sampler_init(int group, int timer_idx) {
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = 4000;//TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = true;
    timer_init(group, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(group, timer_idx, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(group, timer_idx, 10);
    timer_enable_intr(group, timer_idx);
    timer_isr_register(group, timer_idx, sampler_isr,
        (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);
}

void sampler_start(uint16_t sample_number) {
    if (is_sampler_running) {
        return;
    }
    // Adjust the sample buffer size, if it's too small
    if (sample_number > sample_buffer_size) {
        uint16_t buffer_size = sample_buffer_size;
        while (sample_number <= sample_buffer_size) {
            buffer_size *= 1.5;
        }
        uint16_t *new_sample_buffer = realloc(sample_buffer, buffer_size * sizeof(uint16_t));
        if (new_sample_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to reallocate the sample buffer!");
            return;
        }
        sample_buffer = new_sample_buffer;
        sample_buffer_size = buffer_size;
    }
    // Reset the counter
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
    // Reset the sample buffer index
    sample_buffer_number = 0;
    // Reenable the alarm
    TIMERG0.hw_timer[TIMER_0].config.alarm_en = TIMER_ALARM_EN;
    // Start the sampler timer
    expected_samples = sample_number;
    timer_start(TIMER_GROUP_0, TIMER_0);
    is_sampler_running = true;
}

void sampler_stop(void) {
    if (!is_sampler_running) {
        return;
    }
    // Stop the sampler timer
    is_sampler_running = false;
    timer_pause(TIMER_GROUP_0, TIMER_0);
}

static void sampler_task(void *arg) {
    while (1) {
        uint16_t sample;
        xQueueReceive(sampler_queue, &sample, portMAX_DELAY);

        sample_buffer[sample_buffer_number - 1] = sample;

        // Post the buffered samples
        if (sample_buffer_number >= expected_samples) {
            uint16_t i;
            for (i = 0; i < sample_buffer_number; i++) {
                printf("%u", sample_buffer[i]);
                if (i < (sample_buffer_number - 1)) {
                    printf(",");
                } else {
                    printf("\n");
                }
            }
        }
        
    }
    vTaskDelete(NULL);
}

void adc_init(void) {
    //Configure ADC
    adc1_config_width(bit_width);
    adc1_config_channel_atten(channel, atten);

    // Initialize sampler
    sampler_init(TIMER_GROUP_0, TIMER_0);
    sampler_queue = xQueueCreate(20, sizeof(uint16_t));

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    // esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    sample_buffer = malloc(sizeof(uint16_t) * DEFAULT_SAMPLE_BUFFER_SIZE);

    xTaskCreate(sampler_task, "sampler_task", 4096, NULL, 5, NULL);
}

bool is_sampling(void) {
    return is_sampler_running;
}
