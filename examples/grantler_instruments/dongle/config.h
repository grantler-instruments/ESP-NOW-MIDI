#define HAS_DISPLAY 1

// SSD1306 OLED (used by SSD1306Display.h when HAS_DISPLAY == 1)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; or run an i2c scanner
#define SPLASH_DURATION_MS 2000
#define HEADER_ALT_INTERVAL_MS 4000

// Two-button menu (INPUT_PULLUP, idle HIGH)
#define BTN_CURSOR 16
#define BTN_SELECT 17
#define BTN_LONG_PRESS_MS 1000
#define MENU_IDLE_TIMEOUT_MS 60000
