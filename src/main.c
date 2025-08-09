#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kinesis_bridge, LOG_LEVEL_INF);

/* Use Devicetree aliases so we don't hardcode pins */
#if DT_NODE_HAS_PROP(DT_ALIAS(sw0), gpios)
#define BTN_NODE DT_ALIAS(sw0)
#else
#error "Devicetree alias 'sw0' not found (button). Check board DTS."
#endif

#if DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios)
#define LED_NODE DT_ALIAS(led0)
#else
#error "Devicetree alias 'led0' not found (LED). Check board DTS."
#endif

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static struct gpio_callback btn_cb;

static void button_pressed_isr(const struct device *dev,
                               struct gpio_callback *cb,
                               uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Visual feedback for now; later this will start BLE scanning */
	gpio_pin_toggle_dt(&led);
	LOG_INF("Button press detected (placeholder for BLE scan trigger).");
}

int main(void)
{
	int rc;

	if (!device_is_ready(btn.port) || !device_is_ready(led.port)) {
		LOG_ERR("GPIO devices not ready");
		return 0;
	}

	/* LED output (inactive = off) */
	rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (rc) {
		LOG_ERR("LED config failed: %d", rc);
		return 0;
	}

	/* Button input + interrupt on rising edge (active level per board DTS) */
	rc = gpio_pin_configure_dt(&btn, GPIO_INPUT);
	if (rc) {
		LOG_ERR("Button config failed: %d", rc);
		return 0;
	}
	rc = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		LOG_ERR("Button IRQ config failed: %d", rc);
		return 0;
	}
	gpio_init_callback(&btn_cb, button_pressed_isr, BIT(btn.pin));
	gpio_add_callback(btn.port, &btn_cb);

	LOG_INF("kinesis-bridge: skeleton started; LED will toggle on button press.");

	/* Heartbeat blink */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(1000);
	}
}

