#include "solar_os_puzzle_apps.h"
#include "esp_timer.h"
#include "solar_os_keys.h"
#include "solar_os_memory.h"
#include "solar_os_organizer.h"
#include "solar_os_organizer_model.h"
#include "solar_os_puzzles.h"
#include "solar_os_storage.h"
#include "solar_os_tui_widgets.h"
#include "solar_os_writer_files.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {
  uint32_t magic, elapsed;
  uint8_t cursor, kind, reserved[2];
  solar_sudoku_t sudoku;
  solar_mines_t mines;
} puzzle_save_t;
typedef struct {
  solar_os_tui_t tui;
  puzzle_save_t save;
  bool suspended, help, confirm, nes;
  uint8_t previous;
  uint32_t started, last_move;
  char message[128], path[160];
} puzzle_ui_t;
static void *states[2];
static unsigned which(solar_os_context_t *c) {
  return !strcmp(solar_os_context_argv(c, 0), "mines");
}
static puzzle_ui_t *ui(solar_os_context_t *c) { return states[which(c)]; }
static uint32_t now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void new_game(puzzle_ui_t *u) {
  uint32_t seed = now() ^ (uint32_t)esp_timer_get_time();
  u->save.elapsed = 0;
  u->save.cursor = 0;
  u->started = now();
  if (u->save.kind)
    solar_mines_new(&u->save.mines, seed);
  else
    solar_sudoku_new(&u->save.sudoku, seed);
  strcpy(u->message, u->save.kind ? "First reveal and its neighbors are safe"
                                  : "Unique solution; h gives a counted hint");
}
static bool valid_save(const puzzle_save_t *s, unsigned kind) {
  if (s->magic != 0x50444131 || s->kind != kind || s->cursor >= 81)
    return false;
  if (!kind) {
    for (unsigned i = 0; i < 81; i++)
      if (s->sudoku.value[i] > 9 || s->sudoku.given[i] > 1 ||
          s->sudoku.solution[i] < 1 || s->sudoku.solution[i] > 9 ||
          (s->sudoku.given[i] && s->sudoku.value[i] != s->sudoku.solution[i]))
        return false;
    solar_sudoku_t solved = s->sudoku;
    memcpy(solved.value, solved.solution, 81);
    return solar_sudoku_solved(&solved);
  }
  if (s->mines.started > 1 || s->mines.won > 1 || s->mines.lost > 1 ||
      (s->mines.lost && s->mines.won))
    return false;
  unsigned bombs = 0;
  for (unsigned i = 0; i < 81; i++) {
    if (s->mines.bomb[i] > 1 || s->mines.open[i] > 1 || s->mines.flag[i] > 1 ||
        s->mines.around[i] > 8)
      return false;
    bombs += s->mines.bomb[i];
  }
  return s->mines.started ? bombs == 10 : bombs == 0;
}
static bool finished(const puzzle_ui_t *u) {
  return u->save.kind ? (u->save.mines.lost || u->save.mines.won)
                      : solar_sudoku_solved(&u->save.sudoku);
}
static void persist(puzzle_ui_t *u) {
  if (u->started)
    u->save.elapsed += now() - u->started;
  u->started = finished(u) ? 0 : now();
  if (!*u->path)
    return;
  struct {
    uint32_t crc;
    puzzle_save_t data;
  } disk;
  memset(&disk, 0, sizeof(disk));
  disk.data = u->save;
  disk.crc = pim_crc(&disk.data, sizeof(disk.data));
  if (solar_os_writer_safe_replace(u->path, (const char *)&disk, sizeof(disk),
                                   SOLAR_OS_WRITER_FILE_FAULT_NONE) != ESP_OK)
    strcpy(u->message, "Progress save failed; game retained in memory");
}
static void render(puzzle_ui_t *u) {
  if (u->suspended)
    return;
  unsigned rows = solar_os_tui_rows(&u->tui), cols = solar_os_tui_cols(&u->tui);
  if (rows < 15 || cols < 21) {
    solar_os_tui_clear(&u->tui);
    solar_os_tui_write_cell(&u->tui, 0, 0, cols, "Puzzle needs 21x15; q exits",
                            0);
    solar_os_tui_refresh(&u->tui);
    return;
  }
  solar_os_tui_clear(&u->tui);
  char text[128];
  bool kind = u->save.kind;
  unsigned seconds =
      (u->save.elapsed + (u->started ? now() - u->started : 0)) / 1000;
  snprintf(text, sizeof(text), "%s %u:%02u %s", kind ? "Minesweeper" : "Sudoku",
           seconds / 60, seconds % 60,
           kind ? (u->save.mines.won    ? "WON"
                   : u->save.mines.lost ? "LOST"
                                        : "10 mines")
           : solar_sudoku_solved(&u->save.sudoku) ? "SOLVED"
                                                  : "");
  solar_os_tui_write_cell(&u->tui, 0, 0, cols, text, 1);
  if (u->help) {
    const char *help[] = {"Arrows move; n new (confirm)",
                          "Sudoku: 1..9 enter; 0 clear",
                          "h hint, c check conflicts",
                          "NES A cycles digit; B clears",
                          "Mines: Enter/Space reveal",
                          "f flag, Enter on number chord",
                          "NES A reveal, B flag",
                          "Start checks/new; Select help",
                          "Progress saved on exit/suspend",
                          "Ctrl+Z suspends; fg resumes",
                          "? help, q/Esc close"};
    for (unsigned i = 0; i < sizeof(help) / sizeof(help[0]) && i + 2 < rows - 2;
         i++)
      solar_os_tui_write_cell(&u->tui, i + 2, 0, cols, help[i], 0);
  } else {
    snprintf(text, sizeof(text), "Row %u Col %u %s", u->save.cursor / 9 + 1,
             u->save.cursor % 9 + 1, u->nes ? "NES active" : "keyboard");
    solar_os_tui_write_cell(&u->tui, 1, 0, cols, text, 0);
    for (unsigned r = 0; r < 9; r++) {
      unsigned row = 2 + r + (!kind ? r / 3 : 0);
      for (unsigned c = 0; c < 9; c++) {
        unsigned index = r * 9 + c;
        char v[3] = {' ', 0, 0};
        unsigned attr = index == u->save.cursor ? SOLAR_OS_TUI_ATTR_INVERSE : 0;
        if (kind) {
          solar_mines_t *m = &u->save.mines;
          v[0] = m->flag[index]     ? 'F'
                 : !m->open[index]  ? (m->lost && m->bomb[index] ? '*' : '#')
                 : m->bomb[index]   ? '*'
                 : m->around[index] ? '0' + m->around[index]
                                    : '.';
        } else {
          unsigned n = u->save.sudoku.value[index];
          v[0] = n ? '0' + n : '.';
          if (u->save.sudoku.given[index])
            attr |= SOLAR_OS_TUI_ATTR_BOLD;
          if (solar_sudoku_conflict(&u->save.sudoku, index))
            attr |= SOLAR_OS_TUI_ATTR_UNDERLINE;
        }
        unsigned col = c * 2 + (!kind ? c / 3 : 0);
        solar_os_tui_write_cell(&u->tui, row, col, 1, v, attr);
      }
    }
  }
  solar_os_tui_write_cell(&u->tui, rows - 2, 0, cols,
                          u->confirm ? "Start new game? y / n" : u->message, 0);
  solar_os_tui_write_cell(&u->tui, rows - 1, 0, cols,
                          "? help n new q close; arrows move", 0);
  solar_os_tui_refresh(&u->tui);
}
static void action(puzzle_ui_t *u, unsigned char c) {
  if (u->confirm) {
    if (c == 'y' || c == 'Y') {
      new_game(u);
      u->confirm = false;
    } else if (c == 'n' || c == 27)
      u->confirm = false;
    return;
  }
  if (c == '?') {
    u->help = !u->help;
    return;
  }
  if (u->help) {
    if (c == 27)
      u->help = false;
    return;
  }
  if (c == 'n') {
    u->confirm = true;
    return;
  }
  unsigned index = u->save.cursor;
  if (c == SOLAR_OS_KEY_LEFT) {
    if (index % 9)
      u->save.cursor--;
  } else if (c == SOLAR_OS_KEY_RIGHT) {
    if (index % 9 < 8)
      u->save.cursor++;
  } else if (c == SOLAR_OS_KEY_UP) {
    if (index >= 9)
      u->save.cursor -= 9;
  } else if (c == SOLAR_OS_KEY_DOWN) {
    if (index < 72)
      u->save.cursor += 9;
  } else if (u->save.kind) {
    if (c == 'f')
      solar_mines_flag(&u->save.mines, index);
    else if (c == '\r' || c == '\n' || c == ' ')
      solar_mines_reveal(&u->save.mines, index);
  } else {
    if (c >= '0' && c <= '9')
      solar_sudoku_set(&u->save.sudoku, index, c - '0');
    else if (c == 8 || c == 127)
      solar_sudoku_set(&u->save.sudoku, index, 0);
    else if (c == 'h' && !u->save.sudoku.given[index]) {
      solar_sudoku_set(&u->save.sudoku, index, u->save.sudoku.solution[index]);
      u->save.sudoku.hints++;
      snprintf(u->message, sizeof(u->message), "Hints used: %lu",
               (unsigned long)u->save.sudoku.hints);
    } else if (c == 'c') {
      unsigned count = 0;
      for (unsigned i = 0; i < 81; i++)
        count += solar_sudoku_conflict(&u->save.sudoku, i);
      snprintf(u->message, sizeof(u->message),
               "%u conflicting cells / %lu hints", count,
               (unsigned long)u->save.sudoku.hints);
    }
  }
  if (finished(u) && u->started) {
    u->save.elapsed += now() - u->started;
    u->started = 0;
  }
}
static esp_err_t start(solar_os_context_t *ctx) {
  puzzle_ui_t *u = ui(ctx);
  u->save.kind = which(ctx);
  u->save.magic = 0x50444131;
  char dir[128];
  if (solar_os_storage_default_path(".organizer", dir, sizeof(dir)) == ESP_OK) {
    if (mkdir(dir, 0777) != 0 && errno != EEXIST)
      dir[0] = 0;
    if (*dir)
      snprintf(u->path, sizeof(u->path), "%s/%s.bin", dir,
               u->save.kind ? "mines" : "sudoku");
  }
  bool loaded = false;
  FILE *f = *u->path ? fopen(u->path, "rb") : NULL;
  if (f) {
    struct {
      uint32_t crc;
      puzzle_save_t data;
    } disk;
    if (fread(&disk, 1, sizeof(disk), f) == sizeof(disk) && fgetc(f) == EOF &&
        disk.crc == pim_crc(&disk.data, sizeof(disk.data)) &&
        valid_save(&disk.data, u->save.kind)) {
      u->save = disk.data;
      loaded = true;
    }
    fclose(f);
  }
  if (!loaded)
    new_game(u);
  else
    strcpy(u->message, "Saved game resumed");
  u->started = finished(u) ? 0 : now();
  u->nes = solar_os_puzzle_nes_claim(u, true);
  u->previous = u->nes ? solar_os_puzzle_nes_read(u) : 0;
  esp_err_t err = solar_os_tui_screen_begin(&u->tui, ctx);
  if (err != ESP_OK)
    return err;
  solar_os_tui_enable_diff(&u->tui, true);
  render(u);
  return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
  puzzle_ui_t *u = ui(ctx);
  if (!u->suspended)
    persist(u);
  if (u->nes)
    solar_os_puzzle_nes_claim(u, false);
  solar_os_tui_end(&u->tui);
}
static void suspend_ui(solar_os_context_t *ctx) {
  puzzle_ui_t *u = ui(ctx);
  persist(u);
  u->suspended = true;
  if (u->nes)
    solar_os_puzzle_nes_claim(u, false);
  u->nes = false;
}
static void resume_ui(solar_os_context_t *ctx) {
  puzzle_ui_t *u = ui(ctx);
  u->suspended = false;
  u->started = finished(u) ? 0 : now();
  u->nes = solar_os_puzzle_nes_claim(u, true);
  u->previous = u->nes ? solar_os_puzzle_nes_read(u) : 0;
  solar_os_tui_attach_session(&u->tui);
  render(u);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *e) {
  puzzle_ui_t *u = ui(ctx);
  if (e->type == SOLAR_OS_EVENT_TICK) {
    if (u->suspended)
      return true;
    if (u->nes) {
      uint8_t mask = solar_os_puzzle_nes_read(u), pressed = mask & ~u->previous;
      uint32_t at = now();
      if (mask & 0xf0) {
        if ((pressed & 0xf0) || at - u->last_move > 150) {
          if (mask & 64)
            action(u, SOLAR_OS_KEY_UP);
          if (mask & 128)
            action(u, SOLAR_OS_KEY_DOWN);
          if (mask & 32)
            action(u, SOLAR_OS_KEY_LEFT);
          if (mask & 16)
            action(u, SOLAR_OS_KEY_RIGHT);
          u->last_move = at;
        }
      }
      if (pressed & 1) {
        if (u->confirm)
          action(u, 'y');
        else if (u->save.kind)
          action(u, ' ');
        else
          action(u, '0' + ((u->save.sudoku.value[u->save.cursor] + 1) % 10));
      }
      if (pressed & 2)
        action(u, u->confirm ? 'n' : u->save.kind ? 'f' : '0');
      if (pressed & 4)
        action(u, '?');
      if (pressed & 8)
        action(u, u->save.kind ? 'n' : 'c');
      u->previous = mask;
    }
    render(u);
    return true;
  }
  if (e->type != SOLAR_OS_EVENT_CHAR || u->suspended)
    return false;
  unsigned char key = e->data.ch;
  if (key == SOLAR_OS_KEY_APP_EXIT ||
      ((key == 'q' || key == 27) && !u->help && !u->confirm))
    solar_os_context_finish(ctx, 0, NULL);
  else
    action(u, key);
  return true;
}
#define PUZZLE_APP(NAME, SLOT)                                                 \
  {.name = NAME,                                                               \
   .summary = "saved keyboard/NES puzzle game",                                \
   .app_class = SOLAR_OS_APP_CLASS_TUI,                                        \
   .flags = SOLAR_OS_APP_FLAG_RESUMABLE,                                       \
   .start = start,                                                             \
   .stop = stop,                                                               \
   .suspend = suspend_ui,                                                      \
   .resume = resume_ui,                                                        \
   .event = event,                                                             \
   .state_slot = &states[SLOT],                                                \
   .state_size = sizeof(puzzle_ui_t),                                          \
   .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,                      \
   .tick_interval_ms = 100}
const solar_os_app_t solar_os_sudoku_app = PUZZLE_APP("sudoku", 0);
const solar_os_app_t solar_os_mines_app = PUZZLE_APP("mines", 1);
