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

// Test only: simulate USB faults over the serial monitor to exercise the USB
// watchdog without Windows. Commands: s = stuck endpoint, u = suspended with
// refused wakeup, n = no fault, p = print USB health. First press a client
// button once with a MIDI app open (so the dongle knows the computer reads),
// then inject a fault and press the button again: the dongle re-attaches USB
// (stuck: after ~0.5 s, suspended: immediately).
// #define ENOMIK_USB_FAULT_INJECT
