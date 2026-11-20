// Pd task: libpd init, patch loading, audio/MIDI loop. All libpd calls happen
// on this one task.
#include "enomik_pd.h"

#include <atomic>
#include <pthread.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "libpd_esp_compat.h"
#include "midi_bridge.h"
#include "patch_upload.h"
#include "z_libpd.h"
#include "z_print_util.h"

static const char *TAG = "enomik_pd";

namespace
{
    enomik::pd::Config s_cfg;
    void *s_patch = nullptr;
    std::atomic<bool> s_reloadRequested{false};
    enomik::pd::detail::PatchUpload s_upload;
    std::atomic<TaskHandle_t> s_task{nullptr}; // set by the Pd thread itself
    bool s_started = false;

    void printLine(const char *s)
    {
        ESP_LOGI("pd", "%s", s);
    }

    void openPatch()
    {
        s_patch = libpd_openfile(s_cfg.patchFile, s_cfg.patchDir);
        if (!s_patch)
            ESP_LOGE(TAG, "could not open %s/%s", s_cfg.patchDir, s_cfg.patchFile);
        else
            ESP_LOGI(TAG, "opened %s/%s ($0 = %d)", s_cfg.patchDir, s_cfg.patchFile, libpd_getdollarzero(s_patch));
    }

    void reloadPatch()
    {
        if (s_patch)
        {
            libpd_closefile(s_patch);
            s_patch = nullptr;
        }
        openPatch();
    }

    // Control-only clock: wakes the Pd task once per buffer when no audio
    // backend paces the loop.
    void timerTick(void *)
    {
        if (TaskHandle_t t = s_task.load())
            xTaskNotifyGive(t);
    }

    float *allocSamples(int n)
    {
        return n > 0 ? static_cast<float *>(heap_caps_calloc(n, sizeof(float), MALLOC_CAP_INTERNAL)) : nullptr;
    }

    // Runs as a pthread, not a plain FreeRTOS task: Pd calls pthread_self()
    // (sched_init, sys_ismainthread), which asserts on ESP-IDF in a thread
    // that pthread did not create.
    void *pdThread(void *)
    {
        s_task.store(xTaskGetCurrentTaskHandle());
        const int sr = s_cfg.sampleRate;
        const int ticks = s_cfg.ticks;
        const int frames = ticks * 64; // libpd_blocksize() is always 64

        libpd_set_printhook(libpd_print_concatenator);
        libpd_set_concatenated_printhook(printLine);
        libpd_init();
        libpd_esp_extras_setup();
        enomik::pd::detail::installPdHooks();

        if (s_cfg.patchUpload)
        {
            s_upload.configure(s_cfg.patchDir, s_cfg.patchFile, s_cfg.deviceId,
                               enomik::pd::detail::sendSysEx,
                               [] { s_reloadRequested.store(true); });
            enomik::pd::detail::setSysExInterceptor([](const uint8_t *d, unsigned n)
                                                    { return s_upload.handle(d, n); });
            ESP_LOGI(TAG, "patch upload (MIDI File Dump) on, device ID %u", (unsigned)(s_cfg.deviceId & 0x7F));
        }

        enomik::pd::AudioBackend *audio = s_cfg.audio;
        if (audio && !audio->begin(sr, frames))
        {
            ESP_LOGE(TAG, "audio backend failed: running control-only");
            audio = nullptr;
        }
        const int inCh = audio ? audio->inputChannels() : 0;
        const int outCh = audio ? audio->outputChannels() : 0;
        libpd_init_audio(inCh, outCh, sr);

        libpd_add_to_search_path(s_cfg.patchDir);
        openPatch();

        // [; pd dsp 1(
        libpd_start_message(1);
        libpd_add_float(1.0f);
        libpd_finish_message("pd", "dsp");

        float *in = allocSamples(frames * inCh);
        float *out = allocSamples(frames * outCh);
        if ((inCh && !in) || (outCh && !out))
        {
            ESP_LOGE(TAG, "audio buffer alloc failed");
            return nullptr;
        }

        // A blocking write (or read) paces the loop. Otherwise use a timer.
        const bool selfClocked = outCh > 0 || inCh > 0;
        if (!selfClocked)
        {
            esp_timer_create_args_t args = {};
            args.callback = timerTick;
            args.dispatch_method = ESP_TIMER_TASK;
            args.name = "pd_clock";
            args.skip_unhandled_events = true;
            esp_timer_handle_t timer = nullptr;
            esp_timer_create(&args, &timer);
            esp_timer_start_periodic(timer, static_cast<uint64_t>(frames) * 1000000ULL / sr);
            ESP_LOGI(TAG, "control-only: Pd ticked by timer every %d frames", frames);
        }

        ESP_LOGI(TAG, "Pd running: %d Hz, %d ticks/buffer, in %d / out %d ch, core %d, free internal %u, psram %u",
                 sr, ticks, inCh, outCh, xPortGetCoreID(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        for (;;)
        {
            if (!selfClocked)
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            if (s_reloadRequested.exchange(false))
                reloadPatch();

            enomik::pd::detail::poll(); // received MIDI -> Pd ([notein], [ctlin], ...)
            if (s_cfg.patchUpload)
                s_upload.poll();

            if (inCh)
                audio->read(in, frames);
            libpd_process_float(ticks, in, out);
            if (outCh)
                audio->write(out, frames); // blocks on DMA: this is the clock
        }
    }
}

namespace enomik
{
    namespace pd
    {
        bool begin(const Config &config)
        {
            if (s_started)
            {
                ESP_LOGW(TAG, "begin() called twice");
                return false;
            }
            s_cfg = config;
            if (!detail::clientBegin(s_cfg.clientName))
                return false;

            esp_pthread_cfg_t threadCfg = esp_pthread_get_default_config();
            threadCfg.stack_size = s_cfg.stackSize;
            threadCfg.prio = s_cfg.priority;
            threadCfg.pin_to_core = s_cfg.core;
            threadCfg.thread_name = "pd";
            threadCfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
            esp_pthread_set_cfg(&threadCfg);

            pthread_t thread;
            const int err = pthread_create(&thread, nullptr, pdThread, nullptr);
            const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
            esp_pthread_set_cfg(&defaults); // don't leak our settings into the caller's later pthreads
            if (err != 0)
            {
                ESP_LOGE(TAG, "could not start Pd thread (%d)", err);
                return false;
            }
            pthread_detach(thread);
            s_started = true;
            return true;
        }

        void requestReload()
        {
            s_reloadRequested.store(true);
            if (TaskHandle_t t = s_task.load())
                xTaskNotifyGive(t); // wakes the control-only loop; harmless otherwise
        }

        bool mountLittleFs(const char *partitionLabel, const char *basePath)
        {
            esp_vfs_littlefs_conf_t conf = {};
            conf.base_path = basePath;
            conf.partition_label = partitionLabel;
            conf.format_if_mount_failed = false; // keep a broken image for inspection
            const esp_err_t err = esp_vfs_littlefs_register(&conf);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "mount LittleFS '%s' at %s failed: %s", partitionLabel, basePath, esp_err_to_name(err));
                return false;
            }
            size_t total = 0, used = 0;
            esp_littlefs_info(partitionLabel, &total, &used);
            ESP_LOGI(TAG, "LittleFS '%s' at %s: %u/%u bytes used", partitionLabel, basePath, (unsigned)used, (unsigned)total);
            return true;
        }
    }
}
