#include "uart_lab.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "drv_fpioa.h"
#include "drv_uart.h"

struct dshanpi_uart_lab {
    drv_uart_inst_t *uart;
};

static int uart_lab_apply_config(drv_uart_inst_t *uart, uint32_t baud_rate,
                                 uint8_t parity, uint8_t stop_bits)
{
    struct uart_configure config = {
        .baud_rate = baud_rate,
        .data_bits = DATA_BITS_8,
        .stop_bits = stop_bits,
        .parity = parity,
        .bit_order = BIT_ORDER_LSB,
        .invert = NRZ_NORMAL,
        .bufsz = 0,
        .reserved = 0,
    };
    return drv_uart_set_config(uart, &config);
}

int dshanpi_uart_lab_open(dshanpi_uart_lab_t **lab, uint32_t baud_rate,
                          uint8_t parity, uint8_t stop_bits)
{
    dshanpi_uart_lab_t *instance;
    int tx_routed = 0;

    if (lab == NULL)
        return -1;
    *lab = NULL;
    if (!drv_fpioa_is_func_supported_by_pin(DSHANPI_UART_TX_PIN,
                                             UART2_TXD) ||
        !drv_fpioa_is_func_supported_by_pin(DSHANPI_UART_RX_PIN,
                                             UART2_RXD)) {
        printf("[uart-lab] requested FPIOA route is not supported\n");
        return -2;
    }

    instance = calloc(1, sizeof(*instance));
    if (instance == NULL)
        return -3;
    if (drv_fpioa_set_pin_func(DSHANPI_UART_TX_PIN, UART2_TXD) != 0) {
        free(instance);
        return -4;
    }
    tx_routed = 1;
    if (drv_fpioa_set_pin_func(DSHANPI_UART_RX_PIN, UART2_RXD) != 0) {
        if (tx_routed)
            drv_fpioa_set_pin_func(DSHANPI_UART_TX_PIN, GPIO44);
        free(instance);
        return -4;
    }
    drv_fpioa_set_pin_ds(DSHANPI_UART_TX_PIN, 3);
    drv_fpioa_set_pin_pu(DSHANPI_UART_RX_PIN, 1);

    if (drv_uart_inst_create(DSHANPI_UART_CONTROLLER,
                             &instance->uart) != 0) {
        dshanpi_uart_lab_close(&instance);
        return -5;
    }
    if (dshanpi_uart_lab_configure(instance, baud_rate, parity,
                                   stop_bits) != 0) {
        dshanpi_uart_lab_close(&instance);
        return -6;
    }

    printf("[uart-lab] opened UART2 TX=IO44 RX=IO45, %lu baud\n",
           (unsigned long)baud_rate);
    *lab = instance;
    return 0;
}

void dshanpi_uart_lab_close(dshanpi_uart_lab_t **lab)
{
    if (lab == NULL || *lab == NULL)
        return;
    if ((*lab)->uart != NULL)
        drv_uart_inst_destroy(&(*lab)->uart);
    /* Restore the header pins to GPIO mode after UART Lab closes. */
    drv_fpioa_set_pin_func(DSHANPI_UART_TX_PIN, GPIO44);
    drv_fpioa_set_pin_func(DSHANPI_UART_RX_PIN, GPIO45);
    free(*lab);
    *lab = NULL;
    printf("[uart-lab] closed and restored IO44/IO45 GPIO mode\n");
}

int dshanpi_uart_lab_configure(dshanpi_uart_lab_t *lab,
                               uint32_t baud_rate, uint8_t parity,
                               uint8_t stop_bits)
{
    if (lab == NULL || lab->uart == NULL)
        return -1;
    if (uart_lab_apply_config(lab->uart, baud_rate, parity,
                              stop_bits) != 0)
        return -2;
    return 0;
}

size_t dshanpi_uart_lab_write(dshanpi_uart_lab_t *lab,
                              const uint8_t *data, size_t size)
{
    if (lab == NULL || lab->uart == NULL)
        return 0;
    return drv_uart_write(lab->uart, data, size);
}

int dshanpi_uart_lab_write_all(dshanpi_uart_lab_t *lab,
                               const uint8_t *data, size_t size)
{
    size_t offset = 0;
    unsigned stalled_writes = 0;

    if (lab == NULL || lab->uart == NULL ||
        (data == NULL && size != 0))
        return -1;
    while (offset < size) {
        size_t written = drv_uart_write(lab->uart, data + offset,
                                        size - offset);

        /* drv_uart_write() returns size_t even for its negative errors. */
        if (written > size - offset)
            return -2;
        if (written == 0) {
            if (++stalled_writes >= 3)
                return -3;
            usleep(1000);
            continue;
        }
        stalled_writes = 0;
        offset += written;
    }
    return 0;
}

size_t dshanpi_uart_lab_read(dshanpi_uart_lab_t *lab,
                             uint8_t *data, size_t size)
{
    if (lab == NULL || lab->uart == NULL)
        return 0;
    return drv_uart_read(lab->uart, data, size);
}

int dshanpi_uart_lab_poll(dshanpi_uart_lab_t *lab, int timeout_ms)
{
    if (lab == NULL || lab->uart == NULL)
        return -1;
    return drv_uart_poll(lab->uart, timeout_ms);
}
