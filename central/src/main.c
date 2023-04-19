#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>
#include <sys/slist.h>
#include <bluetooth/conn.h>
#include <bluetooth/att.h>
#include <console/console.h>
#include <stdint.h>

static void start_scan(void);
static struct bt_conn *default_conn;

/* Handle 0x0000 is reserved for future use */
#define BT_ATT_FIRST_ATTRIBUTE_HANDLE           0x0001
#define BT_ATT_FIRST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_FIRST_ATTRIBUTE_HANDLE
/* 0xffff is defined as the maximum, and thus last, valid attribute handle */
#define BT_ATT_LAST_ATTRIBUTE_HANDLE            0xffff
#define BT_ATT_LAST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_LAST_ATTRIBUTE_HANDLE

static struct bt_uuid_128 ble_uart_uppercase = BT_UUID_INIT_128(0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x00);

static struct bt_uuid_128 ble_uart_receive_data = BT_UUID_INIT_128(0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x02, 0x03,0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0x00);

static struct bt_uuid_128 ble_uart_notify = BT_UUID_INIT_128(0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x02, 0x03,0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0x11);


static struct bt_uuid *primary_uuid = &ble_uart_uppercase.uuid;

#define CREATE_FLAG(flag) static atomic_t flag = (atomic_t)false
#define SET_FLAG(flag) (void)atomic_set(&flag, (atomic_t)true)
#define UNSET_FLAG(flag) (void)atomic_set(&flag, (atomic_t)false)
#define WAIT_FOR_FLAG(flag) \
	while (!(bool)atomic_get(&flag)) { \
		(void)k_sleep(K_MSEC(1)); \
	}

CREATE_FLAG(flag_discover_complete);
CREATE_FLAG(flag_subscribed);
CREATE_FLAG(flag_write_complete);

static uint16_t chrc_handle;
static uint16_t notify_handle;

static void test_subscribed(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err) {
		printk("Subscribe failed (err %d)\n", err);
	}

	SET_FLAG(flag_subscribed);

	if (!params) {
		printk("params NULL\n");
		return;
	}

	if (params->handle == notify_handle) {
		printk("Subscribed to characteristic\n");
	} else {
		printk("Unknown handle %d\n", params->handle);
	}
}

static volatile size_t num_notifications;
uint8_t test_notify(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data,
		    uint16_t length)
{
	printk("\nReceived notification #%u\n", num_notifications++);
	printk("Length notification %d\n", length);
	uint8_t string[length+1];
    for(int i = 0; i < length;i++){
        string[i] = *((char*)data+i);
    }
    string[length] = '\0';
    printk("\nReceived data from peripheral: %s\n\n",string);
	data = "";

	return BT_GATT_ITER_CONTINUE;
}

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_subscribe_params sub_params = {
	.notify = test_notify,
	.write = test_subscribed,
	.ccc_handle = 0, /* Auto-discover CCC*/
	.disc_params = &disc_params, /* Auto-discover CCC */
	.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
	.value = BT_GATT_CCC_NOTIFY,
};

static void gatt_subscribe(void)
{
	int err;

	UNSET_FLAG(flag_subscribed);

	sub_params.value_handle = chrc_handle;
	err = bt_gatt_subscribe(default_conn, &sub_params);
	if (err < 0) {
		printk("Failed to subscribe\n");
	} else {
		printk("Subscribe request sent\n");
	}
	WAIT_FOR_FLAG(flag_subscribed);
}

static uint8_t discover_func(struct bt_conn *conn,
		const struct bt_gatt_attr *attr,
		struct bt_gatt_discover_params *params)
{
	int err;

	if (attr == NULL) {
		if (chrc_handle == 0) {
			printk("Did not discover long_chrc (%x)",chrc_handle);
		}

		(void)memset(params, 0, sizeof(*params));

		SET_FLAG(flag_discover_complete);

		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (params->type == BT_GATT_DISCOVER_PRIMARY &&
	    bt_uuid_cmp(params->uuid, &ble_uart_uppercase.uuid) == 0) {
		printk("Found service\n");
		params->uuid = NULL;
		params->start_handle = attr->handle + 1;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, params);
		if (err != 0) {
			printk("Discover failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	} else if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, &ble_uart_receive_data.uuid) == 0) {
			printk("Found rvd_chrc\n");
			chrc_handle = chrc->value_handle;
		}else if (bt_uuid_cmp(chrc->uuid, &ble_uart_notify.uuid) == 0) {
			printk("Found notify_chrc\n");
			notify_handle = chrc->value_handle;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

static void gatt_discover(void)
{
	int err;

	printk("Discovering services and characteristics\n");

	static struct bt_gatt_discover_params discover_params;

	discover_params.uuid = primary_uuid;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(default_conn, &discover_params);
	if (err != 0) {
		printk("Discover failed(err %d)\n", err);
	}

	WAIT_FOR_FLAG(flag_discover_complete);
	printk("Discover complete\n");
}

static void gatt_write_cb(struct bt_conn *conn, uint8_t err,
			  struct bt_gatt_write_params *params)
{
	if (err != BT_ATT_ERR_SUCCESS) {
		printk("Write failed: 0x%02X\n", err);
	}

	(void)memset(params, 0, sizeof(*params));

	SET_FLAG(flag_write_complete);
}

static void gatt_write(uint16_t handle, char* chrc_data)
{
	static struct bt_gatt_write_params write_params;
	int err;

	if (handle == chrc_handle) {
		printk("Writing to chrc\n");
		write_params.data = chrc_data;
		write_params.length = strlen(chrc_data);
	}

	write_params.func = gatt_write_cb;
	write_params.handle = handle;

	UNSET_FLAG(flag_write_complete);

	err = bt_gatt_write(default_conn, &write_params);
	if (err != 0) {
		printk("bt_gatt_write failed: %d\n", err);
	}

	WAIT_FOR_FLAG(flag_write_complete);
	printk("write success\n");
}

static void read_entry(void)
{
    console_getline_init();
	bool flag = true;

    while(true){
        printk(">");
        char *s = console_getline();

        printk("Typed line: %s\n", s);

		//Send data to peripheral
		if(flag){
			gatt_discover();
			gatt_subscribe();
		}
		gatt_write(chrc_handle,s);
		flag = false;
    }
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (default_conn) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

	/* connect only to devices in close proximity */
	if (rssi < -70) {
		return;
	}

	if (bt_le_scan_stop()) {
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
	if (err) {
		printk("Create conn to %s failed (%u)\n", addr_str, err);
		start_scan();
	}
}

static void start_scan(void)
{
	/* This demo doesn't require active scan */
	int error;
	error = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);

	if (error) {
		printk("Scanning failed to start (err %d)\n", error);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s (%u)\n", addr, err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	if (conn != default_conn) {
		return;
	}

	printk("Connected: %s\n", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

void main(void)
{
	int error;
	error = bt_enable(NULL);

	if (error) {
		printk("Bluetooth init failed (err %d)\n", error);
		return;
	}

	printk("Bluetooth initialized\n");

	start_scan();
	read_entry();
}