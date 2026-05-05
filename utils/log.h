#pragma once

#define ENOMIK_LOG_NONE  0
#define ENOMIK_LOG_ERROR 1
#define ENOMIK_LOG_DEBUG 2

#ifndef ENOMIK_LOG_LEVEL
#define ENOMIK_LOG_LEVEL ENOMIK_LOG_NONE
#endif

#if defined(ARDUINO)
    #include <Arduino.h>
    #define ENOMIK_LOG_IMPL(fmt, ...)   Serial.printf(fmt "\n", ##__VA_ARGS__)
#elif defined(ESP_PLATFORM)
    #include "esp_log.h"
    #define ENOMIK_LOG_TAG "enomik"
    #define ENOMIK_LOG_IMPL(fmt, ...)   ESP_LOGI(ENOMIK_LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    #define ENOMIK_LOG_IMPL(fmt, ...)   printf(fmt "\n", ##__VA_ARGS__)
#endif

#if ENOMIK_LOG_LEVEL >= ENOMIK_LOG_DEBUG
    #define enomik_log_debug(fmt, ...)  ENOMIK_LOG_IMPL("[DBG] " fmt, ##__VA_ARGS__)
#else
    #define enomik_log_debug(fmt, ...)  do {} while(0)
#endif

#if ENOMIK_LOG_LEVEL >= ENOMIK_LOG_ERROR
    #define enomik_log_error(fmt, ...)  ENOMIK_LOG_IMPL("[ERR] " fmt, ##__VA_ARGS__)
#else
    #define enomik_log_error(fmt, ...)  do {} while(0)
#endif

#define enomik_log(fmt, ...)            ENOMIK_LOG_IMPL(fmt, ##__VA_ARGS__)
