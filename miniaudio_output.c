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
#define MA_API static     // Controls how public APIs should be decorated. Default is `extern`. |
//#define MA_DEBUG_OUTPUT   // Enable `printf()` output of debug logs (`MA_LOG_LEVEL_DEBUG`).
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
    MiniaudioOutput* data = (MiniaudioOutput*)device->pUserData;

    // temp hack to work-around broken miniaudio output for 4 channels
    if (device->playback.channels == 4) {
        RVAudioFormat format = {s_format_convert[device->playback.format], 2, device->sampleRate};
        void* temp_output = alloca(frame_count * 4 * 4);  // max size alloc for the frame
        if (data->callback.callback(data->callback.user_data, temp_output, format, frame_count) == 0) {
            return;
        }

        if (device->playback.format == ma_format_f32) {
            float* real_output = (float*)output;
            float* temp = (float*)temp_output;
            for (int i = 0; i < frame_count; ++i) {
                *real_output++ = *temp++;
                *real_output++ = *temp++;
                *real_output++ = 0.0f;
                *real_output++ = 0.0f;
            }
        } else if (device->playback.format == ma_format_s16) {
            int16_t* real_output = (int16_t*)output;
            int16_t* temp = (int16_t*)temp_output;
            for (int i = 0; i < frame_count; ++i) {
                *real_output++ = *temp++;
                *real_output++ = *temp++;
                *real_output++ = 0.0f;
                *real_output++ = 0.0f;
            }
        } else {
            rv_error("Unsupported hack format %d", device->playback.format);
        }
    } else {
        RVAudioFormat format = {s_format_convert[device->playback.format], device->playback.channels,
                                device->sampleRate};
        data->callback.callback(data->callback.user_data, output, format, frame_count);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void miniaudio_start(void* user_data, RVPlaybackCallback* callback) {
    int error = 0;
    MiniaudioOutput* data = (MiniaudioOutput*)user_data;
    data->callback = *callback;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = &data->devices[data->default_index].id;
    config.playback.format = ma_format_unknown;
    config.playback.channels = 0;
    config.sampleRate = 0;
    config.dataCallback = miniaudio_data_callback;
    config.pUserData = user_data;

    if ((error = ma_device_init(&data->context, &config, &data->device)) != MA_SUCCESS) {
        rv_error("miniaudio: Unable to create device. Error %d", error);
        return;
    }

    for (int i = 0; i < data->device.playback.channels; ++i) {
        rv_debug("channel map %d : %d", i, data->device.playback.channelMap[i]);
    }

    if ((error = ma_device_start(&data->device)) != MA_SUCCESS) {
        rv_error("miniaudio: Unable to start device. Error %d", error);
        ma_device_uninit(&data->device);
        return;
    }

    rv_info("miniaudio device created");
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
    s_format_convert[ma_format_unknown] = 0;
    s_format_convert[ma_format_u8] = RVAudioStreamFormat_U8;
    s_format_convert[ma_format_s16] = RVAudioStreamFormat_S16;
    s_format_convert[ma_format_s24] = RVAudioStreamFormat_S24;
    s_format_convert[ma_format_s32] = RVAudioStreamFormat_S32;
    s_format_convert[ma_format_f32] = RVAudioStreamFormat_F32;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVOutputPlugin s_miniaudio_plugin = {
    RV_OUTPUT_PLUGIN_API_VERSION,
    PLUGIN_NAME,
    "0.0.2",
    "0.11.21",
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
