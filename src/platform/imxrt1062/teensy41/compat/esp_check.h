#pragma once
#define ESP_RETURN_ON_ERROR(expr,tag,...) do { esp_err_t check_error=(expr); if(check_error!=ESP_OK)return check_error; } while(0)
