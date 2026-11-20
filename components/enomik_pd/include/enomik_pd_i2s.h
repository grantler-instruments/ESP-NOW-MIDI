/**
 * @file enomik_pd_i2s.h
 * @brief Generic I2S audio backend for enomik_pd (plain I2S DACs/ADCs/mics).
 *
 * Standard Philips I2S, 32-bit slots, stereo, ESP32 as master, on one I2S
 * controller in full duplex. For parts that need no register setup, e.g.
 * PCM5102A / MAX98357A (out) and INMP441 (in).
 */
#pragma once

#include "enomik_pd.h"

#include <stdint.h>

namespace enomik
{
    namespace pd
    {
        struct I2sPins
        {
            int bclk = -1;
            int ws = -1;
            int dout = -1; ///< to DAC; -1 = no [dac~]
            int din = -1;  ///< from ADC/mic; -1 = no [adc~]
            int mclk = -1; ///< -1 = unused
        };

        class I2sStdBackend : public AudioBackend
        {
        public:
            explicit I2sStdBackend(const I2sPins &pins, int port = 0) : _pins(pins), _port(port) {}

            bool begin(int sampleRate, int framesPerBuffer) override;
            int inputChannels() const override { return _rx ? 2 : 0; }
            int outputChannels() const override { return _tx ? 2 : 0; }
            void read(float *dst, int frames) override;
            void write(const float *src, int frames) override;

        private:
            I2sPins _pins;
            int _port;
            void *_tx = nullptr; // i2s_chan_handle_t
            void *_rx = nullptr;
            int32_t *_buf = nullptr;
            int _bufFrames = 0;
        };
    }
}
