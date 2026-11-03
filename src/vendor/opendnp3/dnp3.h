#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DNP3_FRAME_MAX 292
#define DNP3_FRAGMENT_MAX 2048
#define DNP3_FLOWS 4
typedef void (*dnp3_event_fn)(void *user, const char *text, const uint8_t *frame, size_t length);
typedef struct {
    bool active, have_last;
    uint16_t source, destination, used, last_length;
    uint8_t expected, last_sequence;
    uint8_t data[DNP3_FRAGMENT_MAX], last[250];
    uint32_t touched;
} dnp3_flow_t;
typedef struct {
    uint32_t bytes, frames, header_crc, body_crc, discarded, transport_errors, unsupported, malformed;
} dnp3_stats_t;
typedef struct {
    uint8_t frame[DNP3_FRAME_MAX];
    size_t used;
    dnp3_flow_t flows[DNP3_FLOWS];
    dnp3_stats_t stats;
    dnp3_event_fn event;
    void *user;
    uint32_t now;
} dnp3_decoder_t;
uint16_t dnp3_crc(const uint8_t *data, size_t length);
const char *dnp3_function_name(uint8_t code);
void dnp3_init(dnp3_decoder_t *decoder, dnp3_event_fn event, void *user);
void dnp3_feed(dnp3_decoder_t *decoder, const uint8_t *data, size_t length, uint32_t now_ms);
/* Expire partial frames/fragments on a stream gap, EOF or connection change. */
void dnp3_gap(dnp3_decoder_t *decoder);
void dnp3_expire(dnp3_decoder_t *decoder, uint32_t now_ms, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
