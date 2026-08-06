idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_now_midi_dongle esp_driver_i2c esp_driver_gpio
)
