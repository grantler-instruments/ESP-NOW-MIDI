/**
 * enomik client running Pure Data (libpd), generic I2S audio.
 *
 * Pd's MIDI objects ([notein], [ctlin], [noteout], [midiout], ...) talk to
 * ESP-NOW MIDI (and USB MIDI on S2/S3). [adc~] / [dac~] run over I2S with the
 * pins from menuconfig. The patch is pd/main.pd, flashed as a LittleFS image.
 *
 *   idf.py set-target esp32s3
 *   idf.py build flash monitor
 */
#include "enomik_pd.h"
#include "enomik_pd_i2s.h"
#include "sdkconfig.h"

#if CONFIG_CLIENT_PD_AUDIO
static enomik::pd::I2sStdBackend g_audio({
    .bclk = CONFIG_CLIENT_PD_I2S_BCLK,
    .ws = CONFIG_CLIENT_PD_I2S_WS,
    .dout = CONFIG_CLIENT_PD_I2S_DOUT,
    .din = CONFIG_CLIENT_PD_I2S_DIN,
    .mclk = CONFIG_CLIENT_PD_I2S_MCLK,
});
#endif

extern "C" void app_main(void)
{
    enomik::pd::mountLittleFs("pd", "/pd"); // without it Pd still runs, just with no patch

    enomik::pd::Config cfg;
    cfg.clientName = "client_pd";
    cfg.sampleRate = CONFIG_CLIENT_PD_SAMPLE_RATE;
    cfg.ticks = CONFIG_CLIENT_PD_TICKS;
    cfg.core = CONFIG_CLIENT_PD_CORE;
#if CONFIG_CLIENT_PD_AUDIO
    cfg.audio = &g_audio;
#endif
    enomik::pd::begin(cfg);
}
