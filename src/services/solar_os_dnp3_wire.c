#include "solar_os_dnp3_lab.h"
#include <string.h>
static void put16(uint8_t *p, unsigned n) {
  p[0] = n;
  p[1] = n >> 8;
}
size_t dnp_lab_frame(uint8_t *out, size_t capacity, uint8_t control,
                     uint16_t src, uint16_t dst, const uint8_t *payload,
                     size_t length) {
  if (length > 250 || (length && !payload))
    return 0;
  size_t size = 10 + length + 2 * ((length + 15) / 16);
  if (capacity < size)
    return 0;
  out[0] = 5;
  out[1] = 100;
  out[2] = length + 5;
  out[3] = control;
  put16(out + 4, dst);
  put16(out + 6, src);
  put16(out + 8, dnp3_crc(out, 8));
  size_t at = 10;
  for (size_t done = 0; done < length;) {
    size_t n = length - done;
    if (n > 16)
      n = 16;
    memcpy(out + at, payload + done, n);
    put16(out + at + n, dnp3_crc(payload + done, n));
    at += n + 2;
    done += n;
  }
  return size;
}
int dnp_lab_unpack(const uint8_t *frame, size_t size, uint8_t *payload) {
  if (size < 10 || frame[0] != 5 || frame[1] != 100 || frame[2] < 5 ||
      dnp3_crc(frame, 8) != (uint16_t)(frame[8] | (frame[9] << 8)))
    return -1;
  size_t length = frame[2] - 5;
  if (size != 10 + length + 2 * ((length + 15) / 16))
    return -1;
  size_t at = 10;
  for (size_t done = 0; done < length;) {
    size_t n = length - done;
    if (n > 16)
      n = 16;
    if (dnp3_crc(frame + at, n) !=
        (uint16_t)(frame[at + n] | (frame[at + n + 1] << 8)))
      return -1;
    if (payload)
      memcpy(payload + done, frame + at, n);
    done += n;
    at += n + 2;
  }
  return (int)length;
}
