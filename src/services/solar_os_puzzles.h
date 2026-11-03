#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
  uint8_t value[81], given[81], solution[81];
  uint32_t hints;
} solar_sudoku_t;
typedef struct {
  uint8_t bomb[81], open[81], flag[81], around[81];
  uint32_t seed;
  uint8_t started, lost, won;
} solar_mines_t;
void solar_sudoku_new(solar_sudoku_t *, uint32_t);
bool solar_sudoku_set(solar_sudoku_t *, unsigned, unsigned);
bool solar_sudoku_conflict(const solar_sudoku_t *, unsigned);
bool solar_sudoku_solved(const solar_sudoku_t *);
unsigned solar_sudoku_solutions(uint8_t cells[81], unsigned limit);
void solar_mines_new(solar_mines_t *, uint32_t);
bool solar_mines_reveal(solar_mines_t *, unsigned);
bool solar_mines_flag(solar_mines_t *, unsigned);
