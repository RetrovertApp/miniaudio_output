#include <retrovert/log.h>
#include <retrovert/output.h>
#include <retrovert/settings.h>

#define PLUGIN_NAME "miniaudio"

#define MA_NO_DSOUND      // Disables the DirectSound backend
#define MA_NO_WINMM       // Disables the WinMM backend
#define MA_NO_JACK        // Disables the JACK backend
#define MA_NO_SNDIO       // Disables the sndio backend
#define MA_NO_OSS         // Disables the OSS backend
#define MA_NO_AAUDIO      // Disables the AAudio backend
#define MA_NO_WAV         // Disables the built-in WAV decoder and encoder
#define MA_NO_FLAC        // Disables the built-in FLAC decoder
#define MA_NO_MP3         // Disables the built-in MP3 decoder
#define MA_NO_GENERATION  // Disables generation APIs such a `ma_waveform` and `ma_noise`.
#define MA_API static      // Controls how public APIs should be decorated. Default is `extern`. |
// #define MA_DEBUG_OUTPUT // Enable `printf()` output of debug logs (`MA_LOG_LEVEL_DEBUG`).
// #define MA_COINIT_VALUE // Windows only. The value to pass to internal calls to `CoInitializeEx()`. Defaults to
// `COINIT_MULTITHREADED`.
#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio.h"

const RVLog* g_rv_log = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int s_format_convert[ma_format_count];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct MiniaudioOutput {
    ma_context context;
    ma_device device;
    RVPlaybackCallback callback;
    ma_device_info* devices;
    int default_index;
    int devices_count;
    const char** output_names;
} MiniaudioOutput;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* miniaudio_output_create(const RVService* services) {
    int error = 0;
    MiniaudioOutput* data = calloc(1, sizeof(MiniaudioOutput));

    if (!data) {
        rvfl_error("Unable to allocate memory");
        return NULL;
    }

    if ((error = ma_context_init(NULL, 0, NULL, &data->context)) != MA_SUCCESS) {
        rvfl_error("Unable to create context: %d", error);
        goto error;
    }

    ma_device_info* playback_infos;
    ma_uint32 playback_count;

    if ((error = ma_context_get_devices(&data->context, &playback_infos, &playback_count, NULL, NULL)) != MA_SUCCESS) {
        rvfl_error("Unable to get devices: %d", error);
        return NULL;
    }

    data->devices = malloc(sizeof(ma_device) * playback_count);

    if (!data->devices) {
        rvfl_error("Unable to allocate memory");
        goto error;
    }

    memcpy(data->devices, playback_infos, sizeof(ma_device) * playback_count);
    data->devices_count = playback_count;

    data->output_names = malloc(sizeof(char*) * playback_count);

    if (!data->output_names) {
        rvfl_error("Unable to allocate memory");
        goto error;
    }

    for (int i = 0; i < playback_count; ++i) {
        if (data->devices[i].isDefault) {
            data->default_index = i;
        }
        data->output_names[i] = (const char*)&data->devices[i].name;
        rv_trace("%s (default %d)", data->devices[i].name, data->devices[i].isDefault);
    }

    return data;

error:
    if (data) {
        free(data->devices);
        free(data->output_names);
        free(data);
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int miniaudio_destroy(void* user_data) {
    MiniaudioOutput* data = (MiniaudioOutput*)user_data;
    free(data->output_names);
    free(data->devices);
    free(data);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void miniaudio_data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    (void)input;
    MiniaudioOutput* data = (MiniaudioOutput*)device->pUserData;
    data->callback.callback(data->callback.user_data, output, device->sampleRate, device->playback.channels,
                            s_format_convert[device->playback.format], frame_count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void miniaudio_start(void* user_data, RVPlaybackCallback* callback) {
    int error = 0;
    MiniaudioOutput* data = (MiniaudioOutput*)user_data;
    data->callback = *callback;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = &data->devices[data->default_index].id;
    // TODO: We should use native output here instead, do this default until decoder is finished.
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    // config.playback.format = ma_format_unknown;
    // config.playback.channels = 0;
    // config.sampleRate = 0;
    config.dataCallback = miniaudio_data_callback;
    config.pUserData = user_data;

    if ((error = ma_device_init(&data->context, &config, &data->device)) != MA_SUCCESS) {
        rv_error("Unable to miniaudio create device. Error %d", error);
        return;
    }

    ma_device_start(&data->device);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void miniaudio_stop(void* user_data) {
    MiniaudioOutput* data = (MiniaudioOutput*)user_data;
    ma_device_stop(&data->device);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RVOutputTargets miniaudio_output_targets_info(void* user_data) {
    MiniaudioOutput* data = (MiniaudioOutput*)user_data;
    return (RVOutputTargets){data->output_names, data->devices_count};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void miniaudio_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);

    // format remapping that only needs to be done once.
    s_format_convert[ma_format_unknown] = RVInputType_Unknown;
    s_format_convert[ma_format_u8] = RVInputType_U8;
    s_format_convert[ma_format_s16] = RVInputType_S16;
    s_format_convert[ma_format_s24] = RVInputType_S24;
    s_format_convert[ma_format_s32] = RVInputType_S32;
    s_format_convert[ma_format_f32] = RVInputType_F32;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVOutputPlugin s_miniaudio_plugin = {
    RV_OUTPUT_PLUGIN_API_VERSION,
    PLUGIN_NAME,
    "0.0.1",
    "0.11.9",
    miniaudio_output_create,
    miniaudio_destroy,
    miniaudio_output_targets_info,
    miniaudio_start,
    miniaudio_stop,
    miniaudio_static_init,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern RV_EXPORT RVOutputPlugin* rv_output_plugin() {
    return &s_miniaudio_plugin;
}
