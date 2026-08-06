#pragma once

#include "enomik_dongle.h"
#include "./config.h"
#include "./logo.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

/**
 * @brief Example OLED status UI for enomik::Dongle.
 *
 * Subclass enomik::Dongle::Display and register with setDisplay() to use a
 * different panel or layout.
 */
class SSD1306Display final : public enomik::Dongle::Display {
public:
  SSD1306Display()
    : oled_(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

  bool begin() override {
    if (!oled_.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      return false;
    }
    oled_.setTextSize(1);
    oled_.setTextColor(SSD1306_WHITE);
    drawSplash();
    splashUntilMs_ = millis() + SPLASH_DURATION_MS;
    return true;
  }

  void update(
    const uint8_t mac[6],
    const char* version,
    int peerCount,
    char usbStatus,
    const enomik::MidiMessageHistory* history,
    int historySize,
    int historyHead) override {
    if (splashUntilMs_ != 0 && millis() < splashUntilMs_) {
      return;
    }
    splashUntilMs_ = 0;

    oled_.clearDisplay();
    drawHeader(mac, version, peerCount, usbStatus);
    drawHistory(history, historySize, historyHead);
    oled_.display();
  }

private:
  Adafruit_SSD1306 oled_;
  uint32_t splashUntilMs_ = 0;

  void drawSplash() {
    oled_.clearDisplay();
    const int x = (SCREEN_WIDTH - LOGO_WIDTH) / 2;
    const int y = (SCREEN_HEIGHT - LOGO_HEIGHT) / 2;
    oled_.drawXBitmap(x, y, logo_bits, LOGO_WIDTH, LOGO_HEIGHT, SSD1306_WHITE);
    oled_.display();
  }

  void drawHeader(const uint8_t mac[6],
                  const char* version,
                  int peers,
                  char usbStatus) {
    static char macStr[18];
    static char buf[64];

    unsigned long displayUptime = (millis() / 1000) % 86400;

    snprintf(macStr, sizeof(macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    oled_.setCursor(0, 0);

    snprintf(buf, sizeof(buf), "mac:%s", macStr);
    oled_.println(buf);

    const bool showCon = ((millis() / HEADER_ALT_INTERVAL_MS) % 2) == 0;
    if (showCon) {
      snprintf(buf, sizeof(buf), "v%s con:%d t:%lu",
               version, peers, displayUptime);
    } else {
      snprintf(buf, sizeof(buf), "v%s usb:%c t:%lu",
               version, usbStatus, displayUptime);
    }
    oled_.println(buf);

    oled_.drawLine(0, 18, SCREEN_WIDTH, 18, SSD1306_WHITE);
  }

  void drawHistory(const enomik::MidiMessageHistory* history,
                   int size,
                   int head) {
    int y = 22;

    for (int i = 0; i < size; ++i) {
      int idx = (head + i) % size;
      const enomik::MidiMessageHistory& h = history[idx];

      if (h.timestamp == 0) {
        continue;
      }

      drawHistoryLine(h, y);
      y += 8;

      if (y > SCREEN_HEIGHT - 8) {
        break;
      }
    }
  }

  void drawHistoryLine(const enomik::MidiMessageHistory& h, int y) {
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

    oled_.setCursor(0, y);
    oled_.print(line);
  }

  static void formatStatus(uint8_t status, char out[7]) {
    switch (status) {
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
