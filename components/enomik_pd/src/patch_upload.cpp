#include "patch_upload.h"

#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "enomik_pd_upload";

// MIDI File Dump (MMA): F0 7E <device> 07 <type> ... F7
static constexpr uint8_t kUniversalNonRealtime = 0x7E;
static constexpr uint8_t kFileDump = 0x07;
static constexpr uint8_t kHeader = 0x01;
static constexpr uint8_t kDataPacket = 0x02;
// Handshake: F0 7E <device> <subId> <packet> F7
static constexpr uint8_t kEof = 0x7B;
static constexpr uint8_t kCancel = 0x7D;
static constexpr uint8_t kNak = 0x7E;
static constexpr uint8_t kAck = 0x7F;
static constexpr uint8_t kAllCall = 0x7F;

static constexpr int64_t kTimeoutUs = 10 * 1000 * 1000;
static constexpr uint32_t kMaxFileSize = 1024 * 1024;
static constexpr unsigned kMaxNameLen = 40;

namespace enomik
{
    namespace pd
    {
        namespace detail
        {
            void PatchUpload::configure(const char *dir, const char *mainFile, uint8_t deviceId,
                                        SendFn send, ReloadFn reload)
            {
                _dir = dir;
                _mainFile = mainFile;
                _deviceId = deviceId & 0x7F;
                _send = send;
                _reload = reload;
            }

            void PatchUpload::sendHandshake(uint8_t subId, uint8_t packet)
            {
                if (!_send)
                    return;
                const uint8_t msg[6] = {0xF0, kUniversalNonRealtime, _deviceId, subId,
                                        static_cast<uint8_t>(packet & 0x7F), 0xF7};
                _send(msg, sizeof(msg));
            }

            bool PatchUpload::handle(const uint8_t *data, unsigned length)
            {
                if (!data || !_dir)
                    return false;
                // Body between F0 and F7 (both optional on input).
                const uint8_t *p = data;
                unsigned n = length;
                if (n && p[0] == 0xF0)
                {
                    ++p;
                    --n;
                }
                if (n && p[n - 1] == 0xF7)
                    --n;
                if (n < 3 || p[0] != kUniversalNonRealtime)
                    return false;

                const uint8_t device = p[1];
                const uint8_t subId = p[2];
                const bool fileDump = subId == kFileDump;
                const bool handshake = subId >= kEof; // EOF, WAIT, CANCEL, NAK, ACK
                if (!fileDump && !handshake)
                    return false; // other universal messages (identity request, ...) stay with Pd
                if (device != _deviceId && device != kAllCall)
                    return true; // for another client: consume silently

                _lastActivityUs = esp_timer_get_time();
                if (handshake)
                {
                    const uint8_t packet = n >= 4 ? p[3] : 0;
                    if (subId == kEof)
                        onEndOfFile(packet);
                    else if (subId == kCancel && _file)
                        abort("cancelled by sender");
                    // WAIT / ACK / NAK from the sender side: nothing to do
                    return true;
                }
                if (n < 4)
                    return true;
                switch (p[3])
                {
                case kHeader:
                    onHeader(p + 4, n - 4);
                    break;
                case kDataPacket:
                    onData(p, n);
                    break;
                default:
                    break; // dump request (0x03): not supported
                }
                return true;
            }

            // Header body: <sender> <type x4> <length x4, 7-bit LSB first> <name...>
            void PatchUpload::onHeader(const uint8_t *p, unsigned n)
            {
                if (_file)
                    abort("new header during transfer");
                if (n < 1 + 4 + 4 + 1)
                {
                    sendHandshake(kNak, 0);
                    return;
                }
                const uint32_t length = p[5] | (p[6] << 7) | (p[7] << 14) | (static_cast<uint32_t>(p[8]) << 21);

                // Name: plain file names only, no paths.
                unsigned len = 0;
                for (unsigned i = 9; i < n && len < sizeof(_name) - 1; ++i)
                {
                    const char c = static_cast<char>(p[i]);
                    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                    c == '.' || c == '_' || c == '-' || c == '~';
                    if (!ok || (len == 0 && c == '.'))
                    {
                        len = 0;
                        break;
                    }
                    _name[len++] = c;
                }
                _name[len] = '\0';
                if (len == 0 || n - 9 > kMaxNameLen || length > kMaxFileSize)
                {
                    ESP_LOGW(TAG, "rejected header (name or size %u)", (unsigned)length);
                    sendHandshake(kCancel, 0);
                    return;
                }

                const int finalLen = snprintf(_finalPath, sizeof(_finalPath), "%s/%s", _dir, _name);
                const int tmpLen = snprintf(_tmpPath, sizeof(_tmpPath), "%s/.%s.part", _dir, _name);
                if (finalLen >= static_cast<int>(sizeof(_finalPath)) || tmpLen >= static_cast<int>(sizeof(_tmpPath)))
                {
                    ESP_LOGW(TAG, "rejected %s: path too long", _name);
                    sendHandshake(kCancel, 0);
                    return;
                }
                _file = fopen(_tmpPath, "wb");
                if (!_file)
                {
                    ESP_LOGE(TAG, "cannot write %s", _tmpPath);
                    sendHandshake(kCancel, 0);
                    return;
                }
                _expected = length;
                _received = 0;
                _nextPacket = 0;
                ESP_LOGI(TAG, "receiving %s (%u bytes)", _name, (unsigned)length);
                sendHandshake(kAck, 0);
            }

            // p: body from 7E; p[4] packet, p[5] count-1, data, checksum
            void PatchUpload::onData(const uint8_t *p, unsigned n)
            {
                if (n < 7)
                    return;
                const uint8_t packet = p[4];
                if (!_file)
                {
                    sendHandshake(kCancel, packet);
                    return;
                }
                const unsigned count = p[5] + 1u;
                if (6 + count + 1 != n)
                {
                    sendHandshake(kNak, packet);
                    return;
                }
                uint8_t sum = 0;
                for (unsigned i = 0; i < 6 + count; ++i)
                    sum ^= p[i];
                if ((sum & 0x7F) != p[6 + count])
                {
                    ESP_LOGW(TAG, "packet %u: checksum mismatch", packet);
                    sendHandshake(kNak, packet);
                    return;
                }
                if (packet == ((_nextPacket - 1) & 0x7F) && _received > 0)
                {
                    sendHandshake(kAck, packet); // repeat (our ACK was lost): don't write twice
                    return;
                }
                if (packet != _nextPacket)
                {
                    sendHandshake(kNak, _nextPacket);
                    return;
                }

                // 7-in-8 decoding: each group starts with the MSBs of up to 7 bytes.
                uint8_t out[112];
                unsigned outLen = 0;
                const uint8_t *d = p + 6;
                for (unsigned g = 0; g < count; g += 8)
                {
                    const uint8_t msbs = d[g];
                    for (unsigned j = 1; j < 8 && g + j < count; ++j)
                        out[outLen++] = d[g + j] | (((msbs >> (7 - j)) & 1) << 7);
                }
                const uint32_t room = _expected - _received;
                if (outLen > room)
                    outLen = room; // padding in the last packet
                if (outLen && fwrite(out, 1, outLen, _file) != outLen)
                {
                    abort("write failed (patch partition full?)");
                    return;
                }
                _received += outLen;
                _nextPacket = (_nextPacket + 1) & 0x7F;
                sendHandshake(kAck, packet);
            }

            void PatchUpload::onEndOfFile(uint8_t packet)
            {
                if (!_file)
                    return;
                if (_received != _expected)
                {
                    ESP_LOGW(TAG, "%s: got %u of %u bytes", _name, (unsigned)_received, (unsigned)_expected);
                    abort("incomplete");
                    return;
                }
                fclose(_file);
                _file = nullptr;
                unlink(_finalPath);
                if (rename(_tmpPath, _finalPath) != 0)
                {
                    ESP_LOGE(TAG, "rename %s -> %s failed", _tmpPath, _finalPath);
                    unlink(_tmpPath);
                    sendHandshake(kCancel, packet);
                    return;
                }
                ESP_LOGI(TAG, "stored %s (%u bytes)", _finalPath, (unsigned)_received);
                sendHandshake(kAck, packet);
                if (_mainFile && strcmp(_name, _mainFile) == 0 && _reload)
                    _reload();
            }

            void PatchUpload::abort(const char *why)
            {
                ESP_LOGW(TAG, "upload of %s aborted: %s", _name, why);
                if (_file)
                {
                    fclose(_file);
                    _file = nullptr;
                    unlink(_tmpPath);
                }
                sendHandshake(kCancel, _nextPacket);
            }

            void PatchUpload::poll()
            {
                if (_file && esp_timer_get_time() - _lastActivityUs > kTimeoutUs)
                    abort("timeout");
            }
        }
    }
}
