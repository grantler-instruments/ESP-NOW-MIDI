#pragma once

#include "enomik_dongle.h"
#include "./config.h"
#include "./logo.h"
#include "./Menu.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <cstring>

using GrantlerMenu = Menu<enomik::Dongle>;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

/**
 * @brief OLED status + two-button menu UI for the Grantler dongle.
 *
 * Status page only redraws what actually changed, and only pushes the affected
 * SSD1306 hardware page(s) over I2C instead of the whole 1024-byte frame:
 *   - page 0 (mac line): static, drawn once when the splash ends, never again
 *   - page 1 (version/peers/usb/uptime line): redrawn+pushed only when its
 *     text changes
 *   - pages 2-7 (separator + history): the separator is drawn once with the
 *     mac line; the history block is redrawn+pushed only when a new message
 *     arrives (historyHead advances)
 *
 * Menu and Peers pages replace the full frame while they are active, then
 * restore the status chrome when returning home.
 */
class SSD1306Display final : public enomik::Dongle::Display {
public:
  SSD1306Display()
    : oled_(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

  void setDongle(enomik::Dongle* dongle) { dongle_ = dongle; }
  void setMenu(GrantlerMenu* menu) { menu_ = menu; }

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
    if (splashUntilMs_ != 0) {
      if (millis() < splashUntilMs_) {
        return;
      }
      splashUntilMs_ = 0;

      lastHeaderLine2_[0] = '\0';
      lastHistoryHead_ = -1;
      forceFullPush_ = true;
    }

    const GrantlerMenu::Page page = menu_ ? menu_->page() : GrantlerMenu::Page::Status;

    if (page != GrantlerMenu::Page::Status) {
      menu_->syncPeerList(peerCount);
      drawOverlayIfChanged(page, peerCount);
      return;
    }

    if (lastDrawnPage_ != GrantlerMenu::Page::Status || forceFullPush_) {
      oled_.clearDisplay();
      drawMacLine(mac);
      drawSeparator();
      lastHeaderLine2_[0] = '\0';
      lastHistoryHead_ = -1;
      forceFullPush_ = true;
    }

    lastDrawnPage_ = GrantlerMenu::Page::Status;

    const bool header2Dirty = drawHeaderLine2IfChanged(version, peerCount, usbStatus);
    const bool historyDirty = drawHistoryIfChanged(history, historySize, historyHead);

    if (forceFullPush_) {
      pushPages(0, 7);
      forceFullPush_ = false;
      return;
    }
    if (header2Dirty) {
      pushPages(1, 1);
    }
    if (historyDirty) {
      pushPages(2, 7);
    }
  }

private:
  Adafruit_SSD1306 oled_;
  enomik::Dongle* dongle_ = nullptr;
  GrantlerMenu* menu_ = nullptr;
  uint32_t splashUntilMs_ = 0;
  bool forceFullPush_ = false;
  char lastHeaderLine2_[64] = {0};
  int lastHistoryHead_ = -1;
  GrantlerMenu::Page lastDrawnPage_ = GrantlerMenu::Page::Status;
  int lastCursor_ = -1;
  int lastScroll_ = -1;
  int lastOverlayPeerCount_ = -1;
  int lastAddNibble_ = -1;
  uint8_t lastAddMac_[6] = {0};
  uint32_t lastMutedMask_ = 0;
  bool lastPowerSave_ = false;

  uint32_t mutedMaskFor(int peerCount) const {
    uint32_t mask = 0;
    if (!dongle_) {
      return 0;
    }
    const int n = peerCount < 32 ? peerCount : 32;
    for (int i = 0; i < n; ++i) {
      if (dongle_->isMuted(i)) {
        mask |= (1u << i);
      }
    }
    return mask;
  }

  void drawSplash() {
    oled_.clearDisplay();
    const int x = (SCREEN_WIDTH - LOGO_WIDTH) / 2;
    const int y = (SCREEN_HEIGHT - LOGO_HEIGHT) / 2;
    oled_.drawXBitmap(x, y, logo_bits, LOGO_WIDTH, LOGO_HEIGHT, SSD1306_WHITE);
    oled_.display();
  }

  void drawOverlayIfChanged(GrantlerMenu::Page page, int peerCount) {
    const int cursor = menu_->cursor();
    const int scroll = menu_->scroll();
    const int addNibble = menu_->addNibble();
    const uint8_t* addMac = menu_->addMac();
    const uint32_t mutedMask = mutedMaskFor(peerCount);
    const bool powerSave = dongle_ && dongle_->isPowerSave();
    if (!forceFullPush_ &&
        page == lastDrawnPage_ &&
        cursor == lastCursor_ &&
        scroll == lastScroll_ &&
        peerCount == lastOverlayPeerCount_ &&
        addNibble == lastAddNibble_ &&
        memcmp(addMac, lastAddMac_, 6) == 0 &&
        mutedMask == lastMutedMask_ &&
        powerSave == lastPowerSave_) {
      return;
    }

    lastDrawnPage_ = page;
    lastCursor_ = cursor;
    lastScroll_ = scroll;
    lastOverlayPeerCount_ = peerCount;
    lastAddNibble_ = addNibble;
    memcpy(lastAddMac_, addMac, 6);
    lastMutedMask_ = mutedMask;
    lastPowerSave_ = powerSave;
    forceFullPush_ = false;

    oled_.clearDisplay();
    if (page == GrantlerMenu::Page::AddPeer) {
      drawAddPeer();
    } else {
      drawListPage();
    }
    drawButtonHints();
    pushPages(0, 7);
  }

  void drawListPage() {
    oled_.setCursor(0, 0);
    oled_.print(menu_->title());

    const int count = menu_->listCount();
    const int scroll = menu_->scroll();
    const int cursor = menu_->cursor();
    for (int row = 0; row < GrantlerMenu::kVisibleRows; ++row) {
      const int idx = scroll + row;
      if (idx >= count) {
        break;
      }
      oled_.setCursor(0, 8 + row * 8);
      oled_.print(idx == cursor ? "> " : "  ");
      oled_.print(menu_->listLabel(idx));
    }
  }

  void drawAddPeer() {
    oled_.setCursor(0, 0);
    oled_.print(menu_->title());

    const uint8_t* mac = menu_->addMac();
    const int current = menu_->addNibble();
    oled_.setCursor(0, 16);

    for (int b = 0; b < 6; ++b) {
      if (b > 0) {
        oled_.print(':');
      }
      for (int n = 0; n < 2; ++n) {
        const int nibbleIndex = b * 2 + n;
        const uint8_t nibble =
          (n == 0) ? static_cast<uint8_t>(mac[b] >> 4) : static_cast<uint8_t>(mac[b] & 0x0F);
        const char c = (nibble < 10) ? static_cast<char>('0' + nibble)
                                     : static_cast<char>('A' + nibble - 10);
        if (nibbleIndex == current) {
          oled_.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          oled_.write(c);
          oled_.setTextColor(SSD1306_WHITE);
        } else {
          oled_.write(c);
        }
      }
    }
  }

  void drawButtonHints() {
    oled_.drawFastHLine(0, 55, SCREEN_WIDTH, SSD1306_WHITE);

    // Up/down triangles for the cursor button (default font has no arrows).
    oled_.fillTriangle(3, 57, 0, 62, 6, 62, SSD1306_WHITE);
    oled_.fillTriangle(11, 62, 8, 57, 14, 57, SSD1306_WHITE);

    const char* right = "ok";
    oled_.setCursor(SCREEN_WIDTH - 6 * static_cast<int>(strlen(right)), 56);
    oled_.print(right);
  }

  // Page 0 (rows 0-7): never repainted after this — the mac never changes.
  void drawMacLine(const uint8_t mac[6]) {
    static char macStr[18];
    static char buf[24];

    snprintf(macStr, sizeof(macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    snprintf(buf, sizeof(buf), "mac:%s", macStr);

    oled_.setCursor(0, 0);
    oled_.print(buf);
  }

  // Sits in page 2 alongside the top of the history block; drawn once and
  // left alone — history redraws only clear rows 22+, never touching row 18.
  void drawSeparator() {
    oled_.drawLine(0, 18, SCREEN_WIDTH, 18, SSD1306_WHITE);
  }

  // Page 1 (rows 8-15): rebuilt only when the text actually differs from the
  // last frame (uptime ticks once/sec, con/usb toggles every
  // HEADER_ALT_INTERVAL_MS).
  bool drawHeaderLine2IfChanged(const char* version, int peers, char usbStatus) {
    static char buf[64];

    unsigned long displayUptime = (millis() / 1000) % 86400;
    const bool showCon = ((millis() / HEADER_ALT_INTERVAL_MS) % 2) == 0;
    if (showCon) {
      snprintf(buf, sizeof(buf), "v%s con:%d t:%lu",
               version, peers, displayUptime);
    } else {
      snprintf(buf, sizeof(buf), "v%s usb:%c t:%lu",
               version, usbStatus, displayUptime);
    }

    if (strncmp(buf, lastHeaderLine2_, sizeof(lastHeaderLine2_)) == 0) {
      return false;
    }
    strncpy(lastHeaderLine2_, buf, sizeof(lastHeaderLine2_) - 1);
    lastHeaderLine2_[sizeof(lastHeaderLine2_) - 1] = '\0';

    oled_.fillRect(0, 8, SCREEN_WIDTH, 8, SSD1306_BLACK);
    oled_.setCursor(0, 8);
    oled_.print(buf);
    return true;
  }

  // Pages 2-7 (rows 22-63): only redrawn when a new message actually arrived.
  bool drawHistoryIfChanged(const enomik::MidiMessageHistory* history,
                             int size,
                             int head) {
    if (head == lastHistoryHead_) {
      return false;
    }
    lastHistoryHead_ = head;

    oled_.fillRect(0, 22, SCREEN_WIDTH, SCREEN_HEIGHT - 22, SSD1306_BLACK);

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
    return true;
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

  // Pushes SSD1306 hardware pages [pageStart, pageEnd] (8 rows each) from the
  // local framebuffer over I2C, instead of Adafruit_SSD1306::display()'s
  // unconditional full-frame push. Mirrors that function's own I2C sequence
  // (see Adafruit_SSD1306::display()) restricted to a page range.
  void pushPages(uint8_t pageStart, uint8_t pageEnd) {
    static constexpr uint16_t WIRE_CHUNK = 128; // matches ESP32 core's I2C_BUFFER_LENGTH

    oled_.ssd1306_command(SSD1306_PAGEADDR);
    oled_.ssd1306_command(pageStart);
    oled_.ssd1306_command(pageEnd);
    oled_.ssd1306_command(SSD1306_COLUMNADDR);
    oled_.ssd1306_command(0);
    oled_.ssd1306_command(SCREEN_WIDTH - 1);

    uint8_t* ptr = oled_.getBuffer() + (uint16_t)pageStart * SCREEN_WIDTH;
    uint16_t count = (uint16_t)(pageEnd - pageStart + 1) * SCREEN_WIDTH;

    Wire.beginTransmission(SCREEN_ADDRESS);
    Wire.write((uint8_t)0x40);
    uint16_t bytesOut = 1;
    while (count--) {
      if (bytesOut >= WIRE_CHUNK) {
        Wire.endTransmission();
        Wire.beginTransmission(SCREEN_ADDRESS);
        Wire.write((uint8_t)0x40);
        bytesOut = 1;
      }
      Wire.write(*ptr++);
      bytesOut++;
    }
    Wire.endTransmission();
  }
};
