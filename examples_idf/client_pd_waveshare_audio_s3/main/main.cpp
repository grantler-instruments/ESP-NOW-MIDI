/**
 * enomik client running Pure Data (libpd) on the Waveshare ESP32-S3-AUDIO-Board.
 *
 * [dac~] plays on the on-board speaker (ES8311), [adc~] records the two
 * microphones (ES7210). Pd's MIDI objects talk to ESP-NOW MIDI and USB MIDI.
 * The patch is pd/main.pd, flashed as a LittleFS image.
 *
 *   idf.py set-target esp32s3
 *   idf.py build flash monitor
 */
#include "enomik_pd.h"
#include "enomik_pd_waveshare_s3_audio.h"
#include "sdkconfig.h"

static enomik::pd::WaveshareS3AudioBackend g_audio(CONFIG_CLIENT_PD_SPEAKER_VOLUME,
                                                   CONFIG_CLIENT_PD_MIC_GAIN_DB);

extern "C" void app_main(void)
{
    enomik::pd::mountLittleFs("pd", "/pd"); // without it Pd still runs, just with no patch

    enomik::pd::Config cfg;
    cfg.clientName = "client_pd_waveshare";
    cfg.sampleRate = CONFIG_CLIENT_PD_SAMPLE_RATE;
    cfg.ticks = CONFIG_CLIENT_PD_TICKS;
    cfg.core = CONFIG_CLIENT_PD_CORE;
    cfg.audio = &g_audio;
    enomik::pd::begin(cfg);
}
