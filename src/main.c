/*
 * BLE to USB HID Bridge for Kinesis Advantage 360 Pro
 * Target: nRF52840 Dongle
 * 
 * This bridge connects to the Kinesis keyboard via BLE and forwards
 * HID reports to the host computer via USB. Optimized for Boot Protocol.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(ble_bridge, LOG_LEVEL_INF);

/* USB HID Report Descriptor for Boot Protocol Keyboard */
static const uint8_t hid_report_desc[] = {
    0x05, 0x01,     /* Usage Page (Generic Desktop) */
    0x09, 0x06,     /* Usage (Keyboard) */
    0xA1, 0x01,     /* Collection (Application) */
    
    /* Modifier keys byte */
    0x05, 0x07,     /* Usage Page (Key Codes) */
    0x19, 0xE0,     /* Usage Minimum (224) */
    0x29, 0xE7,     /* Usage Maximum (231) */
    0x15, 0x00,     /* Logical Minimum (0) */
    0x25, 0x01,     /* Logical Maximum (1) */
    0x75, 0x01,     /* Report Size (1) */
    0x95, 0x08,     /* Report Count (8) */
    0x81, 0x02,     /* Input (Data, Variable, Absolute) */
    
    /* Reserved byte */
    0x75, 0x08,     /* Report Size (8) */
    0x95, 0x01,     /* Report Count (1) */
    0x81, 0x01,     /* Input (Constant) */
    
    /* Key array (6 keys) */
    0x05, 0x07,     /* Usage Page (Key Codes) */
    0x19, 0x00,     /* Usage Minimum (0) */
    0x29, 0xFF,     /* Usage Maximum (255) */
    0x15, 0x00,     /* Logical Minimum (0) */
    0x26, 0xFF, 0x00, /* Logical Maximum (255) */
    0x75, 0x08,     /* Report Size (8) */
    0x95, 0x06,     /* Report Count (6) */
    0x81, 0x00,     /* Input (Data, Array) */
    
    0xC0            /* End Collection */
};

/* BLE UUIDs */
#define BT_UUID_HIDS_VAL 0x1812
#define BT_UUID_HIDS BT_UUID_DECLARE_16(BT_UUID_HIDS_VAL)
/* BT_UUID_HIDS_REPORT_VAL is already defined in uuid.h */
#define BT_UUID_HIDS_REPORT BT_UUID_DECLARE_16(BT_UUID_HIDS_REPORT_VAL)

/* Device handles */
static struct bt_conn *current_conn = NULL;
static K_MUTEX_DEFINE(conn_mutex);  /* Mutex to protect current_conn access */
static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_discover_params discover_params;
static struct bt_uuid_16 uuid_hids = BT_UUID_INIT_16(BT_UUID_HIDS_VAL);
static struct bt_uuid_16 uuid_report = BT_UUID_INIT_16(BT_UUID_HIDS_REPORT_VAL);

/* Saved keyboard address for reconnection */
static bt_addr_le_t keyboard_addr;
static bool keyboard_paired = false;

/* HID Report Buffer */
static uint8_t hid_report[8] = {0};
static bool usb_configured = false;
static const struct device *hid_dev;

/* LED Indicators */
#define LED0_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

/* Button for pairing */
#define SW0_NODE DT_ALIAS(sw0)
#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;
static struct k_work button_work;
static uint64_t button_press_time = 0;
static bool button_double_press = false;
#endif

/* Target device name - change this to match your Kinesis */
#define TARGET_DEVICE_NAME "Adv360 Pro"
#define TARGET_DEVICE_NAME_ALT "Adv360 Pro R"
#define TARGET_DEVICE_NAME_ALT2 "Adv360 Pro L"

/* USB HID Callbacks */
static void usb_hid_status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    switch (status) {
    case USB_DC_CONFIGURED:
        LOG_INF("USB configured");
        usb_configured = true;
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
        gpio_pin_set_dt(&led, 1);
#endif
        break;
    case USB_DC_DISCONNECTED:
        LOG_INF("USB disconnected");
        usb_configured = false;
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
        gpio_pin_set_dt(&led, 0);
#endif
        break;
    default:
        break;
    }
}

static const struct hid_ops hid_ops = {
    .get_report = NULL,
    .set_report = NULL,
    .int_in_ready = NULL,
    .int_out_ready = NULL,
};

/* BLE HID Report notification handler */
static uint8_t notify_func(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) {
        LOG_WRN("Unsubscribed");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    if (length >= 8) {
        /* Copy the BLE HID report */
        memcpy(hid_report, data, 8);
        
        /* Forward to USB if configured */
        if (usb_configured && hid_dev) {
            int ret = hid_int_ep_write(hid_dev, hid_report, sizeof(hid_report), NULL);
            if (ret < 0 && ret != -EAGAIN) {
                LOG_ERR("Failed to send HID report: %d", ret);
            }
        }
        
        /* Debug output */
        LOG_DBG("HID Report: %02x %02x %02x %02x %02x %02x %02x %02x",
                hid_report[0], hid_report[1], hid_report[2], hid_report[3],
                hid_report[4], hid_report[5], hid_report[6], hid_report[7]);
    }

    return BT_GATT_ITER_CONTINUE;
}

/* Alternative: Direct subscription without auto-discovery */
static void subscribe_to_reports(struct bt_conn *conn, uint16_t value_handle)
{
    /* Clear any existing subscription params */
    memset(&subscribe_params, 0, sizeof(subscribe_params));
    
    /* Set up subscription */
    subscribe_params.notify = notify_func;
    subscribe_params.value = BT_GATT_CCC_NOTIFY;
    subscribe_params.value_handle = value_handle;
    subscribe_params.ccc_handle = value_handle + 1; /* CCC is typically next handle */
    
    int err = bt_gatt_subscribe(conn, &subscribe_params);
    if (err && err != -EALREADY) {
        LOG_ERR("Subscribe failed (err %d)", err);
        
        /* Try with auto-discovery if manual fails */
        subscribe_params.ccc_handle = 0;
        subscribe_params.end_handle = value_handle + 5;
        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            LOG_ERR("Subscribe with auto-discovery also failed (err %d)", err);
        } else {
            LOG_INF("Subscribed with auto-discovery");
        }
    } else {
        LOG_INF("Subscribed to HID reports");
    }
}

/* GATT Discovery callbacks */
static uint8_t discover_func(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params)
{
    static bool service_found = false;
    static uint16_t service_handle = 0;
    
    if (!attr) {
        if (params->type == BT_GATT_DISCOVER_PRIMARY) {
            /* Finished discovering services, now discover characteristics */
            if (service_found) {
                LOG_INF("HID Service found, discovering characteristics...");
                service_found = false;
                
                /* Reset discover params for characteristic discovery */
                memset(&discover_params, 0, sizeof(discover_params));
                discover_params.uuid = &uuid_report.uuid;
                discover_params.func = discover_func;
                discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
                discover_params.start_handle = service_handle;
                discover_params.end_handle = 0xffff;
                
                int err = bt_gatt_discover(conn, &discover_params);
                if (err) {
                    LOG_ERR("Discover characteristics failed (err %d)", err);
                }
                return BT_GATT_ITER_STOP;
            }
        }
        LOG_WRN("Discovery complete");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("Discovered attr handle %u", attr->handle);

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        /* Found HID service */
        if (bt_uuid_cmp(params->uuid, &uuid_hids.uuid) == 0) {
            LOG_INF("Found HID Service at handle %u", attr->handle);
            service_found = true;
            service_handle = attr->handle;
        }
    } else if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        /* Found HID Report characteristic */
        if (!bt_uuid_cmp(params->uuid, &uuid_report.uuid)) {
            uint16_t value_handle = bt_gatt_attr_value_handle(attr);
            LOG_INF("Found HID Report characteristic at handle %u", attr->handle);
            LOG_INF("Value handle: %u", value_handle);
            
            /* Clear discovery params */
            memset(&discover_params, 0, sizeof(discover_params));
            
            /* Use the alternative subscription function */
            subscribe_to_reports(conn, value_handle);
            
            return BT_GATT_ITER_STOP;
        }
    }

    return BT_GATT_ITER_CONTINUE;
}

/* Forward declarations */
void start_scan(void);
void attempt_reconnect(void);

/* Settings handlers */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb,
                       void *cb_arg)
{
    if (!strcmp(name, "addr")) {
        if (len != sizeof(keyboard_addr)) {
            return -EINVAL;
        }
        read_cb(cb_arg, &keyboard_addr, sizeof(keyboard_addr));
        keyboard_paired = true;
        LOG_INF("Loaded saved keyboard address");
    }
    return 0;
}

static struct settings_handler conf = {
    .name = "ble_bridge",
    .h_set = settings_set
};

/* BLE Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Failed to connect to %s (%u)", addr, err);
        
        k_mutex_lock(&conn_mutex, K_FOREVER);
        if (current_conn) {
            bt_conn_unref(current_conn);
            current_conn = NULL;
        }
        k_mutex_unlock(&conn_mutex);
        
        /* Try to reconnect */
        k_sleep(K_SECONDS(1));
        if (keyboard_paired) {
            attempt_reconnect();
        } else {
            start_scan();
        }
        return;
    }

    LOG_INF("Connected: %s", addr);
    
    k_mutex_lock(&conn_mutex, K_FOREVER);
    current_conn = bt_conn_ref(conn);
    k_mutex_unlock(&conn_mutex);
    
    /* Save keyboard address for reconnection */
    memcpy(&keyboard_addr, bt_conn_get_dst(conn), sizeof(keyboard_addr));
    keyboard_paired = true;
    settings_save_one("ble_bridge/addr", &keyboard_addr, sizeof(keyboard_addr));

    /* Start discovery of HID Service */
    discover_params.uuid = &uuid_hids.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        LOG_ERR("Discover failed (err %d)", err);
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s (reason %u)", addr, reason);

    k_mutex_lock(&conn_mutex, K_FOREVER);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    k_mutex_unlock(&conn_mutex);

    /* Clear HID report on disconnect */
    memset(hid_report, 0, sizeof(hid_report));
    if (usb_configured && hid_dev) {
        hid_int_ep_write(hid_dev, hid_report, sizeof(hid_report), NULL);
    }

    /* Try to reconnect to the same keyboard */
    k_sleep(K_SECONDS(1));
    if (keyboard_paired) {
        LOG_INF("Attempting to reconnect to saved keyboard");
        attempt_reconnect();
    } else {
        start_scan();
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* BLE Scanning */
static bool device_found(struct bt_data *data, void *user_data)
{
    bt_addr_le_t *addr = user_data;
    char name[30];

    if (data->type == BT_DATA_NAME_COMPLETE ||
        data->type == BT_DATA_NAME_SHORTENED) {
        
        memcpy(name, data->data, MIN(data->data_len, sizeof(name) - 1));
        name[MIN(data->data_len, sizeof(name) - 1)] = '\0';

        LOG_DBG("Found device: %s", name);

        /* Check if this is our target keyboard */
        if (strstr(name, TARGET_DEVICE_NAME) ||
            strstr(name, TARGET_DEVICE_NAME_ALT) ||
            strstr(name, TARGET_DEVICE_NAME_ALT2)) {
            
            LOG_INF("Found Kinesis keyboard: %s", name);
            
            /* Stop scanning and connect */
            int err = bt_le_scan_stop();
            if (err) {
                LOG_ERR("Stop scan failed (err %d)", err);
                return true;
            }

            /* Small delay to ensure scan is stopped */
            k_sleep(K_MSEC(100));

            /* Create connection */
            struct bt_conn *conn;
            struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
            
            err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                   param, &conn);
            if (err) {
                LOG_ERR("Create connection failed (err %d)", err);
                k_sleep(K_SECONDS(1));
                start_scan();
            } else {
                /* Connection initiated successfully */
                bt_conn_unref(conn);
            }

            return false; /* Stop parsing */
        }
    }

    return true; /* Continue parsing */
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                    struct net_buf_simple *ad)
{
    /* Parse advertisement data */
    bt_data_parse(ad, device_found, (void *)addr);
}

void start_scan(void)
{
    int err;

    k_mutex_lock(&conn_mutex, K_FOREVER);
    bool already_connected = (current_conn != NULL);
    k_mutex_unlock(&conn_mutex);
    
    if (already_connected) {
        LOG_DBG("Already connected, not scanning");
        return;
    }

    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        LOG_ERR("Scanning failed to start (err %d)", err);
        return;
    }

    LOG_INF("Scanning for Kinesis keyboard...");
}

void attempt_reconnect(void)
{
    k_mutex_lock(&conn_mutex, K_FOREVER);
    bool already_connected = (current_conn != NULL);
    k_mutex_unlock(&conn_mutex);
    
    if (already_connected) {
        LOG_DBG("Already connected");
        return;
    }

    if (!keyboard_paired) {
        LOG_INF("No saved keyboard, starting scan");
        start_scan();
        return;
    }

    LOG_INF("Attempting direct reconnection to saved keyboard");
    
    struct bt_conn *conn;
    struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
    
    int err = bt_conn_le_create(&keyboard_addr, BT_CONN_LE_CREATE_CONN,
                                param, &conn);
    if (err) {
        LOG_ERR("Direct reconnection failed (err %d), starting scan", err);
        start_scan();
    } else {
        LOG_INF("Direct reconnection initiated");
        bt_conn_unref(conn);
    }
}

/* Button work handler - runs in system workqueue context */
#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
static void button_work_handler(struct k_work *work)
{
    if (button_double_press) {
        LOG_INF("Double press detected - clearing pairing and restarting");
        
        /* Disconnect if connected */
        k_mutex_lock(&conn_mutex, K_FOREVER);
        if (current_conn) {
            bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            bt_conn_unref(current_conn);
            current_conn = NULL;
        }
        k_mutex_unlock(&conn_mutex);
        
        /* Clear saved keyboard address */
        keyboard_paired = false;
        memset(&keyboard_addr, 0, sizeof(keyboard_addr));
        
        /* Clear settings */
        settings_save_one("ble_bridge/addr", NULL, 0);
        
        /* Start fresh scan after a delay */
        k_sleep(K_MSEC(100));
        start_scan();
        
        button_double_press = false;
    } else {
        LOG_INF("Single press - attempting reconnect");
        
        /* Single press - try to reconnect */
        k_mutex_lock(&conn_mutex, K_FOREVER);
        bool should_reconnect = (!current_conn && keyboard_paired);
        k_mutex_unlock(&conn_mutex);
        
        if (should_reconnect) {
            attempt_reconnect();
        }
    }
}

/* Button ISR handler - minimal work, just schedules the work item */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                          uint32_t pins)
{
    uint64_t now = k_uptime_get();
    
    /* Detect double press (within 500ms) */
    if ((now - button_press_time) < 500) {
        button_double_press = true;
    } else {
        button_double_press = false;
    }
    
    button_press_time = now;
    
    /* Submit work to system workqueue - non-blocking */
    k_work_submit(&button_work);
}
#endif

int main(void)
{
    int err;

    LOG_INF("BLE to USB HID Bridge starting...");

    /* Initialize LED */
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (!device_is_ready(led.port)) {
        LOG_ERR("LED device not ready");
        return -1;
    }
    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure LED: %d", err);
        return -1;
    }
#endif

    /* Initialize Button */
#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
    if (!device_is_ready(button.port)) {
        LOG_ERR("Button device not ready");
        return -1;
    }
    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err) {
        LOG_ERR("Failed to configure button: %d", err);
        return -1;
    }
    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (err) {
        LOG_ERR("Failed to configure button interrupt: %d", err);
        return -1;
    }
    
    /* Initialize button work item */
    k_work_init(&button_work, button_work_handler);
    
    /* Set up GPIO callback */
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
#endif

    /* Initialize USB HID */
    hid_dev = device_get_binding("HID_0");
    if (!hid_dev) {
        LOG_ERR("Cannot get HID device");
        return -1;
    }

    usb_hid_register_device(hid_dev, hid_report_desc, sizeof(hid_report_desc),
                           &hid_ops);

    err = usb_hid_init(hid_dev);
    if (err) {
        LOG_ERR("Failed to init USB HID: %d", err);
        return -1;
    }

    err = usb_enable(usb_hid_status_cb);
    if (err) {
        LOG_ERR("Failed to enable USB: %d", err);
        return -1;
    }

    /* Initialize Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed: %d", err);
        return -1;
    }

    LOG_INF("Bluetooth initialized");

    /* Register settings handler */
    settings_subsys_init();
    settings_register(&conf);
    
    /* Load settings */
    settings_load();

    /* Try to reconnect to saved keyboard or start scanning */
    k_sleep(K_SECONDS(1));
    if (keyboard_paired) {
        LOG_INF("Found saved keyboard, attempting reconnection");
        attempt_reconnect();
    } else {
        LOG_INF("No saved keyboard, starting scan");
        start_scan();
    }

    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(1));
        
        /* Blink LED if not connected */
        k_mutex_lock(&conn_mutex, K_FOREVER);
        bool is_connected = (current_conn != NULL);
        k_mutex_unlock(&conn_mutex);
        
        if (!is_connected) {
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
            gpio_pin_toggle_dt(&led);
#endif
        }
    }

    return 0;
}

