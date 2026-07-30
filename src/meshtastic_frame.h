// meshtastic_frame.h — Meshtastic v2 on-air packet builder / parser.
//
// Wraps text messages in the same LoRa frame that Meshtastic firmware
// emits on the primary "LongFast" channel so other Meshtastic devices
// on the US 906.875 MHz LongFast preset receive our broadcasts and
// display them normally.
//
// Frame layout (bytes on air):
//   uint32 to        LE, 0xFFFFFFFF = broadcast
//   uint32 from      LE, this node's ID (derived from MAC)
//   uint32 id        LE, packet ID (random)
//   uint8  flags     hop_limit (bits 0..2) | hop_start (bits 5..7)
//   uint8  channel   XOR-hash of "LongFast" || defaultpsk = 0x08
//   uint8  next_hop  0 for broadcast
//   uint8  relay     0
//   ---- 16-byte header ends ----
//   [AES-128-CTR encrypted Data protobuf follows]
//
// Data protobuf (minimal for text broadcast):
//   field 1 (portnum, varint) = 1 (TEXT_MESSAGE_APP)
//   field 2 (payload, bytes)  = UTF-8 text
//
// AES key = 16-byte default LongFast PSK. Nonce = packet_id || from_node
// || zeros, matching Meshtastic firmware's radio nonce derivation.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESHTASTIC_MAX_FRAME  240
#define MESHTASTIC_BROADCAST  0xFFFFFFFFu

// Derived from ESP-S3 MAC (lower 32 bits of 48-bit factory MAC). Stable
// per badge. Cached after first call. On host builds returns a fixed
// sentinel so the harness doesn't blow up.
uint32_t meshtastic_my_node_id(void);

// Encode + encrypt a text broadcast into `out`. `packet_id` should be a
// per-message random value (uniqueness helps Meshtastic firmwares avoid
// re-processing dupes). Returns bytes written to `out`, or 0 on error.
size_t meshtastic_build_text(const char *text, uint32_t packet_id,
                             uint8_t *out, size_t out_max);

// Try to decode an incoming raw frame as a Meshtastic text message on
// the default LongFast channel. On success, writes the extracted UTF-8
// into text_out (NUL-terminated) and returns true.
// `from_node_out` is optional; if non-null, receives the source node ID.
bool meshtastic_parse_text(const uint8_t *in, size_t in_len,
                           char *text_out, size_t text_max,
                           uint32_t *from_node_out);

// Parse only the 16-byte Meshtastic header — no decryption, no payload
// inspection. Useful for a node scanner: any well-formed frame on air
// gives us its source node ID + channel hash regardless of which
// channel PSK the sender used. Returns true if in_len looks plausible.
// All output params are optional.
bool meshtastic_parse_header(const uint8_t *in, size_t in_len,
                             uint32_t *to_out,
                             uint32_t *from_out,
                             uint32_t *id_out,
                             uint8_t  *channel_hash_out,
                             uint8_t  *flags_out);

// Channel hash byte we broadcast under (LongFast default). Callers can
// use this to tell whether a heard packet is on our channel.
#define MESHTASTIC_LONGFAST_CHANNEL_HASH 0x08

// Build a NODEINFO_APP announcement frame. Meshtastic apps hide text
// messages from senders they don't have a User record for, so we need
// to introduce ourselves once when the scene loads. Contents:
//   User { id="!<hex>", long_name, short_name, hw_model=255/PRIVATE }
// wrapped in Data { portnum=NODEINFO_APP=4, payload=<user_bytes>,
// want_response=true }, wrapped in the standard MeshPacket header,
// encrypted with the LongFast key.
// `long_name` and `short_name` are truncated to Meshtastic's field
// limits (39 / 4 chars respectively). Returns bytes written or 0.
size_t meshtastic_build_nodeinfo(const char *long_name,
                                 const char *short_name,
                                 uint32_t packet_id,
                                 uint8_t *out, size_t out_max);
