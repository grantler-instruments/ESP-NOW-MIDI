// Waveshare ESP32-S3-AUDIO-Board backend. See enomik_pd_waveshare_s3_audio.h.
// Pins and addresses: Waveshare schematic v1.1 and demo code.
#include "enomik_pd_waveshare_s3_audio.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "enomik_pd_waveshare";

static constexpr int kChannels = 2;

namespace pins
{
    constexpr gpio_num_t kMclk = GPIO_NUM_12;
    constexpr gpio_num_t kBclk = GPIO_NUM_13;
    constexpr gpio_num_t kWs = GPIO_NUM_14;
    constexpr gpio_num_t kDin = GPIO_NUM_15;  // from ES7210
    constexpr gpio_num_t kDout = GPIO_NUM_16; // to ES8311
    constexpr gpio_num_t kSda = GPIO_NUM_11;
    constexpr gpio_num_t kScl = GPIO_NUM_10;
}

// TCA9555: port 1 holds EXIO8..15. EXIO8 = PA_EN, EXIO9..11 = keys (inputs).
// Port 0 is never written: EXIO6/7 switch the USB/camera mux, and driving
// the wrong one breaks USB flashing.
namespace tca9555
{
    constexpr uint8_t kAddr = 0x20;
    constexpr uint8_t kRegOutput1 = 0x03;
    constexpr uint8_t kRegConfig1 = 0x07; // 1 = input
    constexpr uint8_t kPaEnBit = 1 << 0;  // EXIO8
}

static i2s_chan_handle_t newI2s(int sampleRate, int framesPerBuffer, i2s_chan_handle_t *rx)
{
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.dma_frame_num = framesPerBuffer;
    chanCfg.dma_desc_num = 3;
    chanCfg.auto_clear = true; // underrun plays silence
    esp_err_t err = i2s_new_channel(&chanCfg, &tx, rx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return nullptr;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sampleRate)),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = pins::kMclk,
            .bclk = pins::kBclk,
            .ws = pins::kWs,
            .dout = pins::kDout,
            .din = pins::kDin,
            .invert_flags = {},
        },
    };
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if ((err = i2s_channel_init_std_mode(tx, &stdCfg)) != ESP_OK ||
        (err = i2s_channel_init_std_mode(*rx, &stdCfg)) != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S std mode: %s", esp_err_to_name(err));
        return nullptr;
    }
    return tx;
}

namespace enomik
{
    namespace pd
    {
        bool WaveshareS3AudioBackend::setAmpEnabled(bool on)
        {
            if (!_i2c)
                return false;
            i2c_device_config_t dev = {};
            dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev.device_address = tca9555::kAddr;
            dev.scl_speed_hz = 100000;
            i2c_master_dev_handle_t h = nullptr;
            if (i2c_master_bus_add_device(static_cast<i2c_master_bus_handle_t>(_i2c), &dev, &h) != ESP_OK)
                return false;

            // Read-modify-write so the key inputs and expansion pins keep their state.
            uint8_t reg = tca9555::kRegOutput1, out = 0xFF, cfg = 0xFF;
            bool ok = i2c_master_transmit_receive(h, &reg, 1, &out, 1, 100) == ESP_OK;
            reg = tca9555::kRegConfig1;
            ok = ok && i2c_master_transmit_receive(h, &reg, 1, &cfg, 1, 100) == ESP_OK;
            if (ok)
            {
                out = on ? (out | tca9555::kPaEnBit) : (out & ~tca9555::kPaEnBit);
                cfg &= ~tca9555::kPaEnBit; // EXIO8 output
                // Output level first, then direction: no glitch on the amp enable.
                const uint8_t writeOut[2] = {tca9555::kRegOutput1, out};
                const uint8_t writeCfg[2] = {tca9555::kRegConfig1, cfg};
                ok = i2c_master_transmit(h, writeOut, 2, 100) == ESP_OK &&
                     i2c_master_transmit(h, writeCfg, 2, 100) == ESP_OK;
            }
            i2c_master_bus_rm_device(h);
            if (!ok)
                ESP_LOGE(TAG, "TCA9555 (0x20) not responding: amp state unchanged");
            return ok;
        }

        bool WaveshareS3AudioBackend::begin(int sampleRate, int framesPerBuffer)
        {
            _bufFrames = framesPerBuffer;
            _buf = static_cast<int16_t *>(heap_caps_calloc(framesPerBuffer * kChannels, sizeof(int16_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (!_buf)
                return false;

            // I2C bus (codecs + expander)
            i2c_master_bus_config_t busCfg = {};
            busCfg.i2c_port = I2C_NUM_0;
            busCfg.sda_io_num = pins::kSda;
            busCfg.scl_io_num = pins::kScl;
            busCfg.clk_source = I2C_CLK_SRC_DEFAULT;
            busCfg.glitch_ignore_cnt = 7;
            busCfg.flags.enable_internal_pullup = true;
            i2c_master_bus_handle_t bus = nullptr;
            esp_err_t err = i2c_new_master_bus(&busCfg, &bus);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "I2C bus: %s", esp_err_to_name(err));
                return false;
            }
            _i2c = bus;

            // One full-duplex I2S controller: the codecs share BCLK/WS/MCLK.
            i2s_chan_handle_t rx = nullptr;
            i2s_chan_handle_t tx = newI2s(sampleRate, framesPerBuffer, &rx);
            if (!tx)
                return false;

            audio_codec_i2s_cfg_t i2sCfg = {};
            i2sCfg.port = I2S_NUM_0;
            i2sCfg.rx_handle = rx;
            i2sCfg.tx_handle = tx;
            const audio_codec_data_if_t *dataIf = audio_codec_new_i2s_data(&i2sCfg);

            // ES8311: DAC only, MCLK from the ESP32, amp via the TCA9555.
            audio_codec_i2c_cfg_t dacI2c = {};
            dacI2c.port = I2C_NUM_0;
            dacI2c.addr = ES8311_CODEC_DEFAULT_ADDR;
            dacI2c.bus_handle = bus;
            es8311_codec_cfg_t dacCfg = {};
            dacCfg.ctrl_if = audio_codec_new_i2c_ctrl(&dacI2c);
            dacCfg.gpio_if = audio_codec_new_gpio();
            dacCfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
            dacCfg.pa_pin = -1;
            dacCfg.use_mclk = true;
            dacCfg.hw_gain.pa_voltage = 5.0f;
            dacCfg.hw_gain.codec_dac_voltage = 3.3f;
            const audio_codec_if_t *dac = es8311_codec_new(&dacCfg);

            // ES7210: the two real microphones (MIC3 is the speaker loopback).
            audio_codec_i2c_cfg_t adcI2c = {};
            adcI2c.port = I2C_NUM_0;
            adcI2c.addr = ES7210_CODEC_DEFAULT_ADDR;
            adcI2c.bus_handle = bus;
            es7210_codec_cfg_t adcCfg = {};
            adcCfg.ctrl_if = audio_codec_new_i2c_ctrl(&adcI2c);
            adcCfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2;
            const audio_codec_if_t *adc = es7210_codec_new(&adcCfg);

            if (!dataIf || !dacCfg.ctrl_if || !dac || !adcCfg.ctrl_if || !adc)
            {
                ESP_LOGE(TAG, "codec setup failed (ES8311 0x18 / ES7210 0x40 on I2C?)");
                return false;
            }

            esp_codec_dev_cfg_t outCfg = {};
            outCfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
            outCfg.codec_if = dac;
            outCfg.data_if = dataIf;
            esp_codec_dev_handle_t speaker = esp_codec_dev_new(&outCfg);

            esp_codec_dev_cfg_t inCfg = {};
            inCfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
            inCfg.codec_if = adc;
            inCfg.data_if = dataIf;
            esp_codec_dev_handle_t mic = esp_codec_dev_new(&inCfg);

            esp_codec_dev_sample_info_t fs = {};
            fs.bits_per_sample = 16;
            fs.channel = kChannels;
            fs.sample_rate = static_cast<uint32_t>(sampleRate);
            fs.mclk_multiple = 256;

            if (!speaker || esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK)
            {
                ESP_LOGE(TAG, "ES8311 open failed");
                return false;
            }
            if (!mic || esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK)
            {
                ESP_LOGE(TAG, "ES7210 open failed");
                return false;
            }
            esp_codec_dev_set_out_vol(speaker, _volume);
            esp_codec_dev_set_in_gain(mic, static_cast<float>(_micGainDb));
            _speaker = speaker;
            _mic = mic;

            // Codec first, amp second (as Waveshare's own init): avoids the turn-on pop.
            vTaskDelay(pdMS_TO_TICKS(50));
            setAmpEnabled(true);

            ESP_LOGI(TAG, "ES8311 + ES7210 up: %d Hz, %d frames/buffer, volume %d, mic gain %d dB",
                     sampleRate, framesPerBuffer, _volume, _micGainDb);
            return true;
        }

        void WaveshareS3AudioBackend::read(float *dst, int frames)
        {
            const int n = frames * kChannels;
            if (!_mic || frames > _bufFrames ||
                esp_codec_dev_read(static_cast<esp_codec_dev_handle_t>(_mic), _buf, n * sizeof(int16_t)) != ESP_CODEC_DEV_OK)
            {
                memset(dst, 0, n * sizeof(float));
                return;
            }
            constexpr float kToFloat = 1.0f / 32768.0f;
            for (int i = 0; i < n; ++i)
                dst[i] = static_cast<float>(_buf[i]) * kToFloat;
        }

        void WaveshareS3AudioBackend::write(const float *src, int frames)
        {
            if (!_speaker || frames > _bufFrames)
                return;
            const int n = frames * kChannels;
            for (int i = 0; i < n; ++i)
            {
                float v = src[i];
                v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
                _buf[i] = static_cast<int16_t>(v * 32767.0f);
            }
            // Blocks on DMA: this is the clock.
            esp_codec_dev_write(static_cast<esp_codec_dev_handle_t>(_speaker), _buf, n * sizeof(int16_t));
        }
    }
}
