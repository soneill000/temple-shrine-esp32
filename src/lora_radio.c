// lora_radio.c — SX1276/RFM95W driver skeleton for the badge.
//
// STATUS: UNTESTED against hardware. Register addresses and default
// values come from the Semtech SX1276 datasheet + Meshtastic firmware
// (RadioLib SX127x driver). Pin config matches the badge's
// firmware/meshtastic/variant.h. Real hardware validation is required
// — I've written this from documentation, not from a working build.
//
// Bus sharing: the TFT display and the LoRa radio are on the same
// HSPI/SPI2 bus. Existing display code owns the SPI driver; this
// module borrows the bus with a different CS pin. If concurrent
// access ever causes issues, gate around a mutex.
//
// US Meshtastic LongFast defaults (from Meshtastic firmware):
//   frequency = 906.875 MHz  (US default primary)
//   bandwidth = 250 kHz
//   spreading = SF11
//   coding    = 4/8
//   preamble  = 16 symbols
//   sync word = 0x2B (Meshtastic private-use marker)
//   tx power  = +17 dBm (moderate; not lit up ridiculously)

#include "lora_radio.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw.h"
#endif

// ---- Board pins ----
// CS comes from hw.h (PIN_LORA_CS = 48, matches Meshtastic variant.h's
// LORA_CS = PIN_SPI_CS = 48). RESET/DIO0 aren't in hw.h yet — hardcoded
// here to match the Meshtastic build (LORA_RESET=38, LORA_DIO0=21).
#ifdef ESP_PLATFORM
  #define LORA_PIN_CS     PIN_LORA_CS
#endif
#define LORA_PIN_RESET  38
#define LORA_PIN_DIO0   21

// ---- SX127x register addresses (SX1276 datasheet §6.4) ----
#define REG_FIFO                    0x00
#define REG_OP_MODE                 0x01
#define REG_FRF_MSB                 0x06
#define REG_FRF_MID                 0x07
#define REG_FRF_LSB                 0x08
#define REG_PA_CONFIG               0x09
#define REG_LNA                     0x0C
#define REG_FIFO_ADDR_PTR           0x0D
#define REG_FIFO_TX_BASE_ADDR       0x0E
#define REG_FIFO_RX_BASE_ADDR       0x0F
#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS               0x12
#define REG_RX_NB_BYTES             0x13
#define REG_PKT_RSSI_VALUE          0x1A
#define REG_MODEM_CONFIG_1          0x1D
#define REG_MODEM_CONFIG_2          0x1E
#define REG_SYMB_TIMEOUT_LSB        0x1F
#define REG_PREAMBLE_MSB            0x20
#define REG_PREAMBLE_LSB            0x21
#define REG_PAYLOAD_LENGTH          0x22
#define REG_MODEM_CONFIG_3          0x26
#define REG_SYNC_WORD               0x39
#define REG_VERSION                 0x42

// ---- Modes ----
#define MODE_LONG_RANGE_MODE        0x80    // LoRa mode
#define MODE_SLEEP                  0x00
#define MODE_STDBY                  0x01
#define MODE_TX                     0x03
#define MODE_RX_CONTINUOUS          0x05

// ---- IRQ flags ----
#define IRQ_TX_DONE                 0x08
#define IRQ_RX_DONE                 0x40
#define IRQ_PAYLOAD_CRC_ERROR       0x20

#ifdef ESP_PLATFORM
static const char *TAG = "lora";
static spi_device_handle_t s_lora_dev;
static bool s_ready = false;
static const char *s_status = "not initialised";

static void lora_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), val };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    spi_device_transmit(s_lora_dev, &t);
}

static uint8_t lora_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(s_lora_dev, &t);
    return rx[1];
}

static void lora_write_fifo(const uint8_t *buf, size_t len)
{
    // Sequential writes through the FIFO register.
    for (size_t i = 0; i < len; i++) lora_write_reg(REG_FIFO, buf[i]);
}

static void lora_read_fifo(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) buf[i] = lora_read_reg(REG_FIFO);
}

static void lora_reset(void)
{
    gpio_set_direction(LORA_PIN_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(LORA_PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool lora_radio_init(void)
{
    lora_reset();
    // Attach to the existing SPI2 bus (TFT already inited it).
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = LORA_PIN_CS,
        .queue_size     = 1,
    };
    esp_err_t err = spi_bus_add_device(SPI2_HOST, &cfg, &s_lora_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi add fail: %s", esp_err_to_name(err));
        s_status = "SPI attach failed";
        return false;
    }

    // Version check — SX1276 returns 0x12.
    uint8_t ver = lora_read_reg(REG_VERSION);
    if (ver != 0x12) {
        ESP_LOGW(TAG, "unexpected version 0x%02x (want 0x12)", ver);
        if (ver == 0x00 || ver == 0xFF) s_status = "no chip response";
        else                             s_status = "wrong chip ID";
        return false;
    }

    // Sleep, then switch to LoRa mode.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Frequency: 906.875 MHz (Meshtastic US LongFast primary — slot 20
    // via the default channel-hash algorithm).
    // Fstep = 32MHz / 2^19 = 61.03515625 Hz
    // FRF = 906,875,000 / 61.03515625 = 14,858,240 = 0xE2B800
    lora_write_reg(REG_FRF_MSB, 0xE2);
    lora_write_reg(REG_FRF_MID, 0xB8);
    lora_write_reg(REG_FRF_LSB, 0x00);

    // Modem config 1: BW=250kHz (bits 7..4 = 0x8), CR=4/5 (bits 3..1 =
    // 0x1 → 0x02), explicit header (bit 0 = 0). Meshtastic LongFast
    // uses CR=4/5, not 4/8 — was a bug in v1 of this driver.
    lora_write_reg(REG_MODEM_CONFIG_1, 0x82);
    // SF=11 (0xB0), CRC on (0x04), continuous mode off.
    lora_write_reg(REG_MODEM_CONFIG_2, 0xB4);
    // Low data rate optimize on for SF11@250k (mandatory per datasheet).
    lora_write_reg(REG_MODEM_CONFIG_3, 0x08);

    // Preamble = 16 symbols (Meshtastic uses 16 for LongFast).
    lora_write_reg(REG_PREAMBLE_MSB, 0x00);
    lora_write_reg(REG_PREAMBLE_LSB, 0x10);

    // Meshtastic sync word (private-use, 0x2B).
    lora_write_reg(REG_SYNC_WORD, 0x2B);

    // FIFO base pointers to 0 for both TX and RX (we'll reset ptr each op).
    lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    // PA config: PA_BOOST pin, +17 dBm.
    lora_write_reg(REG_PA_CONFIG, 0x80 | (17 - 2));  // 0x8F

    // LNA boost on.
    lora_write_reg(REG_LNA, 0x23);

    // Put in continuous RX mode by default.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);

    s_ready = true;
    s_status = "ready";
    ESP_LOGI(TAG, "SX1276 up: 906.875 MHz, SF11 BW250 CR4/8, sync 0x2B");
    return true;
}

bool lora_radio_ready(void) { return s_ready; }
const char *lora_radio_status(void) { return s_status; }

bool lora_radio_send(const uint8_t *buf, size_t len)
{
    if (!s_ready || !buf || len == 0 || len > 255) return false;

    // Standby.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    // Reset FIFO ptr.
    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(REG_PAYLOAD_LENGTH, (uint8_t)len);
    lora_write_fifo(buf, len);

    // Kick TX.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

    // Wait for TxDone up to ~3 s.
    for (int i = 0; i < 300; i++) {
        uint8_t flags = lora_read_reg(REG_IRQ_FLAGS);
        if (flags & IRQ_TX_DONE) {
            lora_write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE);   // clear
            // Back to RX.
            lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Timeout — force back to RX.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
    return false;
}

size_t lora_radio_recv(uint8_t *out, size_t max_len, int *rssi_out)
{
    if (!s_ready || !out || max_len == 0) return 0;
    uint8_t flags = lora_read_reg(REG_IRQ_FLAGS);
    if (!(flags & IRQ_RX_DONE)) return 0;

    // Clear the IRQ flag (write-1-to-clear on this chip).
    lora_write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR);

    if (flags & IRQ_PAYLOAD_CRC_ERROR) return 0;

    uint8_t nb = lora_read_reg(REG_RX_NB_BYTES);
    uint8_t addr = lora_read_reg(REG_FIFO_RX_CURRENT_ADDR);
    lora_write_reg(REG_FIFO_ADDR_PTR, addr);
    size_t n = nb < max_len ? nb : max_len;
    lora_read_fifo(out, n);
    if (rssi_out) *rssi_out = (int)lora_read_reg(REG_PKT_RSSI_VALUE) - 157;
    return n;
}

#else  // non-ESP host build (SDL harness) — stubs so the scene compiles

bool lora_radio_init(void)                              { return false; }
bool lora_radio_ready(void)                             { return false; }
const char *lora_radio_status(void)                     { return "host build (no radio)"; }
bool lora_radio_send(const uint8_t *buf, size_t len)    { (void)buf; (void)len; return false; }
size_t lora_radio_recv(uint8_t *out, size_t max_len, int *rssi_out)
{
    (void)out; (void)max_len; if (rssi_out) *rssi_out = -128; return 0;
}

#endif
