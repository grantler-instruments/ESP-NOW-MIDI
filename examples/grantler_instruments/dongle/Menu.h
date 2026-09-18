#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "./config.h"

#ifndef MAX_PEERS
#define MAX_PEERS 20
#endif

/**
 * @brief Two-button UI state for the Grantler dongle OLED.
 *
 * GPIO cursor moves the highlight / scroll. GPIO select short-press enters;
 * long-press goes back one level, or opens a peer context menu when a MAC
 * row is selected. From the status screen, either button opens the menu.
 *
 * List pages share one shape: title, optional Back as row 0, wrapping cursor,
 * long-press / Back row returns to the parent with that child still selected.
 * The root menu uses Close instead of Back. Add/Edit peer is a nibble editor,
 * not a list. Idle timeout (`MENU_IDLE_TIMEOUT_MS`, 0 to disable) returns home.
 *
 * `DongleT` is `enomik::Dongle` in firmware. Native tests pass a fake host.
 */
template <typename DongleT>
class Menu {
public:
  enum class Page : uint8_t {
    Status,
    Menu,
    Peers,
    AddPeer,
    PeerContext,
    Settings,
  };

  enum PeerContextItem : uint8_t {
    kPeerContextBack = 0,
    kPeerContextMute = 1,
    kPeerContextEdit = 2,
    kPeerContextDelete = 3,
  };

  enum SettingsItem : uint8_t {
    kSettingsBack = 0,
    kSettingsPowerSave = 1,
  };

  struct Entry {
    const char* label;
    Page page;
  };

  static constexpr Entry kEntries[] = {
    {"Close", Page::Status},
    {"Peers", Page::Peers},
    {"Settings", Page::Settings},
  };
  static constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
  static constexpr int kVisibleRows = 6;
  static constexpr int kPeerContextCount = 4;
  static constexpr int kSettingsCount = 2;

  explicit Menu(DongleT& dongle) : dongle_(dongle) {}

  Page page() const { return page_; }
  int cursor() const { return cursor_; }
  int scroll() const { return scroll_; }
  uint8_t addNibble() const { return addNibble_; }
  const uint8_t* addMac() const { return addMac_; }
  const uint8_t* contextMac() const { return contextMac_; }

  const char* title() const {
    if (page_ == Page::Menu) {
      return "MENU";
    }
    if (page_ == Page::Settings) {
      return "SETTINGS";
    }
    if (page_ == Page::AddPeer) {
      return editing_ ? "Edit peer" : "Add peer";
    }
    if (page_ == Page::Peers) {
      static char buf[24];
      snprintf(buf, sizeof(buf), "Peers %d/%d", dongle_.getPeersCount(), MAX_PEERS);
      return buf;
    }
    if (page_ == Page::PeerContext) {
      static char buf[18];
      snprintf(buf, sizeof(buf),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               contextMac_[0], contextMac_[1], contextMac_[2],
               contextMac_[3], contextMac_[4], contextMac_[5]);
      return buf;
    }
    return "";
  }

  int listCount() const {
    if (page_ == Page::Menu) {
      return kEntryCount;
    }
    if (page_ == Page::Settings) {
      return kSettingsCount;
    }
    if (page_ == Page::PeerContext) {
      return kPeerContextCount;
    }
    if (page_ == Page::Peers) {
      return peerListCount(dongle_.getPeersCount());
    }
    return 0;
  }

  const char* listLabel(int index) const {
    if (page_ == Page::Menu) {
      if (index < 0 || index >= kEntryCount) {
        return "";
      }
      return kEntries[index].label;
    }
    if (page_ == Page::Settings) {
      return settingsLabel(index);
    }
    if (page_ == Page::PeerContext) {
      return peerContextLabel(index);
    }
    if (page_ == Page::Peers) {
      return peerRowLabel(index);
    }
    return "";
  }

  void noteActivity() { lastActivityMs_ = millis(); }

  bool tick() {
    if (page_ == Page::Status || MENU_IDLE_TIMEOUT_MS == 0) {
      return false;
    }
    if ((millis() - lastActivityMs_) < MENU_IDLE_TIMEOUT_MS) {
      return false;
    }
    goTo(Page::Status, 0);
    return true;
  }

  const char* peerContextLabel(int index) const {
    if (index == kPeerContextBack) {
      return "Back";
    }
    if (index == kPeerContextMute) {
      return dongle_.isMuted(contextMac_) ? "Unmute" : "Mute";
    }
    if (index == kPeerContextEdit) {
      return "Edit";
    }
    if (index == kPeerContextDelete) {
      return "Delete";
    }
    return "";
  }

  const char* settingsLabel(int index) const {
    if (index == kSettingsBack) {
      return "Back";
    }
    if (index == kSettingsPowerSave) {
      return dongle_.isPowerSave() ? "Power save ON" : "Power save OFF";
    }
    return "";
  }

  static int peerListCount(int peerCount) { return 2 + peerCount; }

  void syncPeerList(int peerCount) {
    if (page_ != Page::Peers) {
      return;
    }
    const int count = peerListCount(peerCount);
    if (cursor_ >= count) {
      cursor_ = count - 1;
    }
    if (cursor_ < 0) {
      cursor_ = 0;
    }
    ensureVisible(count);
  }

  void onCursor() {
    if (page_ == Page::AddPeer) {
      incrementAddNibble();
      return;
    }
    const int n = listCount();
    if (n <= 0) {
      return;
    }
    cursor_ = (cursor_ + 1) % n;
    if (page_ == Page::Peers) {
      ensureVisible(n);
    }
  }

  void onEnter() {
    if (page_ == Page::Status) {
      goTo(Page::Menu, 0);
      return;
    }
    if (isBackRow()) {
      onBack();
      return;
    }
    if (page_ == Page::Menu && cursor_ >= 0 && cursor_ < kEntryCount) {
      goTo(kEntries[cursor_].page, 0);
    } else if (page_ == Page::Peers) {
      enterPeerListItem();
    } else if (page_ == Page::AddPeer) {
      enterMacEditor();
    } else if (page_ == Page::PeerContext) {
      enterPeerContextItem();
    } else if (page_ == Page::Settings) {
      enterSettingsItem();
    }
  }

  void onLongPress() {
    if (page_ == Page::Peers) {
      const int peerCount = dongle_.getPeersCount();
      if (cursor_ > 0 && cursor_ < peerListCount(peerCount) - 1) {
        const uint8_t* mac = dongle_.getPeer(cursor_ - 1);
        if (mac) {
          memcpy(contextMac_, mac, 6);
          savedCursor_ = cursor_;
          goTo(Page::PeerContext, 0);
        }
        return;
      }
    }
    onBack();
  }

  void onBack() {
    if (page_ == Page::PeerContext) {
      goTo(Page::Peers, savedCursor_);
      return;
    }
    if (page_ == Page::AddPeer) {
      const int row = editing_ ? savedCursor_ : peerListCount(dongle_.getPeersCount()) - 1;
      editing_ = false;
      goTo(Page::Peers, row);
    } else if (page_ == Page::Peers) {
      goTo(Page::Menu, indexOfPage(Page::Peers));
    } else if (page_ == Page::Settings) {
      goTo(Page::Menu, indexOfPage(Page::Settings));
    } else if (page_ == Page::Menu) {
      goTo(Page::Status, 0);
    }
  }

private:
  DongleT& dongle_;
  Page page_ = Page::Status;
  int cursor_ = 0;
  int scroll_ = 0;
  int savedCursor_ = 0;
  uint8_t addMac_[6] = {0};
  uint8_t addNibble_ = 0;
  uint8_t contextMac_[6] = {0};
  bool editing_ = false;
  uint32_t lastActivityMs_ = 0;

  bool isBackRow() const {
    return page_ != Page::Status && page_ != Page::Menu && page_ != Page::AddPeer &&
           cursor_ <= 0;
  }

  void goTo(Page page, int cursor) {
    page_ = page;
    cursor_ = cursor;
    if (page == Page::Peers) {
      const int count = peerListCount(dongle_.getPeersCount());
      if (cursor_ >= count) {
        cursor_ = count - 1;
      }
      if (cursor_ < 0) {
        cursor_ = 0;
      }
      ensureVisible(count);
    } else {
      scroll_ = 0;
    }
  }

  void ensureVisible(int listCount) {
    if (cursor_ < scroll_) {
      scroll_ = cursor_;
    }
    const int lastVisible = scroll_ + kVisibleRows - 1;
    if (cursor_ > lastVisible) {
      scroll_ = cursor_ - kVisibleRows + 1;
    }
    const int maxScroll = listCount > kVisibleRows ? listCount - kVisibleRows : 0;
    if (scroll_ > maxScroll) {
      scroll_ = maxScroll;
    }
    if (scroll_ < 0) {
      scroll_ = 0;
    }
  }

  static int indexOfPage(Page page) {
    for (int i = 0; i < kEntryCount; ++i) {
      if (kEntries[i].page == page) {
        return i;
      }
    }
    return 0;
  }

  const char* peerRowLabel(int index) const {
    const int count = peerListCount(dongle_.getPeersCount());
    if (index <= 0) {
      return "Back";
    }
    if (index >= count - 1) {
      return "Add peer";
    }
    const uint8_t* mac = dongle_.getPeer(index - 1);
    if (!mac) {
      return "";
    }
    static char line[20];
    snprintf(line, sizeof(line),
             "%s%02X:%02X:%02X:%02X:%02X:%02X",
             dongle_.isMuted(mac) ? "M " : "",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    return line;
  }

  int peerRowForMac(const uint8_t mac[6]) const {
    const int n = dongle_.getPeersCount();
    for (int i = 0; i < n; ++i) {
      const uint8_t* peer = dongle_.getPeer(i);
      if (peer && memcmp(peer, mac, 6) == 0) {
        return i + 1;
      }
    }
    return savedCursor_;
  }

  void openMacEditor(const uint8_t mac[6], bool editing) {
    if (mac) {
      memcpy(addMac_, mac, 6);
    } else {
      memset(addMac_, 0, sizeof(addMac_));
    }
    addNibble_ = 0;
    editing_ = editing;
    goTo(Page::AddPeer, 0);
  }

  void enterMacEditor() {
    if (addNibble_ < 11) {
      addNibble_++;
      return;
    }
    if (editing_) {
      if (dongle_.replacePeer(contextMac_, addMac_)) {
        const int row = peerRowForMac(addMac_);
        editing_ = false;
        goTo(Page::Peers, row);
      }
      return;
    }
    if (dongle_.addPeer(addMac_)) {
      goTo(Page::Peers, dongle_.getPeersCount());
    }
  }

  void enterPeerListItem() {
    const int listCount = peerListCount(dongle_.getPeersCount());
    if (cursor_ >= listCount - 1) {
      openMacEditor(nullptr, false);
    }
  }

  void enterPeerContextItem() {
    if (cursor_ == kPeerContextMute) {
      dongle_.setMuted(contextMac_, !dongle_.isMuted(contextMac_));
      return;
    }
    if (cursor_ == kPeerContextEdit) {
      openMacEditor(contextMac_, true);
      return;
    }
    if (cursor_ == kPeerContextDelete) {
      dongle_.removePeer(contextMac_);
      goTo(Page::Peers, savedCursor_);
    }
  }

  void enterSettingsItem() {
    if (cursor_ == kSettingsPowerSave) {
      dongle_.setPowerSave(!dongle_.isPowerSave());
    }
  }

  void incrementAddNibble() {
    const uint8_t byteIndex = addNibble_ / 2;
    const bool high = (addNibble_ % 2) == 0;
    const uint8_t value = addMac_[byteIndex];
    if (high) {
      const uint8_t nibble = ((value >> 4) + 1) & 0x0F;
      addMac_[byteIndex] = static_cast<uint8_t>((nibble << 4) | (value & 0x0F));
    } else {
      const uint8_t nibble = ((value & 0x0F) + 1) & 0x0F;
      addMac_[byteIndex] = static_cast<uint8_t>((value & 0xF0) | nibble);
    }
  }
};
