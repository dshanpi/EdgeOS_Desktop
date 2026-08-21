#ifndef DSHANPI_UART_LAB_H
#define DSHANPI_UART_LAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct dshanpi_uart_lab dshanpi_uart_lab_t;

/* DongshanPI K230 UART2 header routing. */
#define DSHANPI_UART_CONTROLLER 2
#define DSHANPI_UART_TX_PIN 44
#define DSHANPI_UART_RX_PIN 45

int dshanpi_uart_lab_open(dshanpi_uart_lab_t **lab, uint32_t baud_rate,
                          uint8_t parity, uint8_t stop_bits);
void dshanpi_uart_lab_close(dshanpi_uart_lab_t **lab);
int dshanpi_uart_lab_configure(dshanpi_uart_lab_t *lab,
                               uint32_t baud_rate, uint8_t parity,
                               uint8_t stop_bits);
size_t dshanpi_uart_lab_write(dshanpi_uart_lab_t *lab,
                              const uint8_t *data, size_t size);
int dshanpi_uart_lab_write_all(dshanpi_uart_lab_t *lab,
                               const uint8_t *data, size_t size);
size_t dshanpi_uart_lab_read(dshanpi_uart_lab_t *lab,
                             uint8_t *data, size_t size);
int dshanpi_uart_lab_poll(dshanpi_uart_lab_t *lab, int timeout_ms);

#endif
