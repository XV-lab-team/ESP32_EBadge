#include "shell_app.h"

#include <stdint.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ring.h"
#include "shell.h"

/*
 * Transport: USB Serial/JTAG (GPIO19/20), same COM the PC already uses for
 * download and ESP_LOG. Do not init TinyUSB / USB-OTG — that would steal the PHY.
 */

static const char *TAG = "shell";

#define SHELL_TASK_STACK_BYTES  4096
#define SHELL_TASK_PRIORITY     5
#define SHELL_POLL_MS           10

ring uart_rx_ring;
static char ring_buf[512];
SHELL_TypeDef shell;

signed char myShellRead(char *c)
{
    if (ring_poll(&uart_rx_ring, c) == 0) {
        return 0;
    }
    return -1;
}

void myShellWrite(const char c)
{
    (void)usb_serial_jtag_write_bytes(&c, 1, pdMS_TO_TICKS(20));
}

static void shell_feed_bytes(const uint8_t *data, uint32_t len)
{
    static uint8_t prev;
    uint32_t i;

    for (i = 0; i < len; i++) {
        /* Terminals often send CRLF; letter-shell treats both as Enter. */
        if (data[i] == '\n' && prev == '\r') {
            prev = data[i];
            continue;
        }
        prev = data[i];
        (void)ring_push(&uart_rx_ring, (char)data[i]);
    }
}

static void shell_app_task(void *arg)
{
    uint8_t rx[64];
    int n;
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 256,
    };

    (void)arg;

    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed");
        vTaskDelete(NULL);
        return;
    }
    /* Let ESP_LOG share the same FIFO via the driver, instead of fighting it. */
    usb_serial_jtag_vfs_use_driver();

    ring_init(&uart_rx_ring, ring_buf, sizeof(ring_buf));
    shell.read = myShellRead;
    shell.write = myShellWrite;
    shellInit(&shell);

    ESP_LOGI(TAG, "letter-shell on USB Serial/JTAG");

    for (;;) {
        n = usb_serial_jtag_read_bytes(rx, sizeof(rx), 0);
        if (n > 0) {
            shell_feed_bytes(rx, (uint32_t)n);
        }
        shellTask(&shell);
        vTaskDelay(pdMS_TO_TICKS(SHELL_POLL_MS));
    }
}

void shell_app_start(void)
{
    BaseType_t ok = xTaskCreate(shell_app_task, "shell", SHELL_TASK_STACK_BYTES,
                                NULL, SHELL_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(shell) failed");
    }
}

static int shell_cmd_hello(int argc, char *argv[])
{
    SHELL_TypeDef *sh;

    (void)argc;
    (void)argv;
    sh = shellGetCurrent();
    if (sh == NULL) {
        return -1;
    }
    shellPrint(sh, "hello letter-shell\r\n");
    return 0;
}

SHELL_EXPORT_CMD_EX(hello, shell_cmd_hello, "hello test", hello);
