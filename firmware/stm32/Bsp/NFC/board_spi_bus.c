#include "board_spi_bus.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static StaticSemaphore_t s_bus_mutex_storage;
static SemaphoreHandle_t s_bus_mutex;
static TaskHandle_t s_owner_task;
static UBaseType_t s_owner_depth;

static void board_spi_bus_init_once(void)
{
    taskENTER_CRITICAL();
    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_bus_mutex_storage);
    }
    taskEXIT_CRITICAL();
}

void BoardSpiBus_Lock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }

    board_spi_bus_init_once();
    if (s_bus_mutex != NULL) {
        if (xSemaphoreTakeRecursive(s_bus_mutex, portMAX_DELAY) == pdPASS) {
            taskENTER_CRITICAL();
            s_owner_task = xTaskGetCurrentTaskHandle();
            s_owner_depth++;
            taskEXIT_CRITICAL();
        }
    }
}

void BoardSpiBus_Unlock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }

    if (s_bus_mutex != NULL) {
        taskENTER_CRITICAL();
        TaskHandle_t current = xTaskGetCurrentTaskHandle();
        if (s_owner_task != current || s_owner_depth == 0u) {
            taskEXIT_CRITICAL();
            return;
        }
        s_owner_depth--;
        if (s_owner_depth == 0u) {
            s_owner_task = NULL;
        }
        taskEXIT_CRITICAL();
        (void)xSemaphoreGiveRecursive(s_bus_mutex);
    }
}
