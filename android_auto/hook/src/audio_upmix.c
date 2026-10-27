/* SPDX-License-Identifier: GPL-3.0-or-later
 * Android Auto GAL media upmixer for Audi MHI2Q.
 *
 * Fail-closed LD_PRELOAD interposer:
 *   - touches only mpl6_gal_ent (guidance/calls use other devices);
 *   - defaults off; SQ5_AA_UPMIX=probe only logs capabilities;
 *   - SQ5_AA_UPMIX=on expands only an exact 48 kHz/S16/stereo block stream;
 *   - requires the physical QSA device to report six voices and a full PCM6
 *     fragment before changing any parameter;
 *   - falls back to the caller's untouched stereo parameters on every error.
 */
#include <sys/asoundlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio_upmix_policy.h"
#include "audio/stereo_upmix.h"

#define SQ5_AA_MEDIA_DEVICE "mpl6_gal_ent"
#define SQ5_AA_MASK_ENV "SQ5_AA_UPMIX_MASK"
#define SQ5_AA_LEVEL_ENV "SQ5_AA_UPMIX_LEVEL"
#define SQ5_AA_MIN_FRAGS 4
#define SQ5_AA_MAX_FRAGS 6
#define HOOK_EXPORT __attribute__((visibility("default")))

void probe_log(const char *fmt, ...);

typedef int (*open_fn)(snd_pcm_t **, char *, int);
typedef int (*params_fn)(snd_pcm_t *, snd_pcm_channel_params_t *);
typedef int (*setup_fn)(snd_pcm_t *, snd_pcm_channel_setup_t *);
typedef int (*info_fn)(snd_pcm_t *, snd_pcm_channel_info_t *);
typedef ssize_t (*write_fn)(snd_pcm_t *, const void *, size_t);
typedef int (*close_fn)(snd_pcm_t *);

enum aa_upmix_mode { AA_UPMIX_OFF, AA_UPMIX_PROBE, AA_UPMIX_ON };

static struct {
    pthread_mutex_t state_lock;
    pthread_mutex_t io_lock;
    pthread_key_t io_owner_key;
    pthread_once_t io_key_once;
    pthread_once_t mode_once;
    enum aa_upmix_mode mode;
    snd_pcm_t *handle;
    int expanded;
    unsigned source_channels;
    unsigned target_channels;
    size_t source_fragment;
    size_t target_fragment;
    struct spa_stereo_upmix dsp;
    int dsp_ready;
    struct spa_scene_mask_worker mask_worker;
    int mask_worker_started;
    int16_t output[SQ5_AA_UPMIX_MAX_FRAMES * SQ5_AA_UPMIX_TARGET_CHANNELS];
    uint64_t writes;
    uint64_t failures;
    uint64_t short_writes;
} aa = {
    .state_lock = PTHREAD_MUTEX_INITIALIZER,
    .io_lock = PTHREAD_MUTEX_INITIALIZER,
    .io_key_once = PTHREAD_ONCE_INIT,
    .mode_once = PTHREAD_ONCE_INIT
};

static struct scene_mask_weights mask_blob;
static int mask_blob_state = -1;

static void make_io_key(void) { (void)pthread_key_create(&aa.io_owner_key, NULL); }
static int inside_real_io(void)
{
    pthread_once(&aa.io_key_once, make_io_key);
    return pthread_getspecific(aa.io_owner_key) != NULL;
}
static void set_real_io(int value)
{
    pthread_once(&aa.io_key_once, make_io_key);
    (void)pthread_setspecific(aa.io_owner_key, value ? (void *)&aa : NULL);
}

static void read_mode(void)
{
    const char *value = getenv("SQ5_AA_UPMIX");
    aa.mode = value && !strcmp(value, "on") ? AA_UPMIX_ON :
              value && !strcmp(value, "probe") ? AA_UPMIX_PROBE : AA_UPMIX_OFF;
    if (aa.mode != AA_UPMIX_OFF)
        probe_log("aa_upmix.mode value=%s device=%s", aa.mode == AA_UPMIX_ON ? "on" : "probe",
                  SQ5_AA_MEDIA_DEVICE);
}

static enum aa_upmix_mode current_mode(void)
{
    pthread_once(&aa.mode_once, read_mode);
    return aa.mode;
}

static int requested_level(void)
{
    const char *value = getenv(SQ5_AA_LEVEL_ENV);
    char *end = NULL;
    long level = value ? strtol(value, &end, 10) : SPA_UPMIX_LEVEL_NORMAL;
    if (!value || end == value || *end || level < 0 || level >= SPA_UPMIX_LEVEL_COUNT)
        return SPA_UPMIX_LEVEL_NORMAL;
    return (int)level;
}

static const struct scene_mask_weights *load_mask(void)
{
    const char *path = getenv(SQ5_AA_MASK_ENV);
    FILE *file;
    size_t got;
    int extra;

    if (mask_blob_state >= 0) return mask_blob_state ? NULL : &mask_blob;
    if (!path || !*path) { mask_blob_state = 1; return NULL; }
    file = fopen(path, "rb");
    if (!file) {
        probe_log("aa_upmix.mask unreadable path=%s errno=%d", path, errno);
        mask_blob_state = 1;
        return NULL;
    }
    got = fread(&mask_blob, 1, sizeof(mask_blob), file);
    extra = fgetc(file);
    fclose(file);
    if (got != sizeof(mask_blob) || extra != EOF ||
        memcmp(mask_blob.magic, SMASK_MAGIC, sizeof(mask_blob.magic))) {
        probe_log("aa_upmix.mask rejected path=%s bytes=%u expected=%u", path,
                  (unsigned)got, (unsigned)sizeof(mask_blob));
        memset(&mask_blob, 0, sizeof(mask_blob));
        mask_blob_state = 1;
        return NULL;
    }
    mask_blob_state = 0;
    probe_log("aa_upmix.mask ready path=%s bytes=%u", path, (unsigned)got);
    return &mask_blob;
}

static void stop_dsp_locked(void)
{
    aa.dsp_ready = 0;
    if (aa.mask_worker_started) {
        struct spa_scene_mask_worker_stats stats;
        spa_scene_mask_worker_get_stats(&aa.mask_worker, &stats);
        spa_scene_mask_worker_stop(&aa.mask_worker);
        probe_log("aa_upmix.mask stop rows=%u/%u skipped=%u stale=%u", (unsigned)stats.completed,
                  (unsigned)stats.submitted, (unsigned)stats.busy,
                  (unsigned)stats.stale_discarded);
        aa.mask_worker_started = 0;
    }
}

static int start_dsp_locked(void)
{
    struct spa_stereo_upmix_config config;
    const struct scene_mask_weights *blob;
    int result;

    stop_dsp_locked();
    spa_stereo_upmix_default_config(&config);
    config.backend = SPA_STEREO_UPMIX_SCENE;
    config.level = requested_level();
    spa_stereo_upmix_config_set_lfe(&config, 1);
    blob = load_mask();
    if (blob) {
        if (spa_scene_mask_worker_start(&aa.mask_worker, blob, 1u, 17) != 0) {
            probe_log("aa_upmix.mask worker_failed; refusing wet path");
            return 0;
        }
        aa.mask_worker_started = 1;
        spa_stereo_upmix_config_set_scene_mask_provider(
            &config, spa_scene_mask_worker_provider(&aa.mask_worker));
    }
    result = spa_stereo_upmix_init(&aa.dsp, &config);
    aa.dsp_ready = result == SPA_STEREO_UPMIX_OK;
    probe_log("aa_upmix.dsp ready=%d level=%d mask=%d init=%d", aa.dsp_ready,
              config.level, blob != NULL, result);
    if (!aa.dsp_ready) stop_dsp_locked();
    return aa.dsp_ready;
}

static void dry_expand(const int16_t *input, size_t frames, int16_t *output)
{
    size_t i;
    for (i = 0; i < frames; ++i) {
        output[i * 6 + 0] = input[i * 2 + 0];
        output[i * 6 + 1] = input[i * 2 + 1];
        output[i * 6 + 2] = output[i * 6 + 3] = 0;
        output[i * 6 + 4] = output[i * 6 + 5] = 0;
    }
}

HOOK_EXPORT int snd_pcm_open_name(snd_pcm_t **handle, char *name, int mode)
{
    static open_fn real;
    int result, media;
    if (!real) real = (open_fn)dlsym(RTLD_NEXT, "snd_pcm_open_name");
    if (!real) return -1;
    result = real(handle, name, mode);
    media = current_mode() != AA_UPMIX_OFF && handle && name &&
            !strcmp(name, SQ5_AA_MEDIA_DEVICE) && (mode & SND_PCM_OPEN_PLAYBACK);
    if (result == 0 && media && *handle) {
        pthread_mutex_lock(&aa.io_lock);
        pthread_mutex_lock(&aa.state_lock);
        aa.handle = *handle;
        aa.expanded = aa.dsp_ready = 0;
        aa.source_channels = aa.target_channels = 0;
        aa.source_fragment = aa.target_fragment = 0;
        aa.writes = aa.failures = aa.short_writes = 0;
        pthread_mutex_unlock(&aa.state_lock);
        pthread_mutex_unlock(&aa.io_lock);
        probe_log("aa_upmix.open handle=%p mode=0x%x", *handle, mode);
    }
    return result;
}

HOOK_EXPORT int snd_pcm_plugin_params(snd_pcm_t *handle, snd_pcm_channel_params_t *params)
{
    static params_fn real;
    static setup_fn real_setup;
    static info_fn real_info;
    snd_pcm_channel_params_t adjusted;
    snd_pcm_channel_setup_t actual;
    snd_pcm_channel_info_t info;
    struct sq5_aa_pcm_geometry source, target;
    struct sq5_aa_pcm_caps caps;
    int tracked, result, compatible, dsp_ok;

    if (!real) real = (params_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_params");
    if (!real) return -EIO;
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle;
    pthread_mutex_unlock(&aa.state_lock);
    if (!tracked || !params || current_mode() == AA_UPMIX_OFF) return real(handle, params);

    pthread_mutex_lock(&aa.io_lock);
    /* A new params transaction invalidates the previous QSA geometry before
     * any fallible probe or native call.  Every refusal below therefore
     * leaves writes on the native/stereo path, never on stale PCM6 state. */
    pthread_mutex_lock(&aa.state_lock);
    stop_dsp_locked();
    aa.expanded = 0;
    aa.source_channels = aa.target_channels = 0;
    aa.source_fragment = aa.target_fragment = 0;
    pthread_mutex_unlock(&aa.state_lock);
    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (!real_info) real_info = (info_fn)dlsym(RTLD_NEXT, "snd_pcm_channel_info");
    result = real_info ? real_info(handle, &info) : -1;
    probe_log("aa_upmix.caps result=%d voices=%d..%d rate=%d..%d frag=%d..%d formats=0x%x",
              result, info.min_voices, info.max_voices, info.min_rate, info.max_rate,
              info.min_fragment_size, info.max_fragment_size, (unsigned)info.formats);
    if (current_mode() == AA_UPMIX_PROBE || result != 0) {
        result = real(handle, params);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }

    compatible = params->channel == SND_PCM_CHANNEL_PLAYBACK &&
                 params->mode == SND_PCM_MODE_BLOCK &&
                 params->format.format == SND_PCM_SFMT_S16_LE &&
                 params->format.rate == SQ5_AA_UPMIX_RATE &&
                 params->format.voices == SQ5_AA_UPMIX_SOURCE_CHANNELS;
    source.rate = compatible ? (unsigned)params->format.rate : 0;
    source.channels = compatible ? (unsigned)params->format.voices : 0;
    source.fragment_bytes = compatible && params->buf.block.frag_size > 0 ?
                            (size_t)params->buf.block.frag_size : 0;
    caps.max_channels = info.max_voices > 0 ? (unsigned)info.max_voices : 0;
    caps.max_fragment_bytes = info.max_fragment_size > 0 ?
                              (size_t)info.max_fragment_size : 0;
    if (!compatible || !sq5_aa_upmix_plan(&source, &caps, &target)) {
        probe_log("aa_upmix.bypass reason=geometry rate=%d format=%d voices=%d frag=%d max=%d/%d",
                  params->format.rate, params->format.format, params->format.voices,
                  params->buf.block.frag_size, info.max_voices, info.max_fragment_size);
        result = real(handle, params);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }

    adjusted = *params;
    adjusted.format.voices = SQ5_AA_UPMIX_TARGET_CHANNELS;
    adjusted.buf.block.frag_size = (int)target.fragment_bytes;
    if (adjusted.buf.block.frags_max < SQ5_AA_MAX_FRAGS)
        adjusted.buf.block.frags_max = SQ5_AA_MAX_FRAGS;
    if (adjusted.buf.block.frags_min < SQ5_AA_MIN_FRAGS)
        adjusted.buf.block.frags_min = SQ5_AA_MIN_FRAGS;
    result = real(handle, &adjusted);
    if (result != 0) {
        probe_log("aa_upmix.bypass reason=params result=%d why=0x%x", result,
                  (unsigned)adjusted.why_failed);
        result = real(handle, params);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }
    if (!real_setup) real_setup = (setup_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_setup");
    memset(&actual, 0, sizeof(actual));
    actual.channel = SND_PCM_CHANNEL_PLAYBACK;
    result = real_setup ? real_setup(handle, &actual) : -EIO;
    if (result || actual.mode != SND_PCM_MODE_BLOCK ||
        actual.format.format != SND_PCM_SFMT_S16_LE ||
        actual.format.rate != SQ5_AA_UPMIX_RATE ||
        actual.format.voices != SQ5_AA_UPMIX_TARGET_CHANNELS ||
        actual.buf.block.frag_size != (int)target.fragment_bytes) {
        probe_log("aa_upmix.bypass reason=readback result=%d actual=%d/%d/%d/%d", result,
                  actual.format.rate, actual.format.format, actual.format.voices,
                  actual.buf.block.frag_size);
        result = real(handle, params);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }

    pthread_mutex_lock(&aa.state_lock);
    dsp_ok = start_dsp_locked();
    if (dsp_ok) {
        aa.expanded = 1;
        aa.source_channels = source.channels;
        aa.target_channels = target.channels;
        aa.source_fragment = source.fragment_bytes;
        aa.target_fragment = target.fragment_bytes;
    }
    pthread_mutex_unlock(&aa.state_lock);
    if (!dsp_ok) {
        result = real(handle, params);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }
    probe_log("aa_upmix.params active source=%uch/%uHz/%u target=%uch/%uHz/%u frags=%d..%d",
              source.channels, source.rate, (unsigned)source.fragment_bytes,
              target.channels, target.rate, (unsigned)target.fragment_bytes,
              adjusted.buf.block.frags_min, adjusted.buf.block.frags_max);
    pthread_mutex_unlock(&aa.io_lock);
    return 0;
}

HOOK_EXPORT int snd_pcm_plugin_setup(snd_pcm_t *handle, snd_pcm_channel_setup_t *setup)
{
    static setup_fn real;
    snd_pcm_channel_setup_t actual;
    int expanded, result;
    size_t source_fragment, target_fragment;
    if (!real) real = (setup_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_setup");
    if (!real) return -EIO;
    if (inside_real_io()) return real(handle, setup);
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded;
    source_fragment = aa.source_fragment;
    target_fragment = aa.target_fragment;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded || !setup) return real(handle, setup);
    pthread_mutex_lock(&aa.io_lock);
    actual = *setup;
    result = real(handle, &actual);
    if (!result && actual.format.voices == SQ5_AA_UPMIX_TARGET_CHANNELS &&
        actual.format.rate == SQ5_AA_UPMIX_RATE &&
        actual.buf.block.frag_size == (int)target_fragment) {
        *setup = actual;
        setup->format.voices = SQ5_AA_UPMIX_SOURCE_CHANNELS;
        setup->buf.block.frag_size = (int)source_fragment;
        if (target_fragment && actual.buf.block.max_frag_size)
            setup->buf.block.max_frag_size = (uint32_t)
                ((size_t)actual.buf.block.max_frag_size * source_fragment / target_fragment);
    } else if (!result) {
        probe_log("aa_upmix.setup invalid actual=%d/%d/%d", actual.format.rate,
                  actual.format.voices, actual.buf.block.frag_size);
        result = -EIO;
    }
    pthread_mutex_unlock(&aa.io_lock);
    return result;
}

HOOK_EXPORT ssize_t snd_pcm_plugin_write(snd_pcm_t *handle, const void *buffer, size_t size)
{
    static write_fn real;
    size_t frames, target_bytes;
    ssize_t written;
    int expanded, process_result;
    uint64_t write_number;
    if (!real) real = (write_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_write");
    if (!real) return -EIO;
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded && aa.dsp_ready;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded) return real(handle, buffer, size);
    if (!buffer || size % (SQ5_AA_UPMIX_SOURCE_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES))
        return -EINVAL;
    frames = size / (SQ5_AA_UPMIX_SOURCE_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES);
    if (!frames || frames > SQ5_AA_UPMIX_MAX_FRAMES) return -EINVAL;
    target_bytes = frames * SQ5_AA_UPMIX_TARGET_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES;

    pthread_mutex_lock(&aa.io_lock);
    pthread_mutex_lock(&aa.state_lock);
    process_result = spa_stereo_upmix_process_s16(&aa.dsp, (const int16_t *)buffer,
                                                   frames, aa.output, frames);
    if (process_result != SPA_STEREO_UPMIX_OK) {
        dry_expand((const int16_t *)buffer, frames, aa.output);
        aa.failures++;
    }
    write_number = ++aa.writes;
    pthread_mutex_unlock(&aa.state_lock);
    set_real_io(1);
    written = real(handle, aa.output, target_bytes);
    set_real_io(0);
    pthread_mutex_lock(&aa.state_lock);
    if (written >= 0 && (size_t)written < target_bytes) aa.short_writes++;
    if (write_number == 1 || write_number % 1024u == 0)
        probe_log("aa_upmix.write n=%llu frames=%u result=%d/%u dsp_fail=%llu short=%llu",
                  (unsigned long long)write_number, (unsigned)frames, (int)written,
                  (unsigned)target_bytes, (unsigned long long)aa.failures,
                  (unsigned long long)aa.short_writes);
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    if (written <= 0) return written;
    if ((size_t)written >= target_bytes) return (ssize_t)size;
    return (ssize_t)sq5_aa_upmix_source_bytes((size_t)written);
}

HOOK_EXPORT int snd_pcm_close(snd_pcm_t *handle)
{
    static close_fn real;
    int tracked, result;
    uint64_t writes, failures, short_writes;
    if (!real) real = (close_fn)dlsym(RTLD_NEXT, "snd_pcm_close");
    if (!real) return -1;
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle;
    pthread_mutex_unlock(&aa.state_lock);
    if (!tracked) return real(handle);
    pthread_mutex_lock(&aa.io_lock);
    result = real(handle);
    pthread_mutex_lock(&aa.state_lock);
    writes = aa.writes; failures = aa.failures; short_writes = aa.short_writes;
    stop_dsp_locked();
    aa.handle = NULL;
    aa.expanded = 0;
    aa.source_channels = aa.target_channels = 0;
    aa.source_fragment = aa.target_fragment = 0;
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    probe_log("aa_upmix.close result=%d writes=%llu dsp_fail=%llu short=%llu", result,
              (unsigned long long)writes, (unsigned long long)failures,
              (unsigned long long)short_writes);
    return result;
}
