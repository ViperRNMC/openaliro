/* ESP-IDF GPIO/IRQ backend for the DW3000 decadriver — implements dw3000_hw.h.
 * Replaces the Zephyr deps/dw3000/platform/dw3000_hw.c (not compiled here).
 *
 * IRQ mirrors the Zephyr design: the GPIO ISR wakes a dedicated high-priority
 * task (pinned to core 1) that calls dwt_isr() while the IRQ line stays high —
 * dwt_isr does SPI, so it cannot run in true ISR context. Also provides the
 * cycle-counter diag symbols that dwt_uwb_driver/dw3000/dw3000_device.c
 * references (Xtensa CCOUNT via esp_cpu_get_cycle_count). */
#include "dw3000_hw.h"

#include "board_pins.h"
#include "deca_device_api.h"
#include "dw3000_spi.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *const TAG = "dw3000_hw";

/* Diag cycle-counter exports referenced by dw3000_device.c (RF-arm trace). */
volatile uint32_t g_dw_cyc_gpio;
volatile uint32_t g_dw_cyc_work;
volatile uint32_t g_dw_cyc_isrdone;
volatile uint32_t g_dw_cyc_per_us = 240; /* ESP32-S3 core @240 MHz */

// Return the current CPU cycle count, used as the DW3000 driver's cycle-counter timebase on this port.
uint32_t dw3000_dwt_cyccnt(void) { return esp_cpu_get_cycle_count(); }

static bool s_asleep;
static bool s_irq_enabled;
static SemaphoreHandle_t s_irq_sem;
static TaskHandle_t s_irq_task;

// Mark the DW3000 as asleep. Does not itself put the chip to sleep; only updates local state.
void dw3000_hw_mark_asleep(void) { s_asleep = true; }
// Return whether the DW3000 is currently marked asleep.
bool dw3000_hw_is_asleep(void) { return s_asleep; }

// Initialize the DW3000 hardware port: configure the reset line as input (relying on an external
// pull-up, driven low only when active) and the wakeup line as output held high, then initialize SPI.
// Returns the result of dw3000_spi_init(); 0 on success.
int dw3000_hw_init(void)
{
	/* Reset line: input = released (external pull-up); active low when driven. */
	gpio_config_t rst = {
		.pin_bit_mask = 1ULL << WOZ_DW3000_PIN_RST,
		.mode = GPIO_MODE_INPUT,
	};
	gpio_config(&rst);

	/* Wakeup line held active (matches the Zephyr GPIO_OUTPUT_ACTIVE default). */
	gpio_config_t wk = {
		.pin_bit_mask = 1ULL << WOZ_DW3000_PIN_WAKEUP,
		.mode = GPIO_MODE_OUTPUT,
	};
	gpio_config(&wk);
	gpio_set_level(WOZ_DW3000_PIN_WAKEUP, 1);

	return dw3000_spi_init();
}

// GPIO interrupt handler for the DW3000 IRQ line.
// Runs in IRAM. Latches the cycle count at interrupt time into g_dw_cyc_gpio, then signals s_irq_sem;
// yields to a higher-priority task if the semaphore give woke one.
static void IRAM_ATTR dw3000_gpio_isr(void *arg)
{
	(void)arg;
	g_dw_cyc_gpio = esp_cpu_get_cycle_count();
	BaseType_t hpw = pdFALSE;
	xSemaphoreGiveFromISR(s_irq_sem, &hpw);
	if (hpw) {
		portYIELD_FROM_ISR();
	}
}

// Background task that services DW3000 interrupts: blocks on the IRQ semaphore, then calls
// dwt_isr() in a loop for as long as the IRQ line stays asserted. Records cycle-count timestamps
// (g_dw_cyc_work, g_dw_cyc_isrdone) around the service loop for latency diagnostics. Runs for the
// lifetime of the program; never returns.
static void dw3000_isr_task(void *arg)
{
	(void)arg;
	for (;;) {
		xSemaphoreTake(s_irq_sem, portMAX_DELAY);
		g_dw_cyc_work = esp_cpu_get_cycle_count();
		while (gpio_get_level(WOZ_DW3000_PIN_IRQ)) {
			dwt_isr();
		}
		g_dw_cyc_isrdone = esp_cpu_get_cycle_count();
	}
}

// Configure the DW3000 IRQ GPIO, install the shared ISR service, and start the pinned ISR worker task.
// Idempotent: safe to call more than once, the ISR service and worker task are each created only once.
// The worker task runs on core 1 at priority 23 (above esp_timer and Thread) so DS-TWR slot callbacks
// (RX/TX-done) are not delayed by preemption; it is bursty and mostly blocked on the IRQ semaphore.
// Leaves the interrupt enabled. Returns 0 on success, -1 if the ISR service failed to install.
int dw3000_hw_init_interrupt(void)
{
	static bool isr_service_installed;

	if (s_irq_sem == NULL) {
		s_irq_sem = xSemaphoreCreateBinary();
	}

	gpio_config_t irq = {
		.pin_bit_mask = 1ULL << WOZ_DW3000_PIN_IRQ,
		.mode = GPIO_MODE_INPUT,
		.intr_type = GPIO_INTR_POSEDGE,
	};
	gpio_config(&irq);

	if (!isr_service_installed) {
		esp_err_t e = gpio_install_isr_service(0);
		if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
			ESP_LOGE(TAG, "gpio_install_isr_service failed: %d", e);
			return -1;
		}
		isr_service_installed = true;
	}
	gpio_isr_handler_add(WOZ_DW3000_PIN_IRQ, dw3000_gpio_isr, NULL);

	if (s_irq_task == NULL) {
		/* Pinned to core 1 (BLE/Wi-Fi live on core 0). Priority 23 (above esp_timer=22
		 * and the Matter/Thread service tasks): the DW3000 RX/TX-done callbacks drive the
		 * DS-TWR slot choreography (POLL arm, Response TX, Final-RFRAME arm) with only ~2 ms
		 * between steps. At 20, esp_timer/Thread preempted this task ~2.4 ms per TX-done,
		 * so the Final arm (POLL+2 slots) always ran after the Final had passed (t6 lost ->
		 * garbage ToF). 23 lets the callback fire promptly; the task is bursty and mostly
		 * blocks on the IRQ semaphore, so it does not starve the system. */
		xTaskCreatePinnedToCore(dw3000_isr_task, "dw3000_isr", 4096, NULL,
					23, &s_irq_task, 1);
	}
	s_irq_enabled = true;
	ESP_LOGI(TAG, "IRQ on GPIO%d", WOZ_DW3000_PIN_IRQ);
	return 0;
}

// Re-enable the DW3000 IRQ GPIO interrupt after a prior disable. No-op if already enabled.
void dw3000_hw_interrupt_enable(void)
{
	if (!s_irq_enabled) {
		gpio_intr_enable(WOZ_DW3000_PIN_IRQ);
		s_irq_enabled = true;
	}
}

// Disable the DW3000 IRQ GPIO interrupt. No-op if already disabled.
void dw3000_hw_interrupt_disable(void)
{
	if (s_irq_enabled) {
		gpio_intr_disable(WOZ_DW3000_PIN_IRQ);
		s_irq_enabled = false;
	}
}

// Return whether the DW3000 IRQ GPIO interrupt is currently enabled.
bool dw3000_hw_interrupt_is_enabled(void) { return s_irq_enabled; }

// Hardware-reset the DW3000 by pulsing the RESET pin low then releasing it to hi-Z.
// Blocks for about 3 ms total to let the chip climb to IDLE_RC. Clears the asleep flag.
void dw3000_hw_reset(void)
{
	/* Drive reset low (assert), release to hi-Z, let the chip climb to IDLE_RC. */
	gpio_set_direction(WOZ_DW3000_PIN_RST, GPIO_MODE_OUTPUT);
	gpio_set_level(WOZ_DW3000_PIN_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(1));
	gpio_set_direction(WOZ_DW3000_PIN_RST, GPIO_MODE_INPUT);
	vTaskDelay(pdMS_TO_TICKS(2));
	s_asleep = false;
}

// Wake the DW3000 from sleep if marked asleep; no-op otherwise.
// Drives the wake sequence over SPI, waits for the INIT_RC-to-IDLE_RC startup delay, clears the
// asleep flag, then polls dwt_checkidlerc() (up to ~5 ms) and logs an error if the chip never
// reaches IDLE_RC.
void dw3000_hw_wakeup(void)
{
	if (!s_asleep) {
		return;
	}
	ESP_LOGI(TAG, "WAKEUP CS");
	dw3000_spi_wakeup();
	esp_rom_delay_us(2000); /* INIT_RC -> IDLE_RC startup */
	s_asleep = false;

	int spins = 0;
	while (!dwt_checkidlerc() && spins < 500) {
		esp_rom_delay_us(10);
		spins++;
	}
	if (spins >= 500) {
		ESP_LOGE(TAG, "WAKEUP: chip never reached IDLE_RC");
	}
}

// Drive the DW3000 WAKEUP pin low.
void dw3000_hw_wakeup_pin_low(void)
{
	gpio_set_level(WOZ_DW3000_PIN_WAKEUP, 0);
}

// Tear down the DW3000 hardware port: disable the IRQ line if enabled and finalize the SPI bus.
// Safe to call after dw3000_hw_init; leaves s_irq_enabled cleared.
void dw3000_hw_fini(void)
{
	if (s_irq_enabled) {
		gpio_intr_disable(WOZ_DW3000_PIN_IRQ);
		s_irq_enabled = false;
	}
	dw3000_spi_fini();
}
