#include "sys_mode.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../common.h"

#define TAG "sys_mode"

struct sys_mode_state state = {
    .rt_task_delay = DEFAULT_RT_TASK_DELAY,
    .task_delay = DEFAULT_TASK_DELAY,
    .idle_task_delay = DEFAULT_IDLE_TASK_DELAY,
    .ping_delay = DEFAULT_PING_DELAY,
    .ota_running = false,
    .mqtt_normal_operation = false,
    .last_ping_time = -1,
    .last_tb_ping_time = -1,
};
SemaphoreHandle_t xSemaphore;
bool started = false;

void init()
{
    if (!started)
    {
        xSemaphore = xSemaphoreCreateMutex();
        started = true;
    }
}

#define critical_section(section)                      \
    do                                                 \
    {                                                  \
        init();                                        \
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) \
        {                                              \
            {                                          \
                section;                               \
            }                                          \
            xSemaphoreGive(xSemaphore);                \
        }                                              \
    } while (0)

void set_mode(enum ScreenMode mode)
{
    ESP_LOGE(TAG, "Setting mode %d", mode);
    critical_section(state.mode = mode);
}

void set_tmp_mode(enum ScreenMode mode, int sec_duration, enum ScreenMode next_mode)
{
    // ESP_LOGE(TAG, "Setting tmp mode %d for %d seconds", mode, sec_duration);
    critical_section(state.tmp_mode = mode;
                     state.tmp_mode_expiration = time(0) + sec_duration;
                     state.mode = next_mode);
}


enum ScreenMode get_notmp_mode()
{
    enum ScreenMode ret = qr_display;
    critical_section(  
        ret = state.mode;
    );
    return ret;
}

enum ScreenMode get_mode()
{
    enum ScreenMode ret = qr_display;
    critical_section(
        if (state.tmp_mode_expiration > time(0)) {
            ret = state.tmp_mode;
        } else {
            ret = state.mode;
        });
    return ret;
}

void set_bt_device_history(struct bt_device_record bt_device_history[BT_DEVICE_HISTORY_SIZE])
{
    critical_section(memcpy(state.device_history, bt_device_history, sizeof(state.device_history)));
}

void get_bt_device_history(struct bt_device_record bt_device_history[BT_DEVICE_HISTORY_SIZE])
{
    critical_section(memcpy(bt_device_history, state.device_history, sizeof(state.device_history)));
}

void set_version(char version[32])
{
    critical_section(strcpy(state.version, version));
}

void get_version(char version[32])
{
    critical_section(strcpy(version, state.version));
}

void set_rt_task_delay(int rt_task_delay)
{
    critical_section(state.rt_task_delay = rt_task_delay);
}

int get_rt_task_delay()
{
    int ret = 0;
    critical_section(ret = state.rt_task_delay);
    return ret;
}

void set_task_delay(int task_delay)
{
    critical_section(state.task_delay = task_delay);
}

int get_task_delay()
{
    int ret = 0;
    critical_section(ret = state.task_delay);
    return ret;
}

void set_idle_task_delay(int idle_task_delay)
{
    critical_section(state.idle_task_delay = idle_task_delay);
}

int get_idle_task_delay()
{
    int ret = 0;
    critical_section(ret = state.idle_task_delay);
    return ret;
}

void set_ping_delay(int ping_delay){
    critical_section(state.ping_delay = ping_delay);
}

int get_ping_delay()
{
    int ret = 0;
    critical_section(ret = state.ping_delay);
    return ret;
}

void set_ota_running(bool ota_running)
{
    critical_section(state.ota_running = ota_running);
}

bool is_ota_running()
{
    bool ret = false;
    critical_section(ret = state.ota_running);
    return ret;
}

void set_mqtt_normal_operation(bool mqtt_normal_operation)
{
    critical_section(state.mqtt_normal_operation = mqtt_normal_operation);
}

bool is_mqtt_normal_operation()
{
    bool ret = false;
    critical_section(ret = state.mqtt_normal_operation);
    return ret;
}

void set_last_ping_time(int last_ping_time)
{
    critical_section(state.last_ping_time = last_ping_time);
}

int get_last_ping_time()
{
    int ret = 0;
    critical_section(ret = state.last_ping_time);
    return ret;
}

void set_last_tb_ping_time(int last_tb_ping_time)
{
    critical_section(state.last_tb_ping_time = last_tb_ping_time);
}

int get_last_tb_ping_time()
{
    int ret = 0;
    critical_section(ret = state.last_tb_ping_time);
    return ret;
}