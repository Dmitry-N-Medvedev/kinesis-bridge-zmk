#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(kinesis_bridge, LOG_LEVEL_INF);

/* --- Devicetree aliases for PCA10059 button/LED --- */
#if DT_NODE_HAS_PROP(DT_ALIAS(sw0), gpios)
#define BTN_NODE DT_ALIAS(sw0)
#else
#error "Devicetree alias 'sw0' not found. Check the board DTS/aliases."
#endif

#if DT_NODE_HAS_PROP(DT_ALIAS(led0), gpios)
#define LED_NODE DT_ALIAS(led0)
#else
#error "Devicetree alias 'led0' not found. Check the board DTS/aliases."
#endif

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static struct gpio_callback btn_cb;
static atomic_t last_edge_ms = ATOMIC_INIT(0); /* for debounce */

static void button_work_handler(struct k_work *work);
K_WORK_DEFINE(button_work, button_work_handler);

#define DEBOUNCE_MS 50

/* Read logical button state (pressed = true) accounting for active level. */
static bool button_pressed(void)
{
	int val = gpio_pin_get_dt(&btn);
	bool active_low = (btn.dt_flags & GPIO_ACTIVE_LOW) != 0;
	return active_low ? (val == 0) : (val > 0);
}

/* ISR: light work; schedule a worker (with debounce) */
static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int64_t now = k_uptime_get();
	int64_t prev = atomic_get(&last_edge_ms);

	if ((now - prev) < DEBOUNCE_MS) {
		return; /* bouncing, ignore */
	}
	atomic_set(&last_edge_ms, (int)now);

	k_work_submit(&button_work);
}

/* Runs in thread context after ISR */
static void button_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	bool pressed = button_pressed();
	if (pressed) {
		LOG_INF("BUTTON: RELEASED");
		/* LED on while pressed (handles active-low via DT flags) */
		gpio_pin_set_dt(&led, 1);
	} else {
		LOG_INF("BUTTON: PRESSED");
		gpio_pin_set_dt(&led, 0);
	}
}

int main(void)
{
	int err;

	/* Bring up USB so console/logs go to CDC-ACM */
	err = usb_enable(NULL);
	if (err) {
		/* If USB fails, logs may not be visible; continue anyway. */
	}

	if (!device_is_ready(btn.port) || !device_is_ready(led.port)) {
		LOG_ERR("GPIO controller not ready (check sw0/led0 aliases)");
		return 0;
	}

	/* LED off initially */
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	/* Button input with DT flags (pull-ups / active-low, etc.) */
	gpio_pin_configure_dt(&btn, GPIO_INPUT | btn.dt_flags);

	/* Interrupt on BOTH edges so we report press and release */
	err = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_ERR("gpio_pin_interrupt_configure_dt failed: %d", err);
		return 0;
	}

	gpio_init_callback(&btn_cb, button_isr, BIT(btn.pin));
	gpio_add_callback(btn.port, &btn_cb);

	LOG_INF("Ready. Press and release the button to see events.");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}

