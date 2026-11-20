/**
 * @file enomik_pd.h
 * @brief Run a Pure Data patch (libpd) as an enomik client on ESP-IDF.
 *
 * Pd's MIDI objects ([notein], [ctlin], [noteout], [midiout], ...) are
 * connected to ESP-NOW MIDI, and USB MIDI on ESP32-S2/S3, through
 * enomik::Client. Audio goes through an AudioBackend: the generic I2S one
 * (enomik_pd_i2s.h), a board backend such as enomik_pd_waveshare_s3_audio.h,
 * or your own. Without a backend Pd runs control-only, ticked by a timer.
 *
 * @code
 * #include "enomik_pd.h"
 * #include "enomik_pd_i2s.h"
 *
 * static enomik::pd::I2sStdBackend audio({.bclk = 5, .ws = 6, .dout = 7, .din = 15});
 *
 * extern "C" void app_main(void)
 * {
 *     enomik::pd::mountLittleFs("pd", "/pd");
 *     enomik::pd::Config cfg;
 *     cfg.audio = &audio;
 *     enomik::pd::begin(cfg);
 * }
 * @endcode
 *
 * Threading: libpd is not thread-safe. All Pd work, including the enomik
 * MIDI handlers, runs on one task created by begin().
 */
#pragma once

#include <stdint.h>

namespace enomik
{
    class Client;

    namespace pd
    {
        /** Audio I/O used by the Pd task. Samples are interleaved floats, -1..1. */
        class AudioBackend
        {
        public:
            virtual ~AudioBackend() = default;

            /** Called once on the Pd task. Return false to fall back to control-only. */
            virtual bool begin(int sampleRate, int framesPerBuffer) = 0;

            virtual int inputChannels() const = 0;  ///< [adc~] channels (0 = none)
            virtual int outputChannels() const = 0; ///< [dac~] channels (0 = none)

            /** Fills frames * inputChannels() samples. Only called when inputChannels() > 0. */
            virtual void read(float *dst, int frames) = 0;

            /**
             * Writes frames * outputChannels() samples. Only called when
             * outputChannels() > 0. Must block until the hardware has room:
             * this paces the Pd loop to the sample clock.
             */
            virtual void write(const float *src, int frames) = 0;
        };

        struct Config
        {
            const char *patchDir = "/pd";       ///< directory of the patch; also on Pd's search path
            const char *patchFile = "main.pd";  ///< opened at start
            const char *clientName = nullptr;   ///< enomik::Client::setName(); nullptr keeps the default
            int sampleRate = 48000;
            int ticks = 2;                      ///< Pd blocks (64 samples) per audio buffer
            int core = 1;                       ///< CPU core of the Pd task (WiFi runs on core 0)
            int priority = 22;                  ///< FreeRTOS priority of the Pd task (WiFi task: 23)
            uint32_t stackSize = 16384;         ///< Pd task stack (bytes)
            AudioBackend *audio = nullptr;      ///< nullptr: control-only (no adc~/dac~)

            /**
             * Accept patch files over SysEx (MIDI File Dump, USB and ESP-NOW).
             * Files go to patchDir; receiving patchFile reloads the patch.
             */
            bool patchUpload = true;
            /** File Dump device ID (0-126) of this client; messages to 0x7F (all) are accepted too. */
            uint8_t deviceId = 0;
        };

        /**
         * Starts the enomik client (WiFi, ESP-NOW, USB MIDI on S2/S3) and the Pd
         * task, which opens the patch and turns DSP on. Call once.
         * @return false if the client or the task could not start.
         */
        bool begin(const Config &config);

        /**
         * Closes and reopens the patch on the Pd task at the next buffer. Safe
         * from any task. Use after changing patch files at runtime.
         */
        void requestReload();

        /**
         * Mounts a LittleFS partition, e.g. one built from a patch folder with
         * littlefs_create_partition_image(). Call before begin().
         */
        bool mountLittleFs(const char *partitionLabel = "pd", const char *basePath = "/pd");

        /**
         * The enomik client used by the Pd task: add peers, read stats, etc.
         * Its send and setHandle functions belong to the Pd bridge; calling
         * them from another task races with the Pd task.
         */
        Client &client();
    }
}
