// meshtastic_frame.c — implementation. Uses ESP-IDF's mbedtls AES for
// AES-128-CTR on target; on host builds AES is a no-op stub so the
// scene still compiles for the SDL harness (packets won't be decodable
// there, but that's fine — the harness has no radio).

#include "meshtastic_frame.h"

#include <string.h>

#ifdef ESP_PLATFORM
  #include "mbedtls/aes.h"
  #include "esp_mac.h"
#endif

// Default LongFast channel PSK — the AES-128 key every Meshtastic node
// on the primary channel derives from. Sourced from Meshtastic firmware
// (Channels.cpp: defaultpsk[]).
static const uint8_t DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

// XOR-hash of channel name "LongFast" and PSK. Precomputed so the
// header always carries the right one-byte identifier.
//   xorHash("LongFast") = 0x0A
//   xorHash(DEFAULT_PSK) = 0x02
//   channel_hash        = 0x08
#define LONGFAST_CHANNEL_HASH 0x08

// PortNum enum value for text messages (Meshtastic PortNum.proto).
#define PORTNUM_TEXT_MESSAGE_APP 1

// 16-byte header layout.
#define HDR_SIZE         16
#define HDR_TO_OFF        0
#define HDR_FROM_OFF      4
#define HDR_ID_OFF        8
#define HDR_FLAGS_OFF    12
#define HDR_CHAN_OFF     13
#define HDR_NEXTHOP_OFF  14
#define HDR_RELAY_OFF    15

// Flags: hop_limit=3 in bits 0..2, hop_start=3 in bits 5..7 → 0b01100011
#define DEFAULT_FLAGS 0x63

// ---- Node ID ----
static uint32_t s_my_node_id = 0;

uint32_t meshtastic_my_node_id(void)
{
#ifdef ESP_PLATFORM
    if (s_my_node_id == 0) {
        uint8_t mac[8] = { 0 };
        // esp_efuse_mac_get_default returns the factory-programmed
        // 48-bit MAC. Meshtastic uses the lower 32 bits as the node ID.
        esp_efuse_mac_get_default(mac);
        s_my_node_id = ((uint32_t)mac[2] << 24)
                     | ((uint32_t)mac[3] << 16)
                     | ((uint32_t)mac[4] <<  8)
                     |  (uint32_t)mac[5];
        // Meshtastic node ID 0 is reserved — bump to a deterministic
        // fallback so we don't collide with "unset".
        if (s_my_node_id == 0) s_my_node_id = 0xdeadbeefu;
    }
    return s_my_node_id;
#else
    return 0x0acedbadu;  // host-build sentinel
#endif
}

// ---- Protobuf varint ----
static size_t varint_write(uint32_t v, uint8_t *out)
{
    size_t n = 0;
    while (v >= 0x80) {
        out[n++] = (uint8_t)(v & 0x7F) | 0x80;
        v >>= 7;
    }
    out[n++] = (uint8_t)(v & 0x7F);
    return n;
}

static size_t varint_read(const uint8_t *in, size_t len, uint32_t *v)
{
    uint32_t val = 0;
    size_t n = 0;
    int shift = 0;
    while (n < len && n < 5) {
        uint8_t b = in[n++];
        val |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *v = val; return n; }
        shift += 7;
    }
    return 0;
}

// ---- Data protobuf: {portnum=1, payload=text} ----
static size_t encode_data_text(const char *text, uint8_t *out, size_t out_max)
{
    size_t text_len = strlen(text);
    if (text_len > 200) text_len = 200;   // Meshtastic max text length
    size_t n = 0;
    // Field 1 (portnum), wire varint
    if (n + 2 > out_max) return 0;
    out[n++] = 0x08;
    out[n++] = PORTNUM_TEXT_MESSAGE_APP;
    // Field 2 (payload), wire length-delimited
    if (n + 1 > out_max) return 0;
    out[n++] = 0x12;
    if (n + 5 > out_max) return 0;
    n += varint_write((uint32_t)text_len, &out[n]);
    if (n + text_len > out_max) return 0;
    memcpy(&out[n], text, text_len);
    n += text_len;
    return n;
}

static bool decode_data_text(const uint8_t *in, size_t in_len,
                             char *out, size_t out_max)
{
    if (out_max == 0) return false;
    size_t i = 0;
    bool have_text = false;
    bool have_text_portnum = false;
    out[0] = 0;
    while (i < in_len) {
        uint8_t tag = in[i++];
        uint8_t field = tag >> 3;
        uint8_t wire = tag & 0x07;
        if (field == 1 && wire == 0) {
            uint32_t v;
            size_t k = varint_read(&in[i], in_len - i, &v);
            if (!k) return false;
            i += k;
            if (v == PORTNUM_TEXT_MESSAGE_APP) have_text_portnum = true;
        } else if (field == 2 && wire == 2) {
            uint32_t plen;
            size_t k = varint_read(&in[i], in_len - i, &plen);
            if (!k || i + k + plen > in_len) return false;
            i += k;
            size_t copy = plen < out_max - 1 ? plen : out_max - 1;
            memcpy(out, &in[i], copy);
            out[copy] = 0;
            i += plen;
            have_text = true;
        } else {
            // Skip unknown field
            if (wire == 0) {
                uint32_t v;
                size_t k = varint_read(&in[i], in_len - i, &v);
                if (!k) return false;
                i += k;
            } else if (wire == 2) {
                uint32_t plen;
                size_t k = varint_read(&in[i], in_len - i, &plen);
                if (!k || i + k + plen > in_len) return false;
                i += k + plen;
            } else if (wire == 5) {
                if (i + 4 > in_len) return false;
                i += 4;
            } else if (wire == 1) {
                if (i + 8 > in_len) return false;
                i += 8;
            } else return false;
        }
    }
    return have_text_portnum && have_text;
}

// ---- AES-CTR nonce ----
static void make_nonce(uint32_t packet_id, uint32_t from_node, uint8_t nonce[16])
{
    memset(nonce, 0, 16);
    // Meshtastic radio nonce: packet_id LE then from_node LE, rest zero.
    // The AES-CTR block counter increments the tail-8 bytes.
    nonce[0] = (uint8_t)(packet_id);
    nonce[1] = (uint8_t)(packet_id >> 8);
    nonce[2] = (uint8_t)(packet_id >> 16);
    nonce[3] = (uint8_t)(packet_id >> 24);
    nonce[4] = (uint8_t)(from_node);
    nonce[5] = (uint8_t)(from_node >> 8);
    nonce[6] = (uint8_t)(from_node >> 16);
    nonce[7] = (uint8_t)(from_node >> 24);
}

#ifdef ESP_PLATFORM
static void aes_ctr_apply(const uint8_t *key,
                          uint8_t nonce[16],
                          uint8_t *data, size_t len)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    size_t nc_off = 0;
    uint8_t stream_block[16];
    mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce, stream_block, data, data);
    mbedtls_aes_free(&ctx);
}
#endif

// ---- Public API ----
size_t meshtastic_build_text(const char *text, uint32_t packet_id,
                             uint8_t *out, size_t out_max)
{
    if (!text || !out || out_max < HDR_SIZE + 8) return 0;
    uint32_t from = meshtastic_my_node_id();
    uint32_t to   = MESHTASTIC_BROADCAST;

    // Header
    out[HDR_TO_OFF+0]     = (uint8_t)(to);
    out[HDR_TO_OFF+1]     = (uint8_t)(to >> 8);
    out[HDR_TO_OFF+2]     = (uint8_t)(to >> 16);
    out[HDR_TO_OFF+3]     = (uint8_t)(to >> 24);
    out[HDR_FROM_OFF+0]   = (uint8_t)(from);
    out[HDR_FROM_OFF+1]   = (uint8_t)(from >> 8);
    out[HDR_FROM_OFF+2]   = (uint8_t)(from >> 16);
    out[HDR_FROM_OFF+3]   = (uint8_t)(from >> 24);
    out[HDR_ID_OFF+0]     = (uint8_t)(packet_id);
    out[HDR_ID_OFF+1]     = (uint8_t)(packet_id >> 8);
    out[HDR_ID_OFF+2]     = (uint8_t)(packet_id >> 16);
    out[HDR_ID_OFF+3]     = (uint8_t)(packet_id >> 24);
    out[HDR_FLAGS_OFF]    = DEFAULT_FLAGS;
    out[HDR_CHAN_OFF]     = LONGFAST_CHANNEL_HASH;
    out[HDR_NEXTHOP_OFF]  = 0;
    out[HDR_RELAY_OFF]    = 0;

    // Encode Data protobuf into payload region
    size_t payload_len = encode_data_text(text,
                                          out + HDR_SIZE,
                                          out_max - HDR_SIZE);
    if (payload_len == 0) return 0;

#ifdef ESP_PLATFORM
    uint8_t nonce[16];
    make_nonce(packet_id, from, nonce);
    aes_ctr_apply(DEFAULT_PSK, nonce, out + HDR_SIZE, payload_len);
#endif
    return HDR_SIZE + payload_len;
}

bool meshtastic_parse_text(const uint8_t *in, size_t in_len,
                           char *text_out, size_t text_max,
                           uint32_t *from_node_out)
{
    if (!in || in_len < HDR_SIZE + 4 || !text_out || text_max < 2) return false;
    // Filter to the LongFast channel — reject anything else on the air.
    if (in[HDR_CHAN_OFF] != LONGFAST_CHANNEL_HASH) return false;

    uint32_t packet_id = (uint32_t)in[HDR_ID_OFF+0]
                       | ((uint32_t)in[HDR_ID_OFF+1] << 8)
                       | ((uint32_t)in[HDR_ID_OFF+2] << 16)
                       | ((uint32_t)in[HDR_ID_OFF+3] << 24);
    uint32_t from = (uint32_t)in[HDR_FROM_OFF+0]
                  | ((uint32_t)in[HDR_FROM_OFF+1] << 8)
                  | ((uint32_t)in[HDR_FROM_OFF+2] << 16)
                  | ((uint32_t)in[HDR_FROM_OFF+3] << 24);
    size_t payload_len = in_len - HDR_SIZE;
    if (payload_len > MESHTASTIC_MAX_FRAME) return false;

    uint8_t buf[MESHTASTIC_MAX_FRAME];
    memcpy(buf, in + HDR_SIZE, payload_len);

#ifdef ESP_PLATFORM
    uint8_t nonce[16];
    make_nonce(packet_id, from, nonce);
    aes_ctr_apply(DEFAULT_PSK, nonce, buf, payload_len);
#endif

    if (!decode_data_text(buf, payload_len, text_out, text_max)) return false;
    if (from_node_out) *from_node_out = from;
    return true;
}
