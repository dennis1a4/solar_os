#pragma once
#include "../vendor/opendnp3/dnp3.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DNP_LAB_POINTS 32
#define DNP_LAB_RULES 16
#define DNP_LAB_HISTORY 128
#define DNP_LAB_QUEUE 32
#define DNP_LAB_RX 4096
/* A is master-facing, B is outstation-facing. Direction 0=A->B, 1=B->A. */
typedef enum {
  DNP_LAB_BRIDGE,
  DNP_LAB_MASTER,
  DNP_LAB_OUTSTATION,
  DNP_LAB_DEMO
} dnp_lab_mode_t;
typedef enum {
  DNP_LAB_SERIAL,
  DNP_LAB_LISTEN,
  DNP_LAB_CONNECT
} dnp_lab_transport_t;
typedef struct {
  dnp_lab_transport_t kind;
  char host[128];
  uint16_t port, format;
  uint32_t baud;
  uint8_t slot;
} dnp_lab_endpoint_config_t;
typedef enum {
  DNP_LAB_LINK_SRC,
  DNP_LAB_LINK_DST,
  DNP_LAB_CONTROL_INDEX,
  DNP_LAB_BINARY,
  DNP_LAB_ANALOG
} dnp_lab_rule_kind_t;
typedef struct {
  bool enabled;
  uint8_t direction;
  dnp_lab_rule_kind_t kind;
  uint16_t index, replacement;
  double value;
  int flags;
  uint32_t hits, skipped;
} dnp_lab_rule_t;
typedef struct {
  uint32_t drop_every, duplicate_every, crc_every, truncate_every, flip_every,
      delay_ms;
  uint16_t truncate_bytes, flip_offset;
  uint8_t flip_mask;
} dnp_lab_fault_t;
typedef struct {
  dnp_lab_mode_t mode;
  dnp_lab_endpoint_config_t endpoint[2];
  uint16_t master, outstation;
  uint32_t interval_ms, timeout_ms;
  unsigned points;
  dnp_lab_rule_t rules[DNP_LAB_RULES];
  dnp_lab_fault_t faults[2];
} dnp_lab_config_t;
typedef struct {
  void *lab;
  unsigned direction;
} dnp_lab_observer_t;
typedef struct {
  uint64_t sequence;
  uint32_t at;
  uint8_t direction;
  char stage[8], text[176];
  uint16_t length;
  uint8_t data[DNP3_FRAME_MAX];
} dnp_lab_event_t;
typedef struct {
  uint32_t rx, tx, frames, changed, dropped, duplicated, corrupted, skipped,
      backpressure;
} dnp_lab_stats_t;
typedef struct {
  uint8_t binary, flags, binary_output, output_flags;
  int32_t analog, analog_output;
  uint8_t analog_flags, analog_output_flags;
} dnp_lab_point_t;
typedef struct {
  uint16_t length, offset;
  uint32_t due;
  uint8_t bytes[DNP3_FRAME_MAX];
} dnp_lab_packet_t;
typedef struct {
  uint8_t rx[DNP_LAB_RX];
  size_t used;
  uint32_t touched;
  dnp_lab_packet_t queue[DNP_LAB_QUEUE];
  unsigned head, count;
  dnp3_decoder_t decode;
} dnp_lab_direction_t;
typedef struct {
  dnp_lab_config_t config;
  dnp_lab_direction_t direction[2];
  dnp_lab_stats_t stats[2];
  dnp_lab_event_t history[DNP_LAB_HISTORY];
  uint64_t events;
  dnp_lab_point_t points[DNP_LAB_POINTS], master_points[DNP_LAB_POINTS];
  uint32_t now, last_poll, request_at, select_at;
  uint8_t transport_sequence, application_sequence, expected_sequence,
      expected_function;
  bool waiting, selected, pending_operate, control_analog, control_direct;
  uint16_t selected_index, control_index;
  int32_t selected_value, control_value;
  unsigned selected_kind;
  uint32_t responses, timeouts, controls, errors;
  bool have_rx_fcb;
  uint8_t rx_fcb;
  struct {
    uint8_t data[DNP3_FRAGMENT_MAX], expected;
    uint16_t used;
    bool active;
  } assembly[2];
  uint8_t last_request[250];
  size_t last_request_length;
  uint8_t remote_request[DNP3_FRAGMENT_MAX], remote_reply[DNP3_FRAGMENT_MAX];
  size_t remote_request_length, remote_reply_length;
  dnp_lab_observer_t observer[2];
} dnp_lab_t;
void dnp_lab_defaults(dnp_lab_config_t *);
void dnp_lab_init(dnp_lab_t *, const dnp_lab_config_t *);
size_t dnp_lab_frame(uint8_t *, size_t, uint8_t, uint16_t, uint16_t,
                     const uint8_t *, size_t);
/* Returns payload length, -1 malformed/CRC error. Empty link payload is legal.
 */
int dnp_lab_unpack(const uint8_t *, size_t, uint8_t *);
size_t dnp_lab_receive_space(const dnp_lab_t *, unsigned);
bool dnp_lab_feed(dnp_lab_t *, unsigned, const uint8_t *, size_t, uint32_t);
void dnp_lab_step(dnp_lab_t *, uint32_t);
const dnp_lab_packet_t *dnp_lab_output(dnp_lab_t *, unsigned, uint32_t);
void dnp_lab_sent(dnp_lab_t *, unsigned, size_t);
void dnp_lab_gap(dnp_lab_t *, unsigned);
bool dnp_lab_poll(dnp_lab_t *);
bool dnp_lab_control(dnp_lab_t *, bool, uint16_t, int32_t, bool);
void dnp_lab_log(dnp_lab_t *, unsigned, const char *, const char *,
                 const uint8_t *, size_t);
/* Pure configuration/action parser. Endpoints/role/addresses require stopped.
 * start/stop/save/load/log are handled by the platform/app, not this function.
 */
bool dnp_lab_command(dnp_lab_t *, const char *, bool, char *, size_t);
