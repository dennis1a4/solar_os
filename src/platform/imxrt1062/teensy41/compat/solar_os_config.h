#pragma once
/* Incremental USB shell profile; enable only integrated applications. */
#define SOLAR_OS_PACKAGE_CORE_FS_COMMANDS 1
#define SOLAR_OS_PACKAGE_APP_CALC 1
#define SOLAR_OS_PACKAGE_APP_EDIT 1
#define SOLAR_OS_PACKAGE_APP_PYTHON 1

#if SK_AUDIO_PLAYER
#define SOLAR_OS_PACKAGE_APP_APLAY 1
#define SOLAR_OS_PACKAGE_APP_ARECORD 1
#define SOLAR_OS_PACKAGE_SERVICE_AUDIO_CODECS 1
#endif
