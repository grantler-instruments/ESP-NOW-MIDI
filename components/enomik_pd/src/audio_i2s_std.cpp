#include "enomik_pd_i2s.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "enomik_pd_i2s";

static constexpr int kChannels = 2;
static constexpr float kToFloat = 1.0f / 2147483648.0f;
static constexpr float kToInt = 2147483520.0f; // largest float below 2^31

static gpio_num_t gpio(int n) { return n < 0 ? I2S_GPIO_UNUSED : static_cast<gpio_num_t>(n); }

namespace enomik
{
    namespace pd
    {
        bool I2sStdBackend::begin(int sampleRate, int framesPerBuffer)
        {
            const bool wantTx = _pins.dout >= 0;
            const bool wantRx = _pins.din >= 0;
            if (!wantTx && !wantRx)
            {
                ESP_LOGW(TAG, "no I2S data pins configured");
                return false;
            }

            i2s_chan_handle_t tx = nullptr, rx = nullptr;
            i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(_port, I2S_ROLE_MASTER);
            chanCfg.dma_frame_num = framesPerBuffer;
            chanCfg.dma_desc_num = 3;
            chanCfg.auto_clear = true; // underrun plays silence instead of looping old data
            esp_err_t err = i2s_new_channel(&chanCfg, wantTx ? &tx : nullptr, wantRx ? &rx : nullptr);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
                return false;
            }

            i2s_std_config_t stdCfg = {
                .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sampleRate)),
                .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
                .gpio_cfg = {
                    .mclk = gpio(_pins.mclk),
                    .bclk = gpio(_pins.bclk),
                    .ws = gpio(_pins.ws),
                    .dout = gpio(_pins.dout),
                    .din = gpio(_pins.din),
                    .invert_flags = {},
                },
            };
            stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

            if ((tx && (err = i2s_channel_init_std_mode(tx, &stdCfg)) != ESP_OK) ||
                (rx && (err = i2s_channel_init_std_mode(rx, &stdCfg)) != ESP_OK))
            {
                ESP_LOGE(TAG, "I2S std mode: %s", esp_err_to_name(err));
                return false;
            }

            _bufFrames = framesPerBuffer;
            _buf = static_cast<int32_t *>(heap_caps_calloc(framesPerBuffer * kChannels, sizeof(int32_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (!_buf)
            {
                ESP_LOGE(TAG, "buffer alloc failed");
                return false;
            }

            if (tx)
            {
                // Preload silence so the first write does not underrun.
                size_t loaded = 0;
                for (int i = 0; i < static_cast<int>(chanCfg.dma_desc_num); ++i)
                    i2s_channel_preload_data(tx, _buf, framesPerBuffer * kChannels * sizeof(int32_t), &loaded);
                i2s_channel_enable(tx);
            }
            if (rx)
                i2s_channel_enable(rx);

            _tx = tx;
            _rx = rx;
            ESP_LOGI(TAG, "I2S%d %d Hz, %d frames/buffer, out=%s in=%s", _port, sampleRate, framesPerBuffer,
                     tx ? "on" : "off", rx ? "on" : "off");
            return true;
        }

        void I2sStdBackend::read(float *dst, int frames)
        {
            const int n = frames * kChannels;
            if (!_rx || frames > _bufFrames)
            {
                memset(dst, 0, n * sizeof(float));
                return;
            }
            size_t got = 0;
            i2s_channel_read(static_cast<i2s_chan_handle_t>(_rx), _buf, n * sizeof(int32_t), &got, portMAX_DELAY);
            const int gotSamples = static_cast<int>(got / sizeof(int32_t));
            for (int i = 0; i < gotSamples; ++i)
                dst[i] = static_cast<float>(_buf[i]) * kToFloat;
            for (int i = gotSamples; i < n; ++i)
                dst[i] = 0.0f;
        }

        void I2sStdBackend::write(const float *src, int frames)
        {
            if (!_tx || frames > _bufFrames)
                return;
            const int n = frames * kChannels;
            for (int i = 0; i < n; ++i)
            {
                float v = src[i];
                v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
                _buf[i] = static_cast<int32_t>(v * kToInt);
            }
            size_t written = 0;
            i2s_channel_write(static_cast<i2s_chan_handle_t>(_tx), _buf, n * sizeof(int32_t), &written, portMAX_DELAY);
        }
    }
}
