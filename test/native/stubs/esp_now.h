#pragma once
// Host stub of the ESP-IDF ESP-NOW API for native tests. It keeps a peer
// registry like the real driver (duplicates, limit of 20) and esp_now_send()
// records every packet so tests can check the exact bytes on the wire.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <mutex>
#include <vector>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_ESPNOW_EXIST 0x306B
#define ESP_ERR_ESPNOW_FULL 0x3068
#define ESP_ERR_ESPNOW_NOT_FOUND 0x3069

typedef enum
{
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL,
} esp_now_send_status_t;

typedef struct
{
    int unused;
} wifi_tx_info_t;

typedef struct
{
    uint8_t *src_addr;
    uint8_t *des_addr;
    void *rx_ctrl;
} esp_now_recv_info_t;

typedef struct
{
    uint8_t peer_addr[6];
    uint8_t lmk[16];
    uint8_t channel;
    int ifidx;
    bool encrypt;
    void *priv;
} esp_now_peer_info_t;

struct StubEspNowPacket
{
    uint8_t dest[6];
    std::vector<uint8_t> data;
};

// Shared by every esp_now_* stub; a mutex makes it safe for threaded tests.
struct StubEspNow
{
    std::mutex mutex;
    std::vector<std::array<uint8_t, 6>> peers;
    std::vector<StubEspNowPacket> sent;
    bool record = true;
    bool keepOnInit = false; // esp_now_init() reports "already initialized"
};

inline StubEspNow &stubEspNow()
{
    static StubEspNow state;
    return state;
}

inline std::vector<StubEspNowPacket> &stubEspNowSent() { return stubEspNow().sent; }

inline size_t stubEspNowPeerCount()
{
    std::lock_guard<std::mutex> lock(stubEspNow().mutex);
    return stubEspNow().peers.size();
}

inline int stubEspNowFind(const uint8_t *mac)
{
    auto &peers = stubEspNow().peers;
    for (size_t i = 0; i < peers.size(); ++i)
    {
        if (memcmp(peers[i].data(), mac, 6) == 0)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Like a freshly booted chip: no peers registered, nothing sent.
inline esp_err_t esp_now_init()
{
    std::lock_guard<std::mutex> lock(stubEspNow().mutex);
    if (stubEspNow().keepOnInit)
    {
        return ESP_ERR_ESPNOW_EXIST;
    }
    stubEspNow().peers.clear();
    stubEspNow().sent.clear();
    return ESP_OK;
}

inline esp_err_t esp_now_send(const uint8_t *dest, const uint8_t *data, size_t len)
{
    std::lock_guard<std::mutex> lock(stubEspNow().mutex);
    if (stubEspNow().record)
    {
        StubEspNowPacket p;
        memcpy(p.dest, dest, 6);
        p.data.assign(data, data + len);
        stubEspNow().sent.push_back(p);
    }
    return ESP_OK;
}

inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t *info)
{
    std::lock_guard<std::mutex> lock(stubEspNow().mutex);
    if (stubEspNowFind(info->peer_addr) >= 0)
    {
        return ESP_ERR_ESPNOW_EXIST;
    }
    if (stubEspNow().peers.size() >= 20)
    {
        return ESP_ERR_ESPNOW_FULL;
    }
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), info->peer_addr, 6);
    stubEspNow().peers.push_back(mac);
    return ESP_OK;
}

inline esp_err_t esp_now_del_peer(const uint8_t *mac)
{
    std::lock_guard<std::mutex> lock(stubEspNow().mutex);
    const int i = stubEspNowFind(mac);
    if (i < 0)
    {
        return ESP_ERR_ESPNOW_NOT_FOUND;
    }
    stubEspNow().peers.erase(stubEspNow().peers.begin() + i);
    return ESP_OK;
}

inline esp_err_t esp_now_register_send_cb(void (*)(const wifi_tx_info_t *, esp_now_send_status_t)) { return ESP_OK; }
inline esp_err_t esp_now_register_recv_cb(void (*)(const esp_now_recv_info_t *, const uint8_t *, int)) { return ESP_OK; }
