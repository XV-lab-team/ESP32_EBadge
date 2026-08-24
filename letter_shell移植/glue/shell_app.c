#include "shell_app.h"
#include "shell.h"
#include "ring.h"

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
/* TODO: #include "main.h"  (or usart.h) so UART_HandleTypeDef is visible */

/*
 * Board glue for letter-shell 2.0.8.
 * Replace the UART TODO blocks with the target project's UART driver.
 * Do NOT copy UAV_Trainer product commands from examples/.
 */

#ifndef SHELL_UART_ISR
/* STM32G4 USART_ISR: bit6 = TC. Prefer TXE on the target chip. */
#define SHELL_UART_ISR   (huart_shell.Instance->ISR)
#define SHELL_UART_TDR   (huart_shell.Instance->TDR)
#define SHELL_UART_TXE   (0x40u)
#endif

/* TODO: map to the debug UART handle of the target board. */
extern UART_HandleTypeDef huart_shell;

ring uart_rx_ring;
char ring_buf[512];
SHELL_TypeDef shell;

signed char myShellRead(char *c)
{
    if (ring_poll(&uart_rx_ring, c) == 0)
        return 0;
    return -1;
}

void myShellWrite(const char c)
{
    while ((SHELL_UART_ISR & SHELL_UART_TXE) == 0) {
        /* wait */
    }
    SHELL_UART_TDR = (uint8_t)c;
}

static void shell_feed_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        (void)ring_push(&uart_rx_ring, (char)data[i]);
}

void shell_app_task(void *arg)
{
    (void)arg;

    /* TODO: start UART RX (DMA+IDLE, IRQ, or other). */

    ring_init(&uart_rx_ring, ring_buf, sizeof(ring_buf));
    shell.read = myShellRead;
    shell.write = myShellWrite;
    shellInit(&shell);

    for (;;) {
        /*
         * TODO: when a RX frame or IRQ buffer is ready, call
         * shell_feed_bytes(buf, len) then clear the RX-ready flag.
         */
        shellTask(&shell);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static int shell_cmd_hello(int argc, char *argv[])
{
    SHELL_TypeDef *sh;

    (void)argc;
    (void)argv;
    sh = shellGetCurrent();
    if (sh == NULL)
        return -1;
    shellPrint(sh, "hello letter-shell\r\n");
    return 0;
}

SHELL_EXPORT_CMD_EX(hello, shell_cmd_hello, "hello test", hello);
