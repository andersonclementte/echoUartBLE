#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <stdio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <kernel.h>
#include <services/application.h>

typedef void (*ble_uart_service_rx_callback) (const uint8_t *data, size_t length);
//static ssize_t client_data(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t write_test_chrc(struct bt_conn *conn,
				    const struct bt_gatt_attr *attr,
				    const void *buf, uint16_t len,
				    uint16_t offset, uint8_t flags);
static void ble_uart_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);
int ble_uart_service_register(const ble_uart_service_rx_callback callback);
int ble_uart_service_transmit(const uint8_t *buffer, size_t len);