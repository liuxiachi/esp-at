/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_at.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define IO0_PIN     GPIO_NUM_0
#define IO1_PIN     GPIO_NUM_1

static TaskHandle_t s_io1_blink_handle = NULL;

/* IO1 闪烁任务：亮1秒、灭1秒循环 */
static void io1_blink_task(void *pvParameters)
{
    while (1) {
        gpio_set_level(IO1_PIN, 1);           // 绿灯亮
        vTaskDelay(pdMS_TO_TICKS(1000));       // 等待 1 秒
        gpio_set_level(IO1_PIN, 0);           // 绿灯灭
        vTaskDelay(pdMS_TO_TICKS(1000));       // 等待 1 秒
    }
}

/* Wi-Fi 事件处理：获取到 IP 后执行 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* IO0 拉低（红灯灭） */
        gpio_set_level(IO0_PIN, 0);

        /* 启动 IO1 闪烁任务（防止重复创建） */
        if (s_io1_blink_handle == NULL) {
            xTaskCreate(io1_blink_task, "io1_blink", 2048, NULL, 5, &s_io1_blink_handle);
        }
    }else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* 断网：停止绿灯闪烁任务，绿灯灭，红灯亮 */
        if (s_io1_blink_handle != NULL) {
            vTaskDelete(s_io1_blink_handle);
            s_io1_blink_handle = NULL;
        }
        gpio_set_level(IO1_PIN, 0);  // 绿灯灭
        gpio_set_level(IO0_PIN, 1);  // 红灯亮
    }
}

/* 初始化函数，AT 启动时自动调用 */
static bool at_custom_init(void)
{
    /* 配置 IO0：输出，上电拉高（红灯亮） */
    gpio_config_t io0_conf = {
        .pin_bit_mask = (1ULL << IO0_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io0_conf);
    gpio_set_level(IO0_PIN, 1);  // 上电拉高

    /* 配置 IO1：输出，上电拉低（绿灯灭） */
    gpio_config_t io1_conf = {
        .pin_bit_mask = (1ULL << IO1_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io1_conf);
    gpio_set_level(IO1_PIN, 0);  // 上电拉低

    /* 注册 IP 事件回调 */
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);
    /* 注册 Wi-Fi 断开事件回调 */
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                           &wifi_event_handler, NULL);

    return true;
}

/* 注册初始化函数，优先级 1，AT 启动时自动调用 */
ESP_AT_CMD_SET_INIT_FN(at_custom_init, 1);

static uint8_t at_test_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "test command: <AT%s=?> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_query_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "query command: <AT%s?> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_test(uint8_t para_num)
{
    uint8_t index = 0;

    // get first parameter, and parse it into a digit
    int32_t digit = 0;
    if (esp_at_get_para_as_digit(index++, &digit) != ESP_AT_PARA_PARSE_RESULT_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // get second parameter, and parse it into a string
    uint8_t *str = NULL;
    if (esp_at_get_para_as_str(index++, &str) != ESP_AT_PARA_PARSE_RESULT_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // allocate a buffer and construct the data, then send the data to mcu via interface (uart/spi/sdio/socket)
    uint8_t *buffer = (uint8_t *)malloc(512);
    if (!buffer) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    int len = snprintf((char *)buffer, 512, "setup command: <AT%s=%d,\"%s\"> is executed\r\n",
                       esp_at_get_current_cmd_name(), digit, str);
    esp_at_port_write_data(buffer, len);

    // remember to free the buffer
    free(buffer);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_exe_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "execute command: <AT%s> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_struct at_custom_cmd[] = {
    {"+TEST", at_test_cmd_test, at_query_cmd_test, at_setup_cmd_test, at_exe_cmd_test},
    /**
     * @brief You can define your own AT commands here.
     */
};

bool esp_at_custom_cmd_register(void)
{
    return esp_at_custom_cmd_array_regist(at_custom_cmd, sizeof(at_custom_cmd) / sizeof(esp_at_cmd_struct));
}

ESP_AT_CMD_SET_INIT_FN(esp_at_custom_cmd_register, 1);
