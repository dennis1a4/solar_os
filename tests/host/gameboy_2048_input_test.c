/* ROM-specific regression for mmuszkow/2048-gb. Supply a built 32 KiB ROM.
 * Starts 12 fresh games and attempts 96 legal moves at five taps/second.
 * Exit 1 means missed moves; exit 2 means invalid setup/core failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ENABLE_SOUND 0
#define PEANUT_GB_12_COLOUR 0
#include "vendor/peanut_gb/peanut_gb.h"
static unsigned char rom[32768];
static uint8_t rr(struct gb_s *g, uint_fast32_t a) {
  (void)g;
  return rom[a % 32768];
}
static uint8_t cr(struct gb_s *g, uint_fast32_t a) {
  (void)g;
  (void)a;
  return 255;
}
static void cw(struct gb_s *g, uint_fast32_t a, uint8_t v) {
  (void)g;
  (void)a;
  (void)v;
}
static void err(struct gb_s *g, enum gb_error_e e, uint16_t a) {
  (void)g;
  fprintf(stderr, "core error %d %x\n", e, a);
  exit(2);
}
static void board(struct gb_s *g, uint8_t *b) {
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++)
      b[y * 4 + x] = g->vram[0x1800 + (2 + 4 * y) * 32 + 3 + 4 * x];
}
static int legal(const uint8_t *b, int direction) {
  const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, -1, 1};
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
      int nx = x + dx[direction], ny = y + dy[direction];
      if (b[y * 4 + x] && nx >= 0 && nx < 4 && ny >= 0 && ny < 4 &&
          (!b[ny * 4 + nx] || b[y * 4 + x] == b[ny * 4 + nx]))
        return 1;
    }
  return 0;
}
static void frames(struct gb_s *g, uint8_t mask, int n) {
  g->direct.joypad = ~mask;
  while (n--)
    gb_run_frame(g);
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f || fread(rom, 1, sizeof(rom), f) != sizeof(rom))
    return 2;
  fclose(f);
  struct gb_s *g = calloc(1, sizeof(*g));
  int missed = 0, total = 0;
  for (int run = 0; run < 12; run++) {
    memset(g, 0, sizeof(*g));
    if (gb_init(g, rr, cr, cw, err, NULL))
      return 2;
    frames(g, 0, 60 + run);
    frames(g, JOYPAD_START, 5);
    frames(g, 0, 60);
    for (int move = 0; move < 8; move++) {
      uint8_t before[16], after[16];
      board(g, before);
      int d = (move + run) % 4;
      for (int tries = 0; tries < 4 && !legal(before, d); tries++)
        d = (d + 1) % 4;
      if (!legal(before, d))
        return 2;
      frames(g, 1u << (4 + d), 5);
      frames(g, 0, 7);
      board(g, after);
      if (!memcmp(before, after, 16)) {
        missed++;
        printf("missed run=%d move=%d direction=%d\n", run, move, d);
      }
      total++;
    }
  }
  printf("%s legal moves=%d accepted=%d missed=%d\n", argv[1], total,
         total - missed, missed);
  free(g);
  return missed ? 1 : 0;
}
