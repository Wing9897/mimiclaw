/*
 * MimiClaw LCD Display Module
 * Waveshare ESP32-S3-LCD-1.47 (ST7789 172x320)
 *
 * Controlled by MIMI_HAS_LCD in mimi_secrets.h (default off).
 */
#pragma once

#include "esp_err.h"
#include "mimi_config.h"

/**
 * Initialize LCD hardware (SPI bus, ST7789 panel, backlight, button).
 */
esp_err_t display_init(void);

/**
 * Start the display task (renders pages, handles button).
 */
esp_err_t display_start(void);
