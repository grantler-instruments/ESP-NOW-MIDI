#pragma once
/**
 * @file ssd1306_display.h
 * @brief Minimal from-scratch SSD1306 I2C driver + tiny text/bitmap renderer
 *        for examples_idf/dongle, since Adafruit_GFX/Adafruit_SSD1306 (used
 *        by examples/dongle/SSD1306Display.h) are Arduino-only.
 *
 * Matches examples/dongle/SSD1306Display.h's layout and splash logo exactly.
 * Font is a simplified 5x7, uppercase-only (lowercase input folds to
 * uppercase before lookup) - not pixel-identical to Adafruit's font, agreed
 * acceptable since only layout/logo fidelity matters here.
 *
 * UNVERIFIED against a real build: driver/i2c_master.h's exact struct field
 * names (i2c_master_bus_config_t / i2c_device_config_t) and the SSD1306 init
 * command sequence. The init sequence itself is extremely standard (same
 * shape across nearly every SSD1306 driver ever written), but neither has
 * been checked against real hardware or a real IDF build - expect the same
 * local-build-and-fix loop as the USB MIDI port.
 *
 * Adjust OLED_SDA_GPIO/OLED_SCL_GPIO below if wrong for your board.
 */

#include "enomik_dongle.h"
#include "./logo.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_now_midi_compat.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef OLED_SDA_GPIO
#define OLED_SDA_GPIO 33
#endif
#ifndef OLED_SCL_GPIO
#define OLED_SCL_GPIO 35
#endif

#ifndef SCREEN_ADDRESS
#define SCREEN_ADDRESS 0x3C
#endif
#ifndef SPLASH_DURATION_MS
#define SPLASH_DURATION_MS 2000
#endif
#ifndef HEADER_ALT_INTERVAL_MS
#define HEADER_ALT_INTERVAL_MS 4000
#endif

namespace ssd1306_font_detail
{

struct Glyph
{
    char ch;
    const char *rows; // 7 rows x 5 cols, concatenated, '#' = on
};

// Simplified 5x7 font covering exactly what examples/dongle's status screen
// prints (digits, a handful of uppercase letters + '_' for the status
// mnemonics, and ':' '.' '-' '>' '<' for the header/history formatting).
static const Glyph FONT_TABLE[] = {
    {'0', ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###."},
    {'1', "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'2', ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####"},
    {'3', ".###." "#...#" "....#" "..##." "....#" "#...#" ".###."},
    {'4', "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#."},
    {'5', "#####" "#...." "####." "....#" "....#" "#...#" ".###."},
    {'6', "..##." ".#..." "#...." "####." "#...#" "#...#" ".###."},
    {'7', "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#..."},
    {'8', ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###."},
    {'9', ".###." "#...#" "#...#" ".####" "....#" "...#." ".##.."},
    {'A', "..#.." ".#.#." "#...#" "#...#" "#####" "#...#" "#...#"},
    {'B', "####." "#...#" "#...#" "####." "#...#" "#...#" "####."},
    {'C', ".####" "#...." "#...." "#...." "#...." "#...." ".####"},
    {'D', "####." "#...#" "#...#" "#...#" "#...#" "#...#" "####."},
    {'E', "#####" "#...." "#...." "####." "#...." "#...." "#####"},
    {'F', "#####" "#...." "#...." "####." "#...." "#...." "#...."},
    {'K', "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#"},
    {'N', "#...#" "##..#" "#.#.#" "#.#.#" "#..##" "#...#" "#...#"},
    {'O', ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'P', "####." "#...#" "#...#" "####." "#...." "#...." "#...."},
    {'R', "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#"},
    {'S', ".####" "#...." "#...." ".###." "....#" "....#" "####."},
    {'T', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'U', "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {':', "....." "..#.." "..#.." "....." "..#.." "..#.." "....."},
    {'.', "....." "....." "....." "....." "....." "..#.." "....."},
    {'-', "....." "....." "....." "#####" "....." "....." "....."},
    {'>', "#...." ".#..." "..#.." "...#." "..#.." ".#..." "#...."},
    {'<', "....#" "...#." "..#.." ".#..." "..#.." "...#." "....#"},
    {'_', "....." "....." "....." "....." "....." "....." "#####"},
};

constexpr int FONT_TABLE_SIZE = sizeof(FONT_TABLE) / sizeof(FONT_TABLE[0]);

} // namespace ssd1306_font_detail

class Ssd1306Panel
{
public:
    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 64;
    static constexpr int PAGES = HEIGHT / 8;

    bool begin(gpio_num_t sda, gpio_num_t scl, uint8_t address)
    {
        i2c_master_bus_config_t busCfg = {};
        busCfg.i2c_port = -1;
        busCfg.sda_io_num = sda;
        busCfg.scl_io_num = scl;
        busCfg.clk_source = I2C_CLK_SRC_DEFAULT;
        busCfg.glitch_ignore_cnt = 7;
        busCfg.flags.enable_internal_pullup = true;

        if (i2c_new_master_bus(&busCfg, &bus_) != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_new_master_bus failed");
            return false;
        }

        i2c_device_config_t devCfg = {};
        devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devCfg.device_address = address;
        devCfg.scl_speed_hz = 400000;

        if (i2c_master_bus_add_device(bus_, &devCfg, &dev_) != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
            return false;
        }

        // Standard SSD1306 128x64 init sequence (horizontal addressing mode).
        static const uint8_t initSeq[] = {
            0xAE,       // display off
            0xD5, 0x80, // clock divide / oscillator freq
            0xA8, 0x3F, // multiplex ratio = 63 (64px height)
            0xD3, 0x00, // display offset = 0
            0x40,       // display start line = 0
            0x8D, 0x14, // charge pump enable
            0x20, 0x00, // memory addressing mode = horizontal
            0xA1,       // segment remap
            0xC8,       // COM output scan direction, remapped
            0xDA, 0x12, // COM pins hardware config
            0x81, 0xCF, // contrast
            0xD9, 0xF1, // pre-charge period
            0xDB, 0x40, // VCOMH deselect level
            0xA4,       // resume to RAM content display
            0xA6,       // normal (non-inverted) display
            0xAF,       // display on
        };
        if (!writeCommands(initSeq, sizeof(initSeq)))
        {
            ESP_LOGE(TAG, "SSD1306 init sequence failed");
            return false;
        }

        clearDisplay();
        return true;
    }

    void clearDisplay()
    {
        memset(fb_, 0, sizeof(fb_));
    }

    void drawPixel(int x, int y, bool on)
    {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        {
            return;
        }
        int page = y / 8;
        uint8_t mask = static_cast<uint8_t>(1u << (y % 8));
        uint8_t &b = fb_[page * WIDTH + x];
        if (on)
        {
            b |= mask;
        }
        else
        {
            b &= static_cast<uint8_t>(~mask);
        }
    }

    void drawLine(int x0, int y0, int x1, int y1)
    {
        int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
        int sx = (x1 >= x0) ? 1 : -1;
        int sy = (y1 >= y0) ? 1 : -1;
        int err = dx - dy;

        while (true)
        {
            drawPixel(x0, y0, true);
            if (x0 == x1 && y0 == y1)
            {
                break;
            }
            int e2 = 2 * err;
            if (e2 > -dy)
            {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx)
            {
                err += dx;
                y0 += sy;
            }
        }
    }

    // XBM bit-unpacking: LSB-first per byte, rows padded to byte boundary -
    // same convention Adafruit_GFX::drawXBitmap uses, so logo.h's bytes work
    // unmodified.
    void drawXBitmap(int x, int y, const unsigned char *bitmap, int w, int h)
    {
        int bytesPerRow = (w + 7) / 8;
        for (int row = 0; row < h; row++)
        {
            for (int col = 0; col < w; col++)
            {
                uint8_t byte = bitmap[row * bytesPerRow + (col / 8)];
                if (byte & (1u << (col % 8)))
                {
                    drawPixel(x + col, y + row, true);
                }
            }
        }
    }

    void setCursor(int x, int y)
    {
        cursorX_ = x;
        cursorY_ = y;
    }

    void print(const char *str)
    {
        for (const char *p = str; *p; p++)
        {
            drawChar(*p);
        }
    }

    void println(const char *str)
    {
        print(str);
        cursorX_ = 0;
        cursorY_ += LINE_HEIGHT;
    }

    bool display()
    {
        for (int page = 0; page < PAGES; page++)
        {
            const uint8_t cmds[] = {
                static_cast<uint8_t>(0xB0 + page), // set page start address
                0x00,                              // set lower column address = 0
                0x10,                              // set higher column address = 0
            };
            if (!writeCommands(cmds, sizeof(cmds)))
            {
                return false;
            }
            if (!writeData(&fb_[page * WIDTH], WIDTH))
            {
                return false;
            }
        }
        return true;
    }

private:
    static constexpr const char *TAG = "ssd1306";
    static constexpr int CHAR_WIDTH = 6; // 5px glyph + 1px gap
    static constexpr int LINE_HEIGHT = 8;

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t fb_[WIDTH * PAGES] = {};
    int cursorX_ = 0;
    int cursorY_ = 0;

    bool writeCommands(const uint8_t *cmds, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            uint8_t packet[2] = {0x00, cmds[i]}; // control byte 0x00 = command
            if (i2c_master_transmit(dev_, packet, sizeof(packet), 1000) != ESP_OK)
            {
                return false;
            }
        }
        return true;
    }

    bool writeData(const uint8_t *data, size_t len)
    {
        uint8_t buf[1 + WIDTH];
        buf[0] = 0x40; // control byte 0x40 = data
        memcpy(&buf[1], data, len);
        return i2c_master_transmit(dev_, buf, len + 1, 1000) == ESP_OK;
    }

    void drawChar(char c)
    {
        if (c == '\n')
        {
            cursorX_ = 0;
            cursorY_ += LINE_HEIGHT;
            return;
        }

        const char upper = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        const char *rows = nullptr;
        for (int i = 0; i < ssd1306_font_detail::FONT_TABLE_SIZE; i++)
        {
            if (ssd1306_font_detail::FONT_TABLE[i].ch == upper)
            {
                rows = ssd1306_font_detail::FONT_TABLE[i].rows;
                break;
            }
        }

        if (rows)
        {
            for (int r = 0; r < 7; r++)
            {
                for (int col = 0; col < 5; col++)
                {
                    if (rows[r * 5 + col] == '#')
                    {
                        drawPixel(cursorX_ + col, cursorY_ + r, true);
                    }
                }
            }
        }

        cursorX_ += CHAR_WIDTH;
    }
};

/**
 * @brief OLED status UI for enomik::Dongle - IDF port of
 *        examples/dongle/SSD1306Display.h. Same layout, same splash logo.
 */
class SSD1306Display final : public enomik::Dongle::Display
{
public:
    bool begin() override
    {
        if (!panel_.begin(static_cast<gpio_num_t>(OLED_SDA_GPIO),
                           static_cast<gpio_num_t>(OLED_SCL_GPIO),
                           SCREEN_ADDRESS))
        {
            return false;
        }
        drawSplash();
        splashUntilMs_ = millis() + SPLASH_DURATION_MS;
        return true;
    }

    void update(
        const uint8_t mac[6],
        const char *version,
        int peerCount,
        char usbStatus,
        const enomik::MidiMessageHistory *history,
        int historySize,
        int historyHead) override
    {
        if (splashUntilMs_ != 0 && millis() < splashUntilMs_)
        {
            return;
        }
        splashUntilMs_ = 0;

        panel_.clearDisplay();
        drawHeader(mac, version, peerCount, usbStatus);
        drawHistory(history, historySize, historyHead);
        panel_.display();
    }

private:
    Ssd1306Panel panel_;
    uint32_t splashUntilMs_ = 0;

    void drawSplash()
    {
        panel_.clearDisplay();
        const int x = (Ssd1306Panel::WIDTH - LOGO_WIDTH) / 2;
        const int y = (Ssd1306Panel::HEIGHT - LOGO_HEIGHT) / 2;
        panel_.drawXBitmap(x, y, logo_bits, LOGO_WIDTH, LOGO_HEIGHT);
        panel_.display();
    }

    void drawHeader(const uint8_t mac[6],
                     const char *version,
                     int peers,
                     char usbStatus)
    {
        static char macStr[18];
        static char buf[64];

        unsigned long displayUptime = (millis() / 1000) % 86400;

        snprintf(macStr, sizeof(macStr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);

        panel_.setCursor(0, 0);

        snprintf(buf, sizeof(buf), "mac:%s", macStr);
        panel_.println(buf);

        const bool showCon = ((millis() / HEADER_ALT_INTERVAL_MS) % 2) == 0;
        if (showCon)
        {
            snprintf(buf, sizeof(buf), "v%s con:%d t:%lu",
                     version, peers, displayUptime);
        }
        else
        {
            snprintf(buf, sizeof(buf), "v%s usb:%c t:%lu",
                     version, usbStatus, displayUptime);
        }
        panel_.println(buf);

        panel_.drawLine(0, 18, Ssd1306Panel::WIDTH, 18);
    }

    void drawHistory(const enomik::MidiMessageHistory *history,
                      int size,
                      int head)
    {
        int y = 22;

        for (int i = 0; i < size; ++i)
        {
            int idx = (head + i) % size;
            const enomik::MidiMessageHistory &h = history[idx];

            if (h.timestamp == 0)
            {
                continue;
            }

            drawHistoryLine(h, y);
            y += 8;

            if (y > Ssd1306Panel::HEIGHT - 8)
            {
                break;
            }
        }
    }

    void drawHistoryLine(const enomik::MidiMessageHistory &h, int y)
    {
        char status[7];
        char line[32];

        formatStatus(h.message.status, status);

        snprintf(line, sizeof(line),
                 "%s %s %02X %3d %3d",
                 h.outgoing ? "->" : "<-",
                 status,
                 h.message.channel,
                 h.message.firstByte,
                 h.message.secondByte);

        panel_.setCursor(0, y);
        panel_.print(line);
    }

    static void formatStatus(uint8_t status, char out[7])
    {
        switch (status)
        {
        case MIDI_NOTE_ON: strcpy(out, "N_ON "); break;
        case MIDI_NOTE_OFF: strcpy(out, "N_OFF"); break;
        case MIDI_CONTROL_CHANGE: strcpy(out, "CC   "); break;
        case MIDI_PROGRAM_CHANGE: strcpy(out, "PC   "); break;
        case MIDI_PITCH_BEND: strcpy(out, "PBEND"); break;
        case MIDI_AFTERTOUCH: strcpy(out, "AT   "); break;
        case MIDI_POLY_AFTERTOUCH: strcpy(out, "PAT  "); break;
        case MIDI_START: strcpy(out, "START"); break;
        case MIDI_STOP: strcpy(out, "STOP "); break;
        case MIDI_CONTINUE: strcpy(out, "CONT "); break;
        default: strcpy(out, "UNK  "); break;
        }
    }
};
