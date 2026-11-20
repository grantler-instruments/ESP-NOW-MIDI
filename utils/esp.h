// Define ENOMIK_NO_USB_MIDI to keep native USB free (e.g. for the USB serial
// console while debugging) on S2/S3.
#if (defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)) && (CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3) && !defined(ENOMIK_NO_USB_MIDI)
    #define HAS_USB_MIDI
#endif