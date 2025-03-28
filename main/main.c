#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include "pico/stdlib.h"
#include "ssd1306.h"
#include "gfx.h"
#include <stdio.h>

const int TRIGGER_PIN = 2;
const int ECHO_PIN = 3;
const int TRIGGER_PULSE_US = 10 / 1000;
const int MEASUREMENT_INTERVAL_MS = 60;
const int MAX_PULSE_US = 30000;
const float TEMPO_P_CENT = 58.0;

SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

void pin_callback(uint gpio, int events) {
    int instant = to_us_since_boot(get_absolute_time());
    xQueueSendFromISR(xQueueTime, &instant, 0);
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(TRIGGER_PULSE_US));
        gpio_put(TRIGGER_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
    }
}

void echo_task(void *p) {
    uint32_t initial_time, final_time;
    float distance;

    while (1) {
        if (xQueueReceive(xQueueTime, &initial_time, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (xQueueReceive(xQueueTime, &final_time, pdMS_TO_TICKS(1000)) == pdPASS) {

                int pulse_duration = final_time - initial_time;

                if (pulse_duration > MAX_PULSE_US) {
                    distance = -1.0;
                } else {
                    distance = pulse_duration / TEMPO_P_CENT;
                }
                xQueueSend(xQueueDistance, &distance, 0);
            }
        } else {
            distance = -1.0;
        }
    }
}

void oled_task(void *p) {
    printf("Initializing OLED driver\n");
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    float distance;
    char buffer[32];

    while (1) {
        if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(1000)) == pdPASS) {
            gfx_clear_buffer(&disp);
            if (distance < 0) {
                gfx_draw_string(&disp, 0, 0, 1, "Falha");
            } else {
                sprintf(buffer, "Dist: %.1f cm", distance);
                gfx_draw_string(&disp, 0, 0, 1, buffer);
                int bar_length = (int)(distance);
                if (bar_length > 128)
                    bar_length = 128;
                gfx_draw_line(&disp, 0, 20, bar_length, 20);
            }
            gfx_show(&disp);
        }
    }
}

int main() {
    stdio_init_all();
    printf("Iniciando projeto: Sensor HC-SR04 com OLED e RTOS\n");

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_pull_down(ECHO_PIN);

    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &pin_callback);



    xQueueTime = xQueueCreate(10, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(10, sizeof(float));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "TriggerTask", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "EchoTask", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLEDTask", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
}