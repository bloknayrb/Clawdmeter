#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float session_pct;        // 5-hour window utilization (0–100)
    int   session_reset_mins; // minutes until session window resets
    float weekly_pct;         // 7-day window utilization (0–100)
    int   weekly_reset_mins;  // minutes until weekly window resets
    char  status[16];         // "allowed" or "limited"
    bool  ok;                 // JSON parse succeeded
    bool  valid;              // false until first successful parse
} UsageData;
