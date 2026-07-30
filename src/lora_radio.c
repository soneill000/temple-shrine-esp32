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
#define REG_OCP                     0x0B
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
// Errata mitigation registers (SX1276/77/78 Errata §2.3, spurious
// response mitigation on BW <= 500 kHz).
#define REG_ERRATA_2F               0x2F
#define REG_ERRATA_30               0x30
#define REG_DETECT_OPTIMIZE         0x31
#define REG_INVERT_IQ               0x33
#define REG_DETECTION_THRESHOLD     0x37
#define REG_MODEM_CONFIG_3          0x26
#define REG_SYNC_WORD               0x39
#define REG_DIO_MAPPING_1           0x40
#define REG_VERSION                 0x42
#define REG_PA_DAC                  0x4D
#define REG_INVERT_IQ2              0x3B

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

    // Enter LoRa mode. Per SX1276 datasheet §4.1: "Access to
    // LongRange (bit 7) is only possible in Sleep mode." After power-
    // on the chip is in FSK STANDBY, so we must transition through
    // FSK SLEEP before setting the LongRange bit — writing 0x80
    // directly from Standby is silently ignored on many parts.
    lora_write_reg(REG_OP_MODE, MODE_SLEEP);                            // FSK sleep
    vTaskDelay(pdMS_TO_TICKS(10));
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);     // LoRa sleep
    vTaskDelay(pdMS_TO_TICKS(10));

    // Frequency: 906.875 MHz (Meshtastic US LongFast primary — slot 20
    // via the default channel-hash algorithm).
    // Fstep = 32MHz / 2^19 = 61.03515625 Hz
    // FRF = 906,875,000 / 61.03515625 = 14,858,240 = 0xE2B800
    lora_write_reg(REG_FRF_MSB, 0xE2);
    lora_write_reg(REG_FRF_MID, 0xB8);
    lora_write_reg(REG_FRF_LSB, 0x00);

    // Modem config 1: BW=250kHz (bits 7..4 = 0x8), CR=4/5 (bits 3..1 =
    // 0x1 → 0x02), explicit header (bit 0 = 0).
    lora_write_reg(REG_MODEM_CONFIG_1, 0x82);
    // Modem config 2: SF=11 (bits 7..4 = 0xB), TX single (bit 3 = 0),
    // CRC on (bit 2 = 1), timeout MSB = 0.
    lora_write_reg(REG_MODEM_CONFIG_2, 0xB4);
    // Modem config 3: LDRO OFF (bit 3 = 0) + AGC auto ON (bit 2 = 1).
    // For BW=250 SF=11, symbol time = 2048/250 = 8.19 ms, well under
    // the 16 ms LDRO threshold — RadioLib leaves LDRO off in this
    // case and so must we, otherwise our on-air symbol structure
    // doesn't match what receivers expect.
    lora_write_reg(REG_MODEM_CONFIG_3, 0x04);

    // DetectOptimize + DetectionThreshold: SX1276 requires these for
    // SF7..SF12 (datasheet §4.1.1). RadioLib uses a bit-level modify
    // that leaves reg 0x31 upper bits at their reset default of 0xC0;
    // the errata pass below then clears bit 7 to 0. Net result is
    // 0x43. Writing that directly here.
    lora_write_reg(REG_DETECT_OPTIMIZE,     0x43);
    lora_write_reg(REG_DETECTION_THRESHOLD, 0x0A);

    // SX1276/77/78 Errata §2.3 — spurious response mitigation. For
    // BW ≥ 62.5 kHz the fix is REG 0x2F = 0x40, REG 0x30 = 0x00, and
    // REG 0x31 bit 7 = 0 (already applied above). Without this the IF
    // filter is misaligned and receivers miss packets even when
    // frequency + modulation are correct. This is very likely the
    // reason our earlier attempts saw nothing on the air.
    lora_write_reg(REG_ERRATA_2F, 0x40);
    lora_write_reg(REG_ERRATA_30, 0x00);

    // Non-inverted IQ (Meshtastic default). RadioLib's invertIQ(false)
    // ends up writing 0x27 to REG_INVERT_IQ (RX_OFF bit 6 = 0 +
    // TX_ON bit 0 = 1 — TX bit is intentionally swapped per RadioLib
    // issue #778) and 0x1D to REG_INVERT_IQ2.
    lora_write_reg(REG_INVERT_IQ,  0x27);
    lora_write_reg(REG_INVERT_IQ2, 0x1D);

    // DIO0 -> RxDone (default 0b00). We poll IRQ flags rather than use
    // the interrupt line, but writing a known mapping is safer than
    // leaving DIO in whatever POR state it landed in.
    lora_write_reg(REG_DIO_MAPPING_1, 0x00);

    // Preamble = 16 symbols (Meshtastic default).
    lora_write_reg(REG_PREAMBLE_MSB, 0x00);
    lora_write_reg(REG_PREAMBLE_LSB, 0x10);

    // Meshtastic sync word (private-use, 0x2B).
    lora_write_reg(REG_SYNC_WORD, 0x2B);

    // FIFO base pointers to 0 for both TX and RX.
    lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    // Hop period off (no frequency hopping).
    lora_write_reg(0x24, 0x00);

    // PA_CONFIG: PA_BOOST pin (bit 7 = 1), MAX_POWER = 0x7 (bits 6..4
    // — this is what RadioLib actually writes; even though MAX_POWER
    // is documented as only affecting RFO, RadioLib sets it for
    // PA_BOOST too and Meshtastic works with it set), OUTPUT_POWER =
    // power - 2 = 15 = 0xF for +17 dBm. Net = 0x80 | 0x70 | 0x0F = 0xFF.
    lora_write_reg(REG_PA_CONFIG, 0xFF);

    // PA_DAC: 0x84 = normal mode (bits 2..0 = 0b100 = PA_BOOST_OFF).
    // 0x87 would be +20 dBm high-power mode — explicitly write 0x84
    // so a stale POR value can't leave us there.
    lora_write_reg(REG_PA_DAC, 0x84);

    // Over-current protection: OCP_ON | raw=3 → 60 mA (RadioLib's
    // default from begin()). Fine for +17 dBm draw.
    lora_write_reg(REG_OCP, 0x23);

    // LNA: gain=G1 (max) in bits 7..5 + LNA_BOOST_ON in bits 1..0.
    lora_write_reg(REG_LNA, 0x23);

    // Enter continuous RX.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(REG_IRQ_FLAGS, 0xFF);   // clear any latched flags

    s_ready = true;
    s_status = "ready";
    ESP_LOGI(TAG, "SX1276 up: 906.875 MHz, SF11 BW250 CR4/5, sync 0x2B (RadioLib-mirror)");
    return true;
}

bool lora_radio_ready(void) { return s_ready; }
const char *lora_radio_status(void) { return s_status; }

bool lora_radio_send(const uint8_t *buf, size_t len)
{
    if (!s_ready || !buf || len == 0 || len > 255) return false;

    // Standby.
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    // Clear all IRQ flags before TX so a stale RxDone from continuous
    // RX doesn't spoof the poll loop.
    lora_write_reg(REG_IRQ_FLAGS, 0xFF);
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
