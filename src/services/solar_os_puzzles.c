#include "solar_os_puzzles.h"
#include <string.h>
static uint32_t random_next(uint32_t *s) {
  *s = *s ? *s : 0x9e3779b9;
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return *s;
}
static unsigned allowed(const uint8_t cells[81], unsigned index) {
  unsigned row = index / 9, col = index % 9, mask = 0;
  for (unsigned n = 0; n < 9; n++) {
    if (cells[row * 9 + n])
      mask |= 1U << cells[row * 9 + n];
    if (cells[n * 9 + col])
      mask |= 1U << cells[n * 9 + col];
    unsigned r = row / 3 * 3 + n / 3, c = col / 3 * 3 + n % 3;
    if (cells[r * 9 + c])
      mask |= 1U << cells[r * 9 + c];
  }
  return (~mask) & 0x3fe;
}
unsigned solar_sudoku_solutions(uint8_t cells[81], unsigned limit) {
  unsigned best = 81, choices = 0, best_count = 10;
  if (!limit)
    return 0;
  for (unsigned i = 0; i < 81; i++)
    if (cells[i] > 9)
      return 0;
  for (unsigned i = 0; i < 81; i++)
    if (!cells[i]) {
      unsigned mask = allowed(cells, i), count = 0;
      for (unsigned n = 1; n <= 9; n++)
        if (mask & (1U << n))
          count++;
      if (!count)
        return 0;
      if (count < best_count) {
        best = i;
        choices = mask;
        best_count = count;
        if (count == 1)
          break;
      }
    }
  if (best == 81) {
    for (unsigned i = 0; i < 81; i++) {
      uint8_t value = cells[i];
      if (value < 1 || value > 9)
        return 0;
      cells[i] = 0;
      bool valid = allowed(cells, i) & (1U << value);
      cells[i] = value;
      if (!valid)
        return 0;
    }
    return 1;
  }
  unsigned found = 0;
  for (unsigned n = 1; n <= 9 && found < limit; n++)
    if (choices & (1U << n)) {
      cells[best] = n;
      found += solar_sudoku_solutions(cells, limit - found);
      cells[best] = 0;
    }
  return found;
}
static void shuffle(uint8_t *p, unsigned n, uint32_t *s) {
  for (unsigned i = n; i > 1; i--) {
    unsigned j = random_next(s) % i;
    uint8_t a = p[i - 1];
    p[i - 1] = p[j];
    p[j] = a;
  }
}
void solar_sudoku_new(solar_sudoku_t *s, uint32_t seed) {
  memset(s, 0, sizeof(*s));
  uint8_t digits[] = {1, 2, 3, 4, 5, 6, 7, 8, 9}, bands[] = {0, 1, 2},
          stacks[] = {0, 1, 2}, rows[9], cols[9];
  shuffle(digits, 9, &seed);
  shuffle(bands, 3, &seed);
  shuffle(stacks, 3, &seed);
  for (unsigned i = 0; i < 3; i++) {
    uint8_t a[] = {0, 1, 2}, b[] = {0, 1, 2};
    shuffle(a, 3, &seed);
    shuffle(b, 3, &seed);
    for (unsigned j = 0; j < 3; j++) {
      rows[i * 3 + j] = bands[i] * 3 + a[j];
      cols[i * 3 + j] = stacks[i] * 3 + b[j];
    }
  }
  for (unsigned r = 0; r < 9; r++)
    for (unsigned c = 0; c < 9; c++)
      s->solution[r * 9 + c] =
          digits[(rows[r] * 3 + rows[r] / 3 + cols[c]) % 9];
  memcpy(s->value, s->solution, 81);
  uint8_t order[81];
  for (unsigned i = 0; i < 81; i++)
    order[i] = i;
  shuffle(order, 81, &seed);
  unsigned removed = 0;
  for (unsigned i = 0; i < 81 && removed < 45; i++) {
    unsigned at = order[i];
    uint8_t old = s->value[at];
    s->value[at] = 0;
    uint8_t test[81];
    memcpy(test, s->value, 81);
    if (solar_sudoku_solutions(test, 2) != 1)
      s->value[at] = old;
    else
      removed++;
  }
  for (unsigned i = 0; i < 81; i++)
    s->given[i] = !!s->value[i];
}
bool solar_sudoku_set(solar_sudoku_t *s, unsigned index, unsigned value) {
  if (index >= 81 || value > 9 || s->given[index])
    return false;
  s->value[index] = value;
  return true;
}
bool solar_sudoku_conflict(const solar_sudoku_t *s, unsigned index) {
  if (index >= 81 || !s->value[index])
    return false;
  uint8_t copy[81];
  memcpy(copy, s->value, 81);
  unsigned value = copy[index];
  copy[index] = 0;
  return value > 9 || !(allowed(copy, index) & (1U << value));
}
bool solar_sudoku_solved(const solar_sudoku_t *s) {
  for (unsigned i = 0; i < 81; i++)
    if (!s->value[i] || solar_sudoku_conflict(s, i))
      return false;
  return true;
}
void solar_mines_new(solar_mines_t *s, uint32_t seed) {
  memset(s, 0, sizeof(*s));
  s->seed = seed;
}
static bool neighbor(unsigned a, unsigned b) {
  int dx = (int)(a % 9) - (int)(b % 9), dy = (int)(a / 9) - (int)(b / 9);
  return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}
static void mines_place(solar_mines_t *s, unsigned safe) {
  unsigned order[81], count = 0;
  for (unsigned i = 0; i < 81; i++)
    if (!neighbor(i, safe))
      order[count++] = i;
  for (unsigned n = 0; n < 10; n++) {
    unsigned j = n + random_next(&s->seed) % (count - n);
    unsigned v = order[n];
    order[n] = order[j];
    order[j] = v;
    s->bomb[order[n]] = 1;
  }
  for (unsigned i = 0; i < 81; i++)
    for (unsigned j = 0; j < 81; j++)
      if (i != j && neighbor(i, j) && s->bomb[j])
        s->around[i]++;
  s->started = true;
}
static void flood(solar_mines_t *s, unsigned at) {
  uint8_t queue[81];
  unsigned head = 0, count = 0;
  if (s->open[at] || s->flag[at])
    return;
  s->open[at] = 1;
  queue[count++] = at;
  while (head < count) {
    unsigned cell = queue[head++];
    if (s->bomb[cell]) {
      s->lost = true;
      continue;
    }
    if (s->around[cell])
      continue;
    for (unsigned j = 0; j < 81; j++)
      if (neighbor(cell, j) && !s->open[j] && !s->flag[j]) {
        s->open[j] = 1;
        queue[count++] = j;
      }
  }
}
bool solar_mines_reveal(solar_mines_t *s, unsigned at) {
  if (at >= 81 || s->lost || s->won || s->flag[at])
    return false;
  if (!s->started)
    mines_place(s, at);
  if (s->open[at]) {
    unsigned flags = 0;
    for (unsigned j = 0; j < 81; j++)
      if (j != at && neighbor(at, j) && s->flag[j])
        flags++;
    if (flags != s->around[at])
      return false;
    for (unsigned j = 0; j < 81; j++)
      if (j != at && neighbor(at, j))
        flood(s, j);
  } else
    flood(s, at);
  unsigned safe = 0;
  for (unsigned i = 0; i < 81; i++)
    if (s->open[i] && !s->bomb[i])
      safe++;
  s->won = !s->lost && safe == 71;
  return true;
}
bool solar_mines_flag(solar_mines_t *s, unsigned at) {
  if (at >= 81 || s->open[at] || s->lost || s->won)
    return false;
  s->flag[at] = !s->flag[at];
  return true;
}
