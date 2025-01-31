#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_random.h"

#define LED_PIN_1 GPIO_NUM_18
#define LED_PIN_2 GPIO_NUM_23
#define BUTTON_PIN GPIO_NUM_26
#define MAX_TASKS 5
#define DELTA_TIME 1000

static uint32_t last_interrupt_time = 0;

typedef struct {
    int taskNum;
    int execTime;
    int period;
    TaskHandle_t handle;
    int startTime;
} EDFtask;

EDFtask tasks[MAX_TASKS];
bool finished[MAX_TASKS] = {false};

QueueHandle_t taskQueue;
volatile int timer = 0;
int n = 0;

void IRAM_ATTR isr_handler(void *arg) {
    uint32_t current_time = xTaskGetTickCountFromISR();
    if ((current_time - last_interrupt_time) > pdMS_TO_TICKS(200)) {
        uint32_t pin = (uint32_t) arg;
        xQueueSendFromISR(taskQueue, &pin, NULL);
    }
    last_interrupt_time = current_time;
}

void taskFunction(void *pvParameter) {
    EDFtask task = *((EDFtask *) pvParameter);
    int execTimer = 0;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        execTimer += DELTA_TIME;
        printf("%dms | Task number %d, execTime = %d\n", timer, task.taskNum, execTimer);
        if (execTimer == task.execTime) {
            execTimer = 0;
            finished[task.taskNum-1] = true;;
        }
    }
}

int scheduleNextTask() {
    int firstDeadline = 11 * DELTA_TIME;
    int firstTask = -1;
    for (int i = 0; i < n; i++) {
        if (!finished[i]) {
            int temp = tasks[i].period - ((timer - tasks[i].startTime) % tasks[i].period);
            if (temp < firstDeadline) {
                firstDeadline = temp;
                firstTask = i;
            }
        }
    }
    return firstTask;
}

void EDF_scheduler (void *pvParemeter) {
    int pin;
    int currentTask;
    gpio_set_level(LED_PIN_2, 1);
    while (1) {
        if (xQueueReceive(taskQueue, &pin, 10)) {
            if (n < MAX_TASKS) {
                int randomPeriod = ((esp_random() % 6) + 5);
                int randomExecTime = (1 + (esp_random() % (randomPeriod)) / 4) * DELTA_TIME;
                randomPeriod *= DELTA_TIME;
                tasks[n] = (EDFtask) {n+1, randomExecTime, randomPeriod, NULL, timer};
                xTaskCreate(&taskFunction, "Task", 2048, (void *) &tasks[n], 1, &tasks[n].handle);
                printf("Created task %d: execTime = %d, period = %d\n", n+1, randomExecTime, randomPeriod);
                n += 1;
                if (n == MAX_TASKS) {
                    gpio_set_level(LED_PIN_2, 0);
                }
            } else {
                printf("Can't schedule any more tasks.\n");
            }
        }
        for (int i = 0; i < n; i++) {
            if (((timer - tasks[i].startTime) % tasks[i].period) == 0 && tasks[i].startTime != timer) {
                if (finished[i]) {
                    finished[i] = false;
                } else {
                    printf("Task %d didn't finish before the deadline.\n", tasks[i].taskNum);
                }
            }
        }
        currentTask = scheduleNextTask();
        timer += DELTA_TIME;
        if (currentTask != -1) {
            xTaskNotifyGive(tasks[currentTask].handle);
        } else {
            printf("%dms | \n", timer);
        }
        vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
    }
}

void button_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE};

    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, isr_handler, (void *) BUTTON_PIN);
}

void app_main(void)
{
    gpio_set_direction(LED_PIN_2, GPIO_MODE_DEF_OUTPUT);
    taskQueue = xQueueCreate(MAX_TASKS, sizeof(EDFtask));
    button_init();
    xTaskCreate(EDF_scheduler, "EDF scheduler", 2048, NULL, configMAX_PRIORITIES-1, NULL);
}