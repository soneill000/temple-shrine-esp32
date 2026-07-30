// meshtastic_frame.c — implementation.
//
// AES-128 is embedded here as a pure-C reference implementation
// (~100 lines) so we don't depend on ESP-IDF's mbedtls path resolving
// — PlatformIO's espidf framework was failing to find mbedtls/aes.h
// even with `REQUIRES mbedtls`. The embedded impl works identically on
// target and on the host harness, is small (adds ~2 KB code), and is
// only used for the LongFast channel key so throughput doesn't matter.
//
// AES-128 reference (public domain, adapted from Tiny AES in C by
// kokke): key expansion + AES_encrypt (single block). Used only in
// CTR mode, so no InvCipher is needed.

#include "meshtastic_frame.h"

#include <string.h>

#ifdef ESP_PLATFORM
  #include "esp_mac.h"
#endif

// ---- Embedded AES-128 (encrypt-only, public domain) ----
#define AES_BLOCKLEN 16
#define AES_KEYLEN   16
#define AES_KEYEXPSIZE 176   // 11 round keys × 16

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static void key_expansion(const uint8_t key[16], uint8_t rk[AES_KEYEXPSIZE])
{
    memcpy(rk, key, 16);
    uint8_t tmp[4];
    for (int i = 4; i < 44; i++) {
        tmp[0] = rk[(i - 1) * 4 + 0];
        tmp[1] = rk[(i - 1) * 4 + 1];
        tmp[2] = rk[(i - 1) * 4 + 2];
        tmp[3] = rk[(i - 1) * 4 + 3];
        if (i % 4 == 0) {
            uint8_t t = tmp[0];
            tmp[0] = sbox[tmp[1]] ^ rcon[i / 4];
            tmp[1] = sbox[tmp[2]];
            tmp[2] = sbox[tmp[3]];
            tmp[3] = sbox[t];
        }
        rk[i * 4 + 0] = rk[(i - 4) * 4 + 0] ^ tmp[0];
        rk[i * 4 + 1] = rk[(i - 4) * 4 + 1] ^ tmp[1];
        rk[i * 4 + 2] = rk[(i - 4) * 4 + 2] ^ tmp[2];
        rk[i * 4 + 3] = rk[(i - 4) * 4 + 3] ^ tmp[3];
    }
}

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static void aes_encrypt_block(const uint8_t rk[AES_KEYEXPSIZE], uint8_t s[16])
{
    // AddRoundKey (round 0)
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];

    for (int round = 1; round <= 10; round++) {
        // SubBytes
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];

        // ShiftRows
        uint8_t t;
        t = s[1];  s[1] = s[5];  s[5]  = s[9];  s[9]  = s[13]; s[13] = t;
        t = s[2];  s[2] = s[10]; s[10] = t;
        t = s[6];  s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7]  = s[3];  s[3]  = t;

        // MixColumns (skip on final round)
        if (round < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t *col = &s[c * 4];
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t t01 = a0 ^ a1, t12 = a1 ^ a2, t23 = a2 ^ a3;
                col[0] ^= t01 ^ xtime(t01) ^ t23;
                col[1] ^= t12 ^ xtime(t12) ^ (a0 ^ a3);
                col[2] ^= t23 ^ xtime(t23) ^ (a0 ^ a1);
                col[3] ^= (a3 ^ a0) ^ xtime(a3 ^ a0) ^ (a1 ^ a2);
            }
        }

        // AddRoundKey
        const uint8_t *round_key = rk + round * 16;
        for (int i = 0; i < 16; i++) s[i] ^= round_key[i];
    }
}

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

// PortNum enum values (Meshtastic PortNums.proto).
#define PORTNUM_TEXT_MESSAGE_APP 1
#define PORTNUM_NODEINFO_APP     4

// HardwareModel enum. PRIVATE_HW = 255 is what non-official boards use
// when they don't want to claim a specific Meshtastic HW variant.
#define HW_MODEL_PRIVATE_HW      255

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

// ---- User protobuf builder ----
// Fields we set for a self-announce:
//   1  id         string    "!<8-hex-lower node ID>"
//   2  long_name  string    e.g. "TempleShrine"
//   3  short_name string    e.g. "TMPL" (4 chars max)
//   5  hw_model   enum      PRIVATE_HW = 255
// All other fields left absent (defaulted).
static size_t encode_user(const char *long_name,
                          const char *short_name,
                          uint32_t node_id,
                          uint8_t *out, size_t out_max)
{
    char id_buf[10];
    snprintf(id_buf, sizeof(id_buf), "!%08lx", (unsigned long)node_id);
    size_t id_len = strlen(id_buf);

    size_t ln_len = long_name ? strlen(long_name) : 0;
    if (ln_len > 39) ln_len = 39;
    size_t sn_len = short_name ? strlen(short_name) : 0;
    if (sn_len > 4)  sn_len = 4;

    size_t n = 0;
    // Field 1: id
    if (n + 2 + id_len > out_max) return 0;
    out[n++] = 0x0A;                             // (1<<3)|2 = length-delim
    out[n++] = (uint8_t)id_len;
    memcpy(&out[n], id_buf, id_len); n += id_len;
    // Field 2: long_name
    if (ln_len > 0) {
        if (n + 2 + ln_len > out_max) return 0;
        out[n++] = 0x12;                         // (2<<3)|2
        out[n++] = (uint8_t)ln_len;
        memcpy(&out[n], long_name, ln_len); n += ln_len;
    }
    // Field 3: short_name
    if (sn_len > 0) {
        if (n + 2 + sn_len > out_max) return 0;
        out[n++] = 0x1A;                         // (3<<3)|2
        out[n++] = (uint8_t)sn_len;
        memcpy(&out[n], short_name, sn_len); n += sn_len;
    }
    // Field 5: hw_model = 255 (PRIVATE_HW), varint
    if (n + 3 > out_max) return 0;
    out[n++] = 0x28;                             // (5<<3)|0 varint
    out[n++] = 0xFF;                             // 255 as varint: 0xFF 0x01
    out[n++] = 0x01;
    return n;
}

// ---- Data protobuf: {portnum, payload, [want_response]} ----
// Generalized encoder — used by both text and nodeinfo.
static size_t encode_data(uint8_t portnum,
                          const uint8_t *payload, size_t payload_len,
                          bool want_response,
                          uint8_t *out, size_t out_max)
{
    size_t n = 0;
    // Field 1 (portnum), varint
    if (n + 2 > out_max) return 0;
    out[n++] = 0x08;
    out[n++] = portnum;
    // Field 2 (payload), length-delimited
    if (n + 1 > out_max) return 0;
    out[n++] = 0x12;
    if (n + 5 > out_max) return 0;
    n += varint_write((uint32_t)payload_len, &out[n]);
    if (n + payload_len > out_max) return 0;
    memcpy(&out[n], payload, payload_len);
    n += payload_len;
    // Field 3 (want_response), varint bool
    if (want_response) {
        if (n + 2 > out_max) return 0;
        out[n++] = 0x18;
        out[n++] = 0x01;
    }
    return n;
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
// Layout matches Meshtastic firmware's CryptoEngine::initNonce exactly:
//
//   bytes  0..7   packet_id as uint64_t little-endian
//                 (upper 4 bytes are always 0 — the header id is 32-bit
//                  but the crypto layer widens it to uint64_t)
//   bytes  8..11  from_node as uint32_t little-endian
//   bytes 12..15  block counter (starts at 0, big-endian increment)
//
// Getting from_node's byte position wrong (used to be at 4..7 here)
// means our AES-CTR keystream doesn't match what other Meshtastic
// devices generate for the same (packet_id, from_node) pair. Every
// transmitted packet looked like noise to them, and vice versa.
static void make_nonce(uint32_t packet_id, uint32_t from_node, uint8_t nonce[16])
{
    memset(nonce, 0, 16);
    // packet_id as uint64_t LE — bytes 0..3 = low 32, bytes 4..7 = 0
    nonce[0] = (uint8_t)(packet_id);
    nonce[1] = (uint8_t)(packet_id >> 8);
    nonce[2] = (uint8_t)(packet_id >> 16);
    nonce[3] = (uint8_t)(packet_id >> 24);
    // from_node as uint32_t LE at offset 8
    nonce[8]  = (uint8_t)(from_node);
    nonce[9]  = (uint8_t)(from_node >> 8);
    nonce[10] = (uint8_t)(from_node >> 16);
    nonce[11] = (uint8_t)(from_node >> 24);
}

// AES-128-CTR: encrypt/decrypt in-place. `nonce` counter is the last 4
// bytes as big-endian integer (Meshtastic firmware convention — nonce
// bytes 12..15 hold the block counter).
static void aes_ctr_apply(const uint8_t *key,
                          uint8_t nonce[16],
                          uint8_t *data, size_t len)
{
    uint8_t rk[AES_KEYEXPSIZE];
    key_expansion(key, rk);
    uint32_t counter = 0;
    uint8_t block[16];
    size_t i = 0;
    while (i < len) {
        memcpy(block, nonce, 12);
        block[12] = (uint8_t)(counter >> 24);
        block[13] = (uint8_t)(counter >> 16);
        block[14] = (uint8_t)(counter >>  8);
        block[15] = (uint8_t)(counter);
        aes_encrypt_block(rk, block);
        size_t n = (len - i) < 16 ? (len - i) : 16;
        for (size_t j = 0; j < n; j++) data[i + j] ^= block[j];
        i += n;
        counter++;
    }
}

// ---- Header writer (shared) ----
static void write_broadcast_header(uint8_t *out, uint32_t from, uint32_t packet_id)
{
    uint32_t to = MESHTASTIC_BROADCAST;
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
}

// ---- Public API ----
size_t meshtastic_build_text(const char *text, uint32_t packet_id,
                             uint8_t *out, size_t out_max)
{
    if (!text || !out || out_max < HDR_SIZE + 8) return 0;
    uint32_t from = meshtastic_my_node_id();
    write_broadcast_header(out, from, packet_id);

    size_t payload_len = encode_data_text(text,
                                          out + HDR_SIZE,
                                          out_max - HDR_SIZE);
    if (payload_len == 0) return 0;

    uint8_t nonce[16];
    make_nonce(packet_id, from, nonce);
    aes_ctr_apply(DEFAULT_PSK, nonce, out + HDR_SIZE, payload_len);
    return HDR_SIZE + payload_len;
}

size_t meshtastic_build_nodeinfo(const char *long_name,
                                 const char *short_name,
                                 uint32_t packet_id,
                                 uint8_t *out, size_t out_max)
{
    if (!out || out_max < HDR_SIZE + 32) return 0;
    uint32_t from = meshtastic_my_node_id();

    write_broadcast_header(out, from, packet_id);

    // Encode User protobuf into a scratch buffer, then wrap it in a
    // Data { portnum=NODEINFO_APP, payload=<user>, want_response=true }
    // and land the Data bytes in the outgoing frame.
    uint8_t user_buf[80];
    size_t user_len = encode_user(long_name, short_name, from,
                                  user_buf, sizeof(user_buf));
    if (user_len == 0) return 0;

    size_t data_len = encode_data(PORTNUM_NODEINFO_APP,
                                  user_buf, user_len,
                                  true,   // want_response — solicit others' NodeInfo
                                  out + HDR_SIZE,
                                  out_max - HDR_SIZE);
    if (data_len == 0) return 0;

    uint8_t nonce[16];
    make_nonce(packet_id, from, nonce);
    aes_ctr_apply(DEFAULT_PSK, nonce, out + HDR_SIZE, data_len);
    return HDR_SIZE + data_len;
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

    uint8_t nonce[16];
    make_nonce(packet_id, from, nonce);
    aes_ctr_apply(DEFAULT_PSK, nonce, buf, payload_len);

    if (!decode_data_text(buf, payload_len, text_out, text_max)) return false;
    if (from_node_out) *from_node_out = from;
    return true;
}

bool meshtastic_parse_header(const uint8_t *in, size_t in_len,
                             uint32_t *to_out,
                             uint32_t *from_out,
                             uint32_t *id_out,
                             uint8_t  *channel_hash_out,
                             uint8_t  *flags_out)
{
    if (!in || in_len < HDR_SIZE) return false;
    if (to_out) {
        *to_out = (uint32_t)in[HDR_TO_OFF+0]
                | ((uint32_t)in[HDR_TO_OFF+1] << 8)
                | ((uint32_t)in[HDR_TO_OFF+2] << 16)
                | ((uint32_t)in[HDR_TO_OFF+3] << 24);
    }
    if (from_out) {
        *from_out = (uint32_t)in[HDR_FROM_OFF+0]
                  | ((uint32_t)in[HDR_FROM_OFF+1] << 8)
                  | ((uint32_t)in[HDR_FROM_OFF+2] << 16)
                  | ((uint32_t)in[HDR_FROM_OFF+3] << 24);
    }
    if (id_out) {
        *id_out = (uint32_t)in[HDR_ID_OFF+0]
                | ((uint32_t)in[HDR_ID_OFF+1] << 8)
                | ((uint32_t)in[HDR_ID_OFF+2] << 16)
                | ((uint32_t)in[HDR_ID_OFF+3] << 24);
    }
    if (channel_hash_out) *channel_hash_out = in[HDR_CHAN_OFF];
    if (flags_out)        *flags_out        = in[HDR_FLAGS_OFF];
    return true;
}
