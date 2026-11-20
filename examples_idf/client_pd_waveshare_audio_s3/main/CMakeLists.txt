idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES enomik_pd
)

# Build a LittleFS image from ../pd and flash it with `idf.py flash`.
littlefs_create_partition_image(pd ../pd FLASH_IN_PROJECT)
