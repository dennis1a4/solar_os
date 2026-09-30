#pragma once
#include <arduino_freertos.h>
#include <semphr.h>

// Shared by filesystem calls and USB volume discovery. Never take from an ISR.
struct StorageLock {
    StorageLock();
    ~StorageLock();
};
