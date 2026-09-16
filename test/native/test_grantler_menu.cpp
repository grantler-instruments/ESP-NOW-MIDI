#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "arduino_stubs.h"
#include "examples/grantler_instruments/dongle/Menu.h"
#include "include/PeerMuteList.h"

namespace {

constexpr uint8_t kMacA[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
constexpr uint8_t kMacB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

struct FakeDongle
{
    static constexpr int kMax = MAX_PEERS;
    uint8_t peers[kMax][6]{};
    int count = 0;
    enomik::PeerMuteList mutes;
    bool powerSave = false;

    int getPeersCount() const { return count; }

    const uint8_t *getPeer(int index) const
    {
        if (index < 0 || index >= count)
        {
            return nullptr;
        }
        return peers[index];
    }

    bool addPeer(const uint8_t mac[6])
    {
        if (!mac || count >= kMax)
        {
            return false;
        }
        for (int i = 0; i < count; i++)
        {
            if (memcmp(peers[i], mac, 6) == 0)
            {
                return true;
            }
        }
        memcpy(peers[count++], mac, 6);
        return true;
    }

    bool removePeer(const uint8_t mac[6])
    {
        if (!mac)
        {
            return false;
        }
        mutes.clear(mac);
        for (int i = 0; i < count; i++)
        {
            if (memcmp(peers[i], mac, 6) != 0)
            {
                continue;
            }
            for (int j = i; j < count - 1; j++)
            {
                memcpy(peers[j], peers[j + 1], 6);
            }
            count--;
            return true;
        }
        return false;
    }

    bool replacePeer(const uint8_t oldMac[6], const uint8_t newMac[6])
    {
        if (!oldMac || !newMac)
        {
            return false;
        }
        if (memcmp(oldMac, newMac, 6) == 0)
        {
            return true;
        }
        if (!hasPeer(oldMac) || hasPeer(newMac))
        {
            return false;
        }
        const bool muted = isMuted(oldMac);
        if (!removePeer(oldMac))
        {
            return false;
        }
        if (!addPeer(newMac))
        {
            return false;
        }
        if (muted)
        {
            setMuted(newMac, true);
        }
        return true;
    }

    bool setMuted(const uint8_t mac[6], bool muted)
    {
        if (!mac || !hasPeer(mac))
        {
            return false;
        }
        return mutes.set(mac, muted);
    }

    bool isMuted(const uint8_t mac[6]) const { return mutes.contains(mac); }

    bool isPowerSave() const { return powerSave; }
    void setPowerSave(bool enabled) { powerSave = enabled; }

    bool hasPeer(const uint8_t mac[6]) const
    {
        if (!mac)
        {
            return false;
        }
        for (int i = 0; i < count; i++)
        {
            if (memcmp(peers[i], mac, 6) == 0)
            {
                return true;
            }
        }
        return false;
    }
};

using TestMenu = Menu<FakeDongle>;

void openMenu(TestMenu &menu)
{
    millisNow() = 0;
    menu.noteActivity();
    menu.onEnter();
}

void openPeers(TestMenu &menu)
{
    openMenu(menu);
    menu.noteActivity();
    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
}

void openSettings(TestMenu &menu)
{
    openMenu(menu);
    menu.noteActivity();
    menu.onCursor();
    menu.noteActivity();
    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
}

} // namespace

TEST_CASE("status enter opens the root menu", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    REQUIRE(menu.page() == TestMenu::Page::Status);
    openMenu(menu);
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(menu.cursor() == 0);
}

TEST_CASE("root menu Close returns to status", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openMenu(menu);
    REQUIRE(TestMenu::kEntries[0].page == TestMenu::Page::Status);
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Status);
}

TEST_CASE("root menu enter opens the peers list with Back and Add peer", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openPeers(menu);
    REQUIRE(menu.page() == TestMenu::Page::Peers);
    REQUIRE(TestMenu::peerListCount(0) == 2);
    REQUIRE(menu.cursor() == 0);

    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(TestMenu::kEntries[menu.cursor()].page == TestMenu::Page::Peers);
}

TEST_CASE("peers list cursor wraps and Add peer opens the editor", "[grantler][menu]")
{
    FakeDongle dongle;
    REQUIRE(dongle.addPeer(kMacA));
    TestMenu menu(dongle);

    openPeers(menu);
    REQUIRE(TestMenu::peerListCount(1) == 3);

    menu.onCursor();
    REQUIRE(menu.cursor() == 1);
    menu.onCursor();
    REQUIRE(menu.cursor() == 2);
    menu.onCursor();
    REQUIRE(menu.cursor() == 0);

    menu.onCursor();
    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::AddPeer);
    REQUIRE(menu.addNibble() == 0);
}

TEST_CASE("add-peer editor increments a nibble and saves the MAC", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openPeers(menu);
    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::AddPeer);

    menu.onCursor();
    REQUIRE(menu.addMac()[0] == 0x10);

    for (int i = 0; i < 11; i++)
    {
        menu.noteActivity();
        menu.onEnter();
    }
    REQUIRE(menu.addNibble() == 11);

    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Peers);
    REQUIRE(dongle.count == 1);
    REQUIRE(dongle.peers[0][0] == 0x10);
    REQUIRE(menu.cursor() == 1);
}

TEST_CASE("long-press on a MAC opens a context menu that can mute and delete", "[grantler][menu]")
{
    FakeDongle dongle;
    REQUIRE(dongle.addPeer(kMacA));
    REQUIRE(dongle.addPeer(kMacB));
    TestMenu menu(dongle);

    openPeers(menu);
    menu.onCursor();
    REQUIRE(menu.cursor() == 1);

    menu.noteActivity();
    menu.onLongPress();
    REQUIRE(menu.page() == TestMenu::Page::PeerContext);
    REQUIRE(memcmp(menu.contextMac(), kMacA, 6) == 0);
    REQUIRE(menu.cursor() == 0);
    REQUIRE(strcmp(menu.listLabel(0), "Back") == 0);
    REQUIRE(strcmp(menu.listLabel(1), "Mute") == 0);

    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(dongle.isMuted(kMacA));
    REQUIRE_FALSE(dongle.isMuted(kMacB));
    REQUIRE(strcmp(menu.listLabel(1), "Unmute") == 0);
    REQUIRE(menu.page() == TestMenu::Page::PeerContext);

    menu.onCursor();
    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Peers);
    REQUIRE_FALSE(dongle.hasPeer(kMacA));
    REQUIRE(dongle.hasPeer(kMacB));
    REQUIRE_FALSE(dongle.isMuted(kMacA));
}

TEST_CASE("peer context Back returns to the same MAC row", "[grantler][menu]")
{
    FakeDongle dongle;
    REQUIRE(dongle.addPeer(kMacA));
    TestMenu menu(dongle);

    openPeers(menu);
    menu.onCursor();
    REQUIRE(menu.cursor() == 1);
    menu.noteActivity();
    menu.onLongPress();
    REQUIRE(menu.page() == TestMenu::Page::PeerContext);

    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Peers);
    REQUIRE(menu.cursor() == 1);
}

TEST_CASE("peer context Edit opens the nibble editor and replaces the MAC", "[grantler][menu]")
{
    FakeDongle dongle;
    REQUIRE(dongle.addPeer(kMacA));
    REQUIRE(dongle.addPeer(kMacB));
    REQUIRE(dongle.setMuted(kMacA, true));
    TestMenu menu(dongle);

    openPeers(menu);
    menu.onCursor();
    menu.noteActivity();
    menu.onLongPress();
    REQUIRE(menu.page() == TestMenu::Page::PeerContext);

    menu.onCursor();
    menu.onCursor();
    REQUIRE(strcmp(menu.listLabel(menu.cursor()), "Edit") == 0);
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::AddPeer);
    REQUIRE(strcmp(menu.title(), "Edit peer") == 0);
    REQUIRE(memcmp(menu.addMac(), kMacA, 6) == 0);

    menu.onCursor();
    REQUIRE(menu.addMac()[0] == 0x21);
    for (int i = 0; i < 11; i++)
    {
        menu.noteActivity();
        menu.onEnter();
    }
    menu.noteActivity();
    menu.onEnter();

    uint8_t edited[6] = {0x21, 0x22, 0x33, 0x44, 0x55, 0x66};
    REQUIRE(menu.page() == TestMenu::Page::Peers);
    REQUIRE(dongle.hasPeer(edited));
    REQUIRE_FALSE(dongle.hasPeer(kMacA));
    REQUIRE(dongle.hasPeer(kMacB));
    REQUIRE(dongle.isMuted(edited));
    REQUIRE_FALSE(dongle.isMuted(kMacA));
    REQUIRE(menu.cursor() == 2);
}

TEST_CASE("long-press on Back goes up a level", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openPeers(menu);
    menu.noteActivity();
    menu.onLongPress();
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(TestMenu::kEntries[menu.cursor()].page == TestMenu::Page::Peers);
}

TEST_CASE("idle timeout returns to status after MENU_IDLE_TIMEOUT_MS", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openPeers(menu);
    millisNow() = MENU_IDLE_TIMEOUT_MS - 1;
    REQUIRE_FALSE(menu.tick());
    REQUIRE(menu.page() == TestMenu::Page::Peers);

    millisNow() = MENU_IDLE_TIMEOUT_MS;
    REQUIRE(menu.tick());
    REQUIRE(menu.page() == TestMenu::Page::Status);

    millisNow() = MENU_IDLE_TIMEOUT_MS + 1000;
    REQUIRE_FALSE(menu.tick());
}

TEST_CASE("button activity resets the idle timeout", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openMenu(menu);
    millisNow() = MENU_IDLE_TIMEOUT_MS - 1;
    menu.noteActivity();
    millisNow() = MENU_IDLE_TIMEOUT_MS + 1000;
    REQUIRE_FALSE(menu.tick());
    REQUIRE(menu.page() == TestMenu::Page::Menu);
}

TEST_CASE("peers list scroll keeps the cursor visible", "[grantler][menu]")
{
    FakeDongle dongle;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 10; i++)
    {
        mac[5] = static_cast<uint8_t>(i + 1);
        REQUIRE(dongle.addPeer(mac));
    }
    TestMenu menu(dongle);

    openPeers(menu);
    REQUIRE(menu.scroll() == 0);
    for (int i = 0; i < 8; i++)
    {
        menu.onCursor();
    }
    REQUIRE(menu.cursor() == 8);
    REQUIRE(menu.scroll() == menu.cursor() - TestMenu::kVisibleRows + 1);
}

TEST_CASE("settings page toggles power save and Back returns to menu", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openSettings(menu);
    REQUIRE(menu.page() == TestMenu::Page::Settings);
    REQUIRE(menu.cursor() == 0);
    REQUIRE_FALSE(dongle.isPowerSave());
    REQUIRE(strcmp(menu.settingsLabel(1), "Power save OFF") == 0);

    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(dongle.isPowerSave());
    REQUIRE(strcmp(menu.settingsLabel(1), "Power save ON") == 0);
    REQUIRE(menu.page() == TestMenu::Page::Settings);

    menu.onCursor();
    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(menu.cursor() == 2);
    REQUIRE(TestMenu::kEntries[menu.cursor()].page == TestMenu::Page::Settings);
}

TEST_CASE("settings Back and long-press return to the Settings row", "[grantler][menu]")
{
    FakeDongle dongle;
    TestMenu menu(dongle);

    openSettings(menu);
    REQUIRE(menu.page() == TestMenu::Page::Settings);
    REQUIRE(menu.cursor() == 0);

    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(menu.cursor() == 2);
    REQUIRE(TestMenu::kEntries[menu.cursor()].page == TestMenu::Page::Settings);

    menu.noteActivity();
    menu.onEnter();
    REQUIRE(menu.page() == TestMenu::Page::Settings);

    menu.noteActivity();
    menu.onLongPress();
    REQUIRE(menu.page() == TestMenu::Page::Menu);
    REQUIRE(menu.cursor() == 2);
}
