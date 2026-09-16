#pragma once

#include <cstdint>
#include <cstring>
#include "./config.h"

/**
 * @brief Two-button UI state for the Grantler dongle OLED.
 *
 * GPIO cursor moves the highlight / scroll. GPIO select short-press enters;
 * long-press goes back one level, or opens a peer context menu when a MAC
 * row is selected. From the status screen, either button opens the menu.
 *
 * Peers list rows: Back, then MACs, then Add peer. Add peer opens a nibble
 * editor (cursor increments the highlighted digit, enter advances / saves).
 * Long-press on a MAC opens a context menu (Mute/Unmute, Delete).
 * Idle timeout (`MENU_IDLE_TIMEOUT_MS`, 0 to disable) returns to status.
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
  };

  enum PeerContextItem : uint8_t {
    kPeerContextMute = 0,
    kPeerContextDelete = 1,
  };

  struct Entry {
    const char* label;
    Page page;
  };

  static constexpr Entry kEntries[] = {
    {"Peers", Page::Peers},
  };
  static constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
  static constexpr int kVisibleRows = 7;
  static constexpr int kPeerContextCount = 2;

  explicit Menu(DongleT& dongle) : dongle_(dongle) {}

  Page page() const { return page_; }
  int cursor() const { return cursor_; }
  int scroll() const { return scroll_; }
  uint8_t addNibble() const { return addNibble_; }
  const uint8_t* addMac() const { return addMac_; }
  int contextCursor() const { return contextCursor_; }
  const uint8_t* contextMac() const { return contextMac_; }

  void noteActivity() { lastActivityMs_ = millis(); }

  bool tick() {
    if (page_ == Page::Status || MENU_IDLE_TIMEOUT_MS == 0) {
      return false;
    }
    if ((millis() - lastActivityMs_) < MENU_IDLE_TIMEOUT_MS) {
      return false;
    }
    page_ = Page::Status;
    cursor_ = 0;
    scroll_ = 0;
    return true;
  }

  const char* peerContextLabel(int index) const {
    if (index == kPeerContextMute) {
      return dongle_.isMuted(contextMac_) ? "Unmute" : "Mute";
    }
    if (index == kPeerContextDelete) {
      return "Delete";
    }
    return "";
  }

  static int peerListCount(int peerCount) { return 2 + peerCount; }

  void syncPeerList(int peerCount) {
    if (page_ != Page::Peers) {
      return;
    }
    const int listCount = peerListCount(peerCount);
    if (cursor_ >= listCount) {
      cursor_ = listCount - 1;
    }
    if (cursor_ < 0) {
      cursor_ = 0;
    }
    ensureVisible(listCount);
  }

  void onCursor() {
    if (page_ == Page::Menu) {
      cursor_ = (cursor_ + 1) % kEntryCount;
    } else if (page_ == Page::Peers) {
      const int listCount = peerListCount(dongle_.getPeersCount());
      cursor_ = (cursor_ + 1) % listCount;
      ensureVisible(listCount);
    } else if (page_ == Page::AddPeer) {
      incrementAddNibble();
    } else if (page_ == Page::PeerContext) {
      contextCursor_ = (contextCursor_ + 1) % kPeerContextCount;
    }
  }

  void onEnter() {
    if (page_ == Page::Status) {
      page_ = Page::Menu;
      cursor_ = 0;
    } else if (page_ == Page::Menu && cursor_ >= 0 && cursor_ < kEntryCount) {
      page_ = kEntries[cursor_].page;
      cursor_ = 0;
      scroll_ = 0;
    } else if (page_ == Page::Peers) {
      enterPeerListItem();
    } else if (page_ == Page::AddPeer) {
      if (addNibble_ < 11) {
        addNibble_++;
      } else if (dongle_.addPeer(addMac_)) {
        page_ = Page::Peers;
        cursor_ = dongle_.getPeersCount();
        scroll_ = 0;
        ensureVisible(peerListCount(dongle_.getPeersCount()));
      }
    } else if (page_ == Page::PeerContext) {
      enterPeerContextItem();
    }
  }

  void onLongPress() {
    if (page_ == Page::Peers) {
      const int peerCount = dongle_.getPeersCount();
      if (cursor_ > 0 && cursor_ < peerListCount(peerCount) - 1) {
        const uint8_t* mac = dongle_.getPeer(cursor_ - 1);
        if (mac) {
          memcpy(contextMac_, mac, 6);
          contextCursor_ = 0;
          page_ = Page::PeerContext;
        }
        return;
      }
    }
    onBack();
  }

  void onBack() {
    if (page_ == Page::PeerContext) {
      page_ = Page::Peers;
      return;
    }
    if (page_ == Page::AddPeer) {
      page_ = Page::Peers;
      cursor_ = peerListCount(dongle_.getPeersCount()) - 1;
      scroll_ = 0;
      ensureVisible(peerListCount(dongle_.getPeersCount()));
    } else if (page_ == Page::Peers) {
      page_ = Page::Menu;
    } else if (page_ == Page::Menu) {
      page_ = Page::Status;
    }
  }

private:
  DongleT& dongle_;
  Page page_ = Page::Status;
  int cursor_ = 0;
  int scroll_ = 0;
  uint8_t addMac_[6] = {0};
  uint8_t addNibble_ = 0;
  uint8_t contextMac_[6] = {0};
  int contextCursor_ = 0;
  uint32_t lastActivityMs_ = 0;

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

  void enterPeerListItem() {
    const int peerCount = dongle_.getPeersCount();
    const int listCount = peerListCount(peerCount);
    if (cursor_ <= 0) {
      page_ = Page::Menu;
      return;
    }
    if (cursor_ >= listCount - 1) {
      memset(addMac_, 0, sizeof(addMac_));
      addNibble_ = 0;
      page_ = Page::AddPeer;
    }
  }

  void enterPeerContextItem() {
    if (contextCursor_ == kPeerContextMute) {
      dongle_.setMuted(contextMac_, !dongle_.isMuted(contextMac_));
      return;
    }
    if (contextCursor_ == kPeerContextDelete) {
      dongle_.removePeer(contextMac_);
      page_ = Page::Peers;
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
