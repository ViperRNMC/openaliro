/* ESP-IDF SPI backend for the DW3000 decadriver — implements dw3000_spi.h.
 * Replaces the Zephyr deps/dw3000/platform/dw3000_spi.c (not compiled here).
 *
 * CS is a plain GPIO (spics_io_num = -1), matching the Zephyr cs-gpios model, so
 * the wakeup path can hold CS low ~500us. Each DW3000 command is one CS-low
 * full-duplex transfer: header + body assembled in a DMA-capable, word-aligned
 * bounce buffer; on reads the body slice of the RX buffer is copied back. */
#include "dw3000_spi.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *const TAG = "dw3000_spi";

static spi_device_handle_t s_dev_slow;
static spi_device_handle_t s_dev_fast;
static spi_device_handle_t s_cur;
static SemaphoreHandle_t s_lock;

/* Max DW3000 single SPI transfer: ~1023 B frame + a few header bytes. */
#define DW_XFER_MAX 2048
/* Non-DMA SPI bursts cap at the CPU data registers (64 B on S3). */
#define DW_XFER_CHUNK SOC_SPI_MAXIMUM_BUFFER_SIZE
static WORD_ALIGNED_ATTR DRAM_ATTR uint8_t s_txbuf[DW_XFER_MAX];
static WORD_ALIGNED_ATTR DRAM_ATTR uint8_t s_rxbuf[DW_XFER_MAX];

// Bring up the DW3000 SPI bus and CS GPIO for this port.
// Idempotent: returns 0 immediately if already initialized. Configures CS as an
// active-low output (idle high), initializes the SPI bus with DMA disabled
// (transfers <=64 B use CPU data registers directly; larger ones are chunked by
// dw_xfer), and adds both a slow-clock and fast-clock device handle on the bus.
// Starts the active device at slow speed. Only the DW3000 sits on this bus.
// Returns 0 on success, -1 if bus init or device add fails.
int dw3000_spi_init(void)
{
	if (s_dev_fast != NULL) {
		return 0; /* already up: re-adding devices would leak the old handles */
	}
	if (s_lock == NULL) {
		s_lock = xSemaphoreCreateMutex();
	}

	/* CS as GPIO, idle high (DW3000 CS is active low). */
	gpio_config_t cs = {
		.pin_bit_mask = 1ULL << WOZ_DW3000_PIN_CS,
		.mode = GPIO_MODE_OUTPUT,
	};
	gpio_config(&cs);
	gpio_set_level(WOZ_DW3000_PIN_CS, 1);

	spi_bus_config_t bus = {
		.mosi_io_num = WOZ_DW3000_PIN_MOSI,
		.miso_io_num = WOZ_DW3000_PIN_MISO,
		.sclk_io_num = WOZ_DW3000_PIN_SCLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = DW_XFER_MAX,
	};
	/* DMA-disabled: the ~50us/transaction DMA-descriptor + cache-msync path dwarfs
	 * bit-time for the small register/STS writes on the ranging arm critical path.
	 * Without DMA, <=64 B transfers use the CPU data registers directly (~10us).
	 * dw_xfer chunks anything larger into 64 B bursts. Only the DW3000 is on this
	 * bus (status LED is on RMT), so this is safe. */
	esp_err_t e = spi_bus_initialize(WOZ_DW3000_SPI_HOST, &bus, SPI_DMA_DISABLED);
	if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "spi_bus_initialize failed: %d", e);
		return -1;
	}

	spi_device_interface_config_t dev = {
		.mode = 0, /* DW3000: SPI mode 0 (CPOL=0, CPHA=0) */
		.spics_io_num = -1, /* manual CS via GPIO */
		.queue_size = 1,
		.clock_speed_hz = WOZ_DW3000_SPI_SLOW_HZ,
	};
	if (spi_bus_add_device(WOZ_DW3000_SPI_HOST, &dev, &s_dev_slow) != ESP_OK) {
		ESP_LOGE(TAG, "add slow device failed");
		return -1;
	}
	dev.clock_speed_hz = WOZ_DW3000_SPI_FAST_HZ;
	if (spi_bus_add_device(WOZ_DW3000_SPI_HOST, &dev, &s_dev_fast) != ESP_OK) {
		ESP_LOGE(TAG, "add fast device failed");
		return -1;
	}
	s_cur = s_dev_slow;
	ESP_LOGI(TAG, "DW3000 SPI up (slow %d Hz / fast %d Hz)",
		 WOZ_DW3000_SPI_SLOW_HZ, WOZ_DW3000_SPI_FAST_HZ);
	return 0;
}

// Switch subsequent DW3000 SPI transfers to the slow-clock device handle.
void dw3000_spi_speed_slow(void) { s_cur = s_dev_slow; }
// Switch subsequent DW3000 SPI transfers to the fast-clock device handle.
void dw3000_spi_speed_fast(void) { s_cur = s_dev_fast; }

// Tears down the DW3000 SPI bus: removes the slow and fast SPI device handles if present, then frees the SPI bus on WOZ_DW3000_SPI_HOST.
// Safe to call when devices were never added (each handle is checked for non-NULL before removal).
void dw3000_spi_fini(void)
{
	if (s_dev_slow) {
		spi_bus_remove_device(s_dev_slow);
		s_dev_slow = NULL;
	}
	if (s_dev_fast) {
		spi_bus_remove_device(s_dev_fast);
		s_dev_fast = NULL;
	}
	spi_bus_free(WOZ_DW3000_SPI_HOST);
}

/* One CS-low command: [header][body|zeros][crc?]; capture RX body slice when
 * rx_body != NULL. */
static int32_t dw_xfer(const uint8_t *hdr, uint16_t hlen, const uint8_t *body,
		       uint16_t blen, uint8_t *rx_body, const uint8_t *crc)
{
	size_t total = (size_t)hlen + blen + (crc ? 1u : 0u);

	if (total == 0 || total > DW_XFER_MAX) {
		return -1;
	}

	xSemaphoreTake(s_lock, portMAX_DELAY);

	memcpy(s_txbuf, hdr, hlen);
	if (body && blen) {
		memcpy(s_txbuf + hlen, body, blen);
	} else if (blen) {
		memset(s_txbuf + hlen, 0, blen);
	}
	if (crc) {
		s_txbuf[hlen + blen] = *crc;
	}

	/* Non-DMA transfers cap at DW_XFER_CHUNK (64 B), so split longer DW3000
	 * transactions into <=64 B bursts inside one CS-low window; the chip streams
	 * sequentially so a split read/write is seamless. Anything <=64 B (every
	 * register/STS write on the arm critical path) is a single burst, unchanged. */
	gpio_set_level(WOZ_DW3000_PIN_CS, 0);
	esp_err_t e = ESP_OK;
	for (size_t off = 0; off < total && e == ESP_OK; off += DW_XFER_CHUNK) {
		size_t n = total - off;
		spi_transaction_t t = {0};

		if (n > DW_XFER_CHUNK) {
			n = DW_XFER_CHUNK;
		}
		t.length = n * 8u;
		t.tx_buffer = s_txbuf + off;
		t.rx_buffer = rx_body ? (s_rxbuf + off) : NULL;
		e = spi_device_polling_transmit(s_cur, &t);
	}
	gpio_set_level(WOZ_DW3000_PIN_CS, 1);

	if (e == ESP_OK && rx_body && blen) {
		memcpy(rx_body, s_rxbuf + hlen, blen);
	}

	xSemaphoreGive(s_lock);
	return (e == ESP_OK) ? 0 : -1;
}

// Read from the DW3000 over SPI: sends a header then clocks in readLength bytes.
// Thin wrapper over dw_xfer with no body write and no CRC. Returns whatever
// dw_xfer returns.
int32_t dw3000_spi_read(uint16_t headerLength, uint8_t *headerBuffer,
			uint16_t readLength, uint8_t *readBuffer)
{
	return dw_xfer(headerBuffer, headerLength, NULL, readLength, readBuffer,
		       NULL);
}

// Write to the DW3000 over SPI: sends a header followed by a body, no CRC byte.
// Thin wrapper over dw_xfer with no read capture. Returns whatever dw_xfer
// returns.
int32_t dw3000_spi_write(uint16_t headerLength, const uint8_t *headerBuffer,
			 uint16_t bodyLength, const uint8_t *bodyBuffer)
{
	return dw_xfer(headerBuffer, headerLength, bodyBuffer, bodyLength, NULL,
		       NULL);
}

// Write to the DW3000 over SPI with a trailing CRC8 byte appended after the body.
// Thin wrapper over dw_xfer with no read capture. Returns whatever dw_xfer
// returns.
int32_t dw3000_spi_write_crc(uint16_t headerLength, const uint8_t *headerBuffer,
			     uint16_t bodyLength, const uint8_t *bodyBuffer,
			     uint8_t crc8)
{
	return dw_xfer(headerBuffer, headerLength, bodyBuffer, bodyLength, NULL,
		       &crc8);
}

// Wake the DW3000 from sleep by toggling CS.
// Drives CS low for ~500us then releases it high, per the Qorvo CS-toggle wake
// sequence. Blocks for the duration of the delay.
void dw3000_spi_wakeup(void)
{
	/* CS active low: drive low ~500us to wake, then release (Qorvo CS-toggle). */
	gpio_set_level(WOZ_DW3000_PIN_CS, 0);
	esp_rom_delay_us(500);
	gpio_set_level(WOZ_DW3000_PIN_CS, 1);
}

// No-op: SPI transaction tracing is not implemented in this port.
void dw3000_spi_trace_output(void) { /* SPI trace not built in this port. */ }
