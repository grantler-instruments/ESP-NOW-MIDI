/**
 * @file enomik_pd_waveshare_s3_audio.h
 * @brief enomik_pd audio backend for the Waveshare ESP32-S3-AUDIO-Board.
 *
 *   ES8311 (DAC, I2C 0x18) -> NS4150B amp -> speaker   <- [dac~ 1 2]
 *   ES7210 (ADC, I2C 0x40) <- MIC1 / MIC2               -> [adc~ 1 2]
 *   TCA9555 (I2C 0x20) EXIO8 = amp enable (active high)
 *
 * Both codecs share one I2S clock pair (MCLK 12, BCLK 13, WS 14, DIN 15,
 * DOUT 16); the ESP32 is master, 16 bit. I2C on SDA 11 / SCL 10. Codec
 * registers via espressif/esp_codec_dev (chip drivers, no board BSP).
 * Only TCA9555 port 1 is written: EXIO6/7 on port 0 switch the USB/camera mux.
 *
 * ESP32-S3 only.
 */
#pragma once

#include "enomik_pd.h"

#include <stdint.h>

namespace enomik
{
    namespace pd
    {
        class WaveshareS3AudioBackend : public AudioBackend
        {
        public:
            /**
             * @param speakerVolume ES8311 output volume, 0-100
             * @param micGainDb     ES7210 input gain, 0-37 dB
             */
            explicit WaveshareS3AudioBackend(int speakerVolume = 70, int micGainDb = 30)
                : _volume(speakerVolume), _micGainDb(micGainDb) {}

            bool begin(int sampleRate, int framesPerBuffer) override;
            int inputChannels() const override { return _mic ? 2 : 0; }
            int outputChannels() const override { return _speaker ? 2 : 0; }
            void read(float *dst, int frames) override;
            void write(const float *src, int frames) override;

            /** Switches the speaker amplifier (TCA9555 EXIO8). On after begin(). */
            bool setAmpEnabled(bool on);

        private:
            int _volume;
            int _micGainDb;
            void *_i2c = nullptr;     // i2c_master_bus_handle_t
            void *_speaker = nullptr; // esp_codec_dev_handle_t
            void *_mic = nullptr;
            int16_t *_buf = nullptr;
            int _bufFrames = 0;
        };
    }
}
