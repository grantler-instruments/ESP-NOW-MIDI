#pragma once
// Host stub of the ESP-IDF ESP-NOW API for native tests. esp_now_send()
// records every packet so tests can check the exact bytes on the wire.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_ESPNOW_EXIST 0x3068

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

inline std::vector<StubEspNowPacket> &stubEspNowSent()
{
    static std::vector<StubEspNowPacket> sent;
    return sent;
}

inline esp_err_t esp_now_init() { return ESP_OK; }
inline esp_err_t esp_now_send(const uint8_t *dest, const uint8_t *data, size_t len)
{
    StubEspNowPacket p;
    memcpy(p.dest, dest, 6);
    p.data.assign(data, data + len);
    stubEspNowSent().push_back(p);
    return ESP_OK;
}
inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_OK; }
inline esp_err_t esp_now_del_peer(const uint8_t *) { return ESP_OK; }
inline esp_err_t esp_now_register_send_cb(void (*)(const wifi_tx_info_t *, esp_now_send_status_t)) { return ESP_OK; }
inline esp_err_t esp_now_register_recv_cb(void (*)(const esp_now_recv_info_t *, const uint8_t *, int)) { return ESP_OK; }
