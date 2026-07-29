// lora_radio.h — thin wrapper over the on-badge Semtech SX127x LoRa
// radio (RFM95W). Shares the TFT's SPI bus (CS=48 for TFT, CS=? for
// LoRa — see variant.h; DIO0=21, RESET=38). US 915 MHz Meshtastic
// LongFast defaults: 906.875 MHz, BW=250k, SF=11, CR=4/8, PPP=8,
// sync word=0x2B.
//
// The full driver is in lora_radio.c; this header exposes:
//   - init/deinit
//   - send raw bytes (blocking; caller handles rate limiting)
//   - poll for incoming packet (non-blocking; returns bytes read)
//
// This layer does NOT know about Meshtastic packet framing or
// encryption — that belongs in meshtastic_frame.c.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Init the SX127x radio for US Meshtastic LongFast. Returns true on
// success. Fails silently if the radio ID register doesn't match
// SX1276 — caller can still use the scene UI in "no-radio" mode.
bool lora_radio_init(void);

// Human-readable status from the most recent init attempt. Used by the
// UI to show *why* the radio is offline (no SPI, wrong version, etc.)
// instead of just a blank "OFFLINE". Stable pointer, safe to display.
const char *lora_radio_status(void);

// Are we in receive mode right now? (Used by the scene to show a
// "listening" indicator.)
bool lora_radio_ready(void);

// Broadcast `len` bytes. Blocks until transmission completes (or
// times out at 3 s). Returns true on success. After sending, radio
// is put back into receive mode automatically.
bool lora_radio_send(const uint8_t *buf, size_t len);

// Poll for an incoming packet. If one is available, up to `max_len`
// bytes are copied into `out` and the byte count is returned; else
// returns 0. Non-blocking.
size_t lora_radio_recv(uint8_t *out, size_t max_len, int *rssi_out);
