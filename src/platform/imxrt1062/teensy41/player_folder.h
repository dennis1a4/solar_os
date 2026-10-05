#pragma once
#include "solar_os_player_playlist.h"
#define SK_PLAYER_FOLDER_MAX 512U
esp_err_t sk_player_folder_open(const char *path);
void sk_player_folder_close(void);
size_t sk_player_folder_shuffle(bool enabled, size_t keep_index);
bool sk_player_folder_shuffled(void);
