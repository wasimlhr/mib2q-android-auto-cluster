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
#define SQ5_AA_LEVEL_FILE_ENV "SQ5_AA_UPMIX_LEVEL_FILE"
#define SQ5_AA_LEVEL_FILE "/tmp/carplay_upmix_level"
#define SQ5_AA_LEVEL_POLL_WRITES 64u
/* Luka's mature bridge accepts a large caller write and lets QSA split it
 * across physical periods.  GAL delivers Android Auto media in 8192-byte
 * stereo packets (2048 frames on run 310), which is larger than one DSP
 * invocation and larger than the 3072-byte logical QSA period.  Keep one
 * bounded output buffer, process it in DSP-sized pieces, then make the same
 * single QSA call GAL expects. */
/* 256 KiB is more than ten times the 24,576-byte expanded packet observed on
 * the car, without adding Luka's full 1 MiB bridge buffer to every GAL
 * process. */
#define SQ5_AA_UPMIX_MAX_OUTPUT_BYTES (256u * 1024u)
#define SQ5_AA_SAD_SET_COMMAND UINT32_C(0x8018fa01)
#define SQ5_AA_SAD_BYTES 24u
#define SQ5_AA_SAD_STEREO UINT8_C(0xc0)
#define SQ5_AA_SAD_5_1 UINT8_C(0xfc)
#define SQ5_AA_STEREO_BITRATE UINT32_C(0x177000)
#define SQ5_AA_5_1_BITRATE UINT32_C(0x465000)
#define SQ5_AA_SAMPLE_WIDTH UINT8_C(0x10)
/* Luka's validated MHI2Q path keeps the caller's low-water mark but requires
 * at least eight widened periods in the QSA ring.  The patched MU0918 mpl6
 * record permits ten 9216-byte periods. */
#define SQ5_AA_REQUIRED_FRAGS 8
#define HOOK_EXPORT __attribute__((visibility("default")))

void probe_log(const char *fmt, ...);

typedef int (*open_fn)(snd_pcm_t **, char *, int);
typedef int (*params_fn)(snd_pcm_t *, snd_pcm_channel_params_t *);
typedef int (*setup_fn)(snd_pcm_t *, snd_pcm_channel_setup_t *);
typedef int (*info_fn)(snd_pcm_t *, snd_pcm_channel_info_t *);
typedef int (*status_fn)(snd_pcm_t *, snd_pcm_channel_status_t *);
typedef int (*descriptor_fn)(snd_pcm_t *, int);
typedef int (*devctl_fn)(int, int, void *, size_t, int *);
typedef int (*prepare_fn)(snd_pcm_t *, int);
typedef int (*flush_fn)(snd_pcm_t *, int);
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
    int media_fd;
    int expanded;
    unsigned source_channels;
    unsigned target_channels;
    size_t source_fragment;
    size_t target_fragment;
    struct spa_stereo_upmix dsp;
    int dsp_ready;
    struct spa_scene_mask_worker mask_worker;
    int mask_worker_started;
    int16_t output[SQ5_AA_UPMIX_MAX_OUTPUT_BYTES / sizeof(int16_t)];
    uint64_t writes;
    uint64_t prepares;
    uint64_t flushes;
    uint64_t infos;
    uint64_t statuses;
    uint64_t sad_rewrites;
    uint64_t failures;
    uint64_t short_writes;
    int active_level;
} aa = {
    .state_lock = PTHREAD_MUTEX_INITIALIZER,
    .io_lock = PTHREAD_MUTEX_INITIALIZER,
    .io_key_once = PTHREAD_ONCE_INIT,
    .mode_once = PTHREAD_ONCE_INIT,
    .media_fd = -1
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
        probe_log("aa_upmix.mode value=%s device=%s normalize_default_mode=1",
                  aa.mode == AA_UPMIX_ON ? "on" : "probe", SQ5_AA_MEDIA_DEVICE);
}

static enum aa_upmix_mode current_mode(void)
{
    pthread_once(&aa.mode_once, read_mode);
    return aa.mode;
}

static int parse_level(const char *value, int fallback)
{
    char *end = NULL;
    long level = value ? strtol(value, &end, 10) : fallback;
    if (!value || end == value || *end || level < 0 || level >= SPA_UPMIX_LEVEL_COUNT)
        return fallback;
    return (int)level;
}

static int requested_level(void)
{
    const char *path = getenv(SQ5_AA_LEVEL_FILE_ENV);
    const char *value = getenv(SQ5_AA_LEVEL_ENV);
    char line[16];
    FILE *file;
    int fallback = parse_level(value, SPA_UPMIX_LEVEL_NORMAL);
    if (!path || !*path) path = SQ5_AA_LEVEL_FILE;
    file = fopen(path, "r");
    if (!file) return fallback;
    line[0] = 0;
    if (!fgets(line, sizeof(line), file)) line[0] = 0;
    fclose(file);
    line[strcspn(line, "\r\n")] = 0;
    return parse_level(line, fallback);
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
    aa.active_level = config.level;
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
        aa.media_fd = -1;
        aa.expanded = aa.dsp_ready = 0;
        aa.source_channels = aa.target_channels = 0;
        aa.source_fragment = aa.target_fragment = 0;
        aa.writes = aa.prepares = aa.flushes = 0;
        aa.infos = aa.statuses = 0;
        aa.sad_rewrites = 0;
        aa.failures = aa.short_writes = 0;
        pthread_mutex_unlock(&aa.state_lock);
        pthread_mutex_unlock(&aa.io_lock);
        probe_log("aa_upmix.open handle=%p mode=0x%x", *handle, mode);
    }
    return result;
}

/* GAL owns the SAD devctl and addresses it by the PCM file descriptor rather
 * than by snd_pcm_t. Capture only the descriptor for our exact media handle;
 * every other QSA endpoint remains outside the rewrite boundary. */
HOOK_EXPORT int snd_pcm_file_descriptor(snd_pcm_t *handle, int channel)
{
    static descriptor_fn real;
    int fd, tracked;
    if (!real) real = (descriptor_fn)dlsym(RTLD_NEXT, "snd_pcm_file_descriptor");
    if (!real) return -1;
    fd = real(handle, channel);
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle &&
              channel == SND_PCM_CHANNEL_PLAYBACK;
    if (tracked && fd >= 0 && current_mode() != AA_UPMIX_OFF)
        aa.media_fd = fd;
    pthread_mutex_unlock(&aa.state_lock);
    if (tracked && fd >= 0)
        probe_log("aa_upmix.fd value=%d", fd);
    return fd;
}

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int stock_stereo_sad(const uint8_t *sad)
{
    return sad[0] == 0 && sad[1] == 1 && sad[2] == 0 &&
           sad[3] == SQ5_AA_SAD_STEREO && sad[4] == 0 &&
           load_le32(sad + 8) == SQ5_AA_STEREO_BITRATE &&
           sad[12] == SQ5_AA_SAMPLE_WIDTH;
}

/* The patched QSA path writes L R C LFE Ls Rs, but stock GAL still submits a
 * stereo SAD header (c0). Rewrite only the exact SET packet observed on the
 * exact expanded mpl6_gal_ent descriptor. A private copy preserves GAL's
 * caller buffer, and every mismatch passes through byte-for-byte. */
HOOK_EXPORT int devctl(int fd, int command, void *data, size_t bytes, int *info)
{
    static devctl_fn real;
    uint8_t rewritten[SQ5_AA_SAD_BYTES];
    int eligible;
    uint64_t rewrite_number;

    if (!real) real = (devctl_fn)dlsym(RTLD_NEXT, "devctl");
    if (!real) return -1;
    pthread_mutex_lock(&aa.state_lock);
    eligible = current_mode() == AA_UPMIX_ON && aa.expanded && aa.dsp_ready &&
               fd >= 0 && fd == aa.media_fd;
    pthread_mutex_unlock(&aa.state_lock);
    if (!eligible || (uint32_t)command != SQ5_AA_SAD_SET_COMMAND ||
        !data || bytes != SQ5_AA_SAD_BYTES ||
        !stock_stereo_sad((const uint8_t *)data))
        return real(fd, command, data, bytes, info);

    memcpy(rewritten, data, sizeof(rewritten));
    rewritten[3] = SQ5_AA_SAD_5_1;
    store_le32(rewritten + 8, SQ5_AA_5_1_BITRATE);
    pthread_mutex_lock(&aa.state_lock);
    rewrite_number = ++aa.sad_rewrites;
    pthread_mutex_unlock(&aa.state_lock);
    probe_log("aa_upmix.sad rewrite=%llu fd=%d asad=c0->fc bitrate=177000->465000 width=10",
              (unsigned long long)rewrite_number, fd);
    return real(fd, command, rewritten, sizeof(rewritten), info);
}

static int32_t stereo_bytes(int32_t target_bytes)
{
    return target_bytes > 0 ?
           (int32_t)sq5_aa_upmix_source_bytes((size_t)target_bytes) : target_bytes;
}

/* GAL sizes its media period from the channel info.  On the patched PCM6
 * driver it read six voices and a 9216-byte maximum, asked for 9216-byte
 * stereo periods, was given 3072 and closed the sink before its first write
 * (car run 310: prepare ok, writes=0, every ~3 s).  Show GAL the stereo
 * device the upmix serves instead: two voices and every byte size scaled
 * from PCM6 to its stereo source, i.e. the stock 3072-byte period.  The
 * params interposer and libasound itself still read the physical record. */
static int stereo_info(snd_pcm_t *handle, snd_pcm_channel_info_t *info,
                       info_fn real, const char *api)
{
    snd_pcm_channel_info_t physical;
    int tracked, result;
    uint64_t call_number;

    if (!real) return -EIO;
    if (inside_real_io()) return real(handle, info);
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle;
    pthread_mutex_unlock(&aa.state_lock);
    if (!tracked || !info || current_mode() != AA_UPMIX_ON) return real(handle, info);

    /* snd_pcm_plugin_info may call snd_pcm_channel_info; translate once. */
    set_real_io(1);
    result = real(handle, info);
    set_real_io(0);
    if (result || info->max_voices < SQ5_AA_UPMIX_TARGET_CHANNELS ||
        info->max_fragment_size <
            SQ5_AA_UPMIX_TARGET_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES)
        return result;
    physical = *info;
    info->min_voices = info->max_voices = SQ5_AA_UPMIX_SOURCE_CHANNELS;
    info->min_fragment_size = stereo_bytes(physical.min_fragment_size);
    info->max_fragment_size = stereo_bytes(physical.max_fragment_size);
    info->max_buffer_size = stereo_bytes(physical.max_buffer_size);
    info->fifo_size = stereo_bytes(physical.fifo_size);
    info->transfer_block_size = stereo_bytes(physical.transfer_block_size);
    pthread_mutex_lock(&aa.state_lock);
    call_number = ++aa.infos;
    pthread_mutex_unlock(&aa.state_lock);
    if (call_number <= 4)
        probe_log("aa_upmix.info api=%s n=%llu physical=%d..%dv/%d..%d/buf%d shown=%d..%dv/%d..%d/buf%d align=%d",
                  api, (unsigned long long)call_number,
                  physical.min_voices, physical.max_voices,
                  physical.min_fragment_size, physical.max_fragment_size,
                  physical.max_buffer_size, info->min_voices, info->max_voices,
                  info->min_fragment_size, info->max_fragment_size,
                  info->max_buffer_size, physical.fragment_align);
    return 0;
}

HOOK_EXPORT int snd_pcm_channel_info(snd_pcm_t *handle, snd_pcm_channel_info_t *info)
{
    static info_fn real;
    if (!real) real = (info_fn)dlsym(RTLD_NEXT, "snd_pcm_channel_info");
    return stereo_info(handle, info, real, "channel");
}

HOOK_EXPORT int snd_pcm_plugin_info(snd_pcm_t *handle, snd_pcm_channel_info_t *info)
{
    static info_fn real;
    if (!real) real = (info_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_info");
    return stereo_info(handle, info, real, "plugin");
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
    size_t requested_source_fragment;
    int tracked, result, compatible, dsp_ok;

    if (!real) real = (params_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_params");
    if (!real) return -EIO;
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle;
    pthread_mutex_unlock(&aa.state_lock);
    if (!tracked || !params || current_mode() == AA_UPMIX_OFF) return real(handle, params);

    pthread_mutex_lock(&aa.io_lock);
    /* Match Luka's proven ownership rule: every libasound call made while we
     * own the QSA transaction must see physical geometry in any PLT callback
     * it makes into our setup/prepare/flush interposers. */
    set_real_io(1);
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
        set_real_io(0);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }

    /* This GAL build leaves channel/mode at their zero/default values even
     * though the exact named handle was opened for playback.  The format and
     * fragment fields are valid.  The handle identity and open mode are the
     * authoritative route boundary; normalize channel/mode only in our private
     * copy before configuring the PCM6 device. */
    compatible = params->format.format == SND_PCM_SFMT_S16_LE &&
                 params->format.rate == SQ5_AA_UPMIX_RATE &&
                 params->format.voices == SQ5_AA_UPMIX_SOURCE_CHANNELS;
    source.rate = compatible ? (unsigned)params->format.rate : 0;
    source.channels = compatible ? (unsigned)params->format.voices : 0;
    source.fragment_bytes = compatible && params->buf.block.frag_size > 0 ?
                            (size_t)params->buf.block.frag_size : 0;
    caps.max_channels = info.max_voices > 0 ? (unsigned)info.max_voices : 0;
    caps.max_fragment_bytes = info.max_fragment_size > 0 ?
                              (size_t)info.max_fragment_size : 0;
    requested_source_fragment = source.fragment_bytes;
    if (compatible)
        source.fragment_bytes =
            sq5_aa_upmix_select_source_fragment(requested_source_fragment, &caps);
    if (source.fragment_bytes && source.fragment_bytes != requested_source_fragment)
        probe_log("aa_upmix.fragment requested=%u selected=%u target=%u",
                  (unsigned)requested_source_fragment,
                  (unsigned)source.fragment_bytes,
                  (unsigned)(source.fragment_bytes * SQ5_AA_UPMIX_TARGET_CHANNELS /
                             SQ5_AA_UPMIX_SOURCE_CHANNELS));
    if (!compatible || !sq5_aa_upmix_plan(&source, &caps, &target)) {
        probe_log("aa_upmix.bypass reason=geometry channel=%d mode=%d rate=%d format=%d voices=%d frag=%d max=%d/%d",
                  params->channel, params->mode, params->format.rate,
                  params->format.format, params->format.voices,
                  params->buf.block.frag_size, info.max_voices, info.max_fragment_size);
        result = real(handle, params);
        set_real_io(0);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }

    adjusted = *params;
    adjusted.channel = SND_PCM_CHANNEL_PLAYBACK;
    adjusted.mode = SND_PCM_MODE_BLOCK;
    adjusted.format.voices = SQ5_AA_UPMIX_TARGET_CHANNELS;
    adjusted.buf.block.frag_size = (int)target.fragment_bytes;
    if (adjusted.buf.block.frags_max < SQ5_AA_REQUIRED_FRAGS)
        adjusted.buf.block.frags_max = SQ5_AA_REQUIRED_FRAGS;
    if (adjusted.buf.block.frags_min > adjusted.buf.block.frags_max)
        adjusted.buf.block.frags_min = adjusted.buf.block.frags_max;
    result = real(handle, &adjusted);
    if (result != 0) {
        probe_log("aa_upmix.bypass reason=params result=%d why=0x%x", result,
                  (unsigned)adjusted.why_failed);
        result = real(handle, params);
        set_real_io(0);
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
        set_real_io(0);
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
        set_real_io(0);
        pthread_mutex_unlock(&aa.io_lock);
        return result;
    }
    probe_log("aa_upmix.params active source=%uch/%uHz/%u target=%uch/%uHz/%u frags=%d..%d",
              source.channels, source.rate, (unsigned)source.fragment_bytes,
              target.channels, target.rate, (unsigned)target.fragment_bytes,
              adjusted.buf.block.frags_min, adjusted.buf.block.frags_max);
    set_real_io(0);
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
    set_real_io(1);
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
    set_real_io(0);
    pthread_mutex_unlock(&aa.io_lock);
    return result;
}

/* QSA's plugin prepare path calls snd_pcm_plugin_setup through its own PLT.
 * That nested call belongs to libasound and must see the physical PCM6
 * geometry.  Only GAL's outer setup call receives the translated stereo
 * geometry.  Without this ownership guard MU0918 aborts the media sink before
 * its first write: params/readback succeed, phone PCM arrives, then the handle
 * closes with writes=0. */
HOOK_EXPORT int snd_pcm_plugin_prepare(snd_pcm_t *handle, int channel)
{
    static prepare_fn real;
    int expanded, native_channel, result;
    uint64_t call_number;

    if (!real) real = (prepare_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_prepare");
    if (!real) return -EIO;
    if (inside_real_io()) return real(handle, channel);
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded) return real(handle, channel);

    native_channel = SND_PCM_CHANNEL_PLAYBACK;
    pthread_mutex_lock(&aa.io_lock);
    set_real_io(1);
    result = real(handle, native_channel);
    set_real_io(0);
    pthread_mutex_lock(&aa.state_lock);
    call_number = ++aa.prepares;
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    probe_log("aa_upmix.prepare n=%llu requested=%d native=%d result=%d",
              (unsigned long long)call_number, channel, native_channel, result);
    return result;
}

HOOK_EXPORT int snd_pcm_plugin_flush(snd_pcm_t *handle, int channel)
{
    static flush_fn real;
    int expanded, result;
    uint64_t call_number;

    if (!real) real = (flush_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_flush");
    if (!real) return -EIO;
    if (inside_real_io()) return real(handle, channel);
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded) return real(handle, channel);

    pthread_mutex_lock(&aa.io_lock);
    set_real_io(1);
    result = real(handle, SND_PCM_CHANNEL_PLAYBACK);
    set_real_io(0);
    pthread_mutex_lock(&aa.state_lock);
    call_number = ++aa.flushes;
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    probe_log("aa_upmix.flush n=%llu requested=%d native=%d result=%d",
              (unsigned long long)call_number, channel,
              SND_PCM_CHANNEL_PLAYBACK, result);
    return result;
}

/* Keep GAL's queue accounting in the same stereo bytes as its setup.  No
 * io_lock: a status poll must not wait behind a write blocked in QSA. */
static int stereo_status(snd_pcm_t *handle, snd_pcm_channel_status_t *status,
                         status_fn real, const char *api)
{
    int expanded, result;
    int32_t count, free_bytes;
    uint64_t call_number;

    if (!real) return -EIO;
    if (inside_real_io()) return real(handle, status);
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded || !status) return real(handle, status);

    set_real_io(1);
    result = real(handle, status);
    set_real_io(0);
    if (result) return result;
    count = status->count;
    free_bytes = status->free;
    status->count = stereo_bytes(count);
    status->free = stereo_bytes(free_bytes);
    status->scount = (uint32_t)sq5_aa_upmix_source_bytes(status->scount);
    status->subbuffered = (uint32_t)sq5_aa_upmix_source_bytes(status->subbuffered);
    pthread_mutex_lock(&aa.state_lock);
    call_number = ++aa.statuses;
    pthread_mutex_unlock(&aa.state_lock);
    if (call_number <= 8)
        probe_log("aa_upmix.status api=%s n=%llu state=%d count=%d->%d free=%d->%d underrun=%d",
                  api, (unsigned long long)call_number, status->status, count,
                  status->count, free_bytes, status->free, status->underrun);
    return 0;
}

HOOK_EXPORT int snd_pcm_channel_status(snd_pcm_t *handle, snd_pcm_channel_status_t *status)
{
    static status_fn real;
    if (!real) real = (status_fn)dlsym(RTLD_NEXT, "snd_pcm_channel_status");
    return stereo_status(handle, status, real, "channel");
}

HOOK_EXPORT int snd_pcm_plugin_status(snd_pcm_t *handle, snd_pcm_channel_status_t *status)
{
    static status_fn real;
    if (!real) real = (status_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_status");
    return stereo_status(handle, status, real, "plugin");
}

static ssize_t upmix_write(snd_pcm_t *handle, const void *buffer, size_t size,
                           write_fn real, const char *api)
{
    size_t frames, frame_offset, target_bytes;
    ssize_t written;
    int expanded;
    uint64_t write_number;
    if (!real) return -EIO;
    /* snd_pcm_plugin_write may call snd_pcm_write internally.  The outer
     * interposer has already expanded that buffer, so the nested raw boundary
     * must pass through unchanged instead of expanding a second time. */
    if (inside_real_io()) return real(handle, buffer, size);
    pthread_mutex_lock(&aa.state_lock);
    expanded = handle && handle == aa.handle && aa.expanded && aa.dsp_ready;
    pthread_mutex_unlock(&aa.state_lock);
    if (!expanded) return real(handle, buffer, size);
    if (!buffer || size % (SQ5_AA_UPMIX_SOURCE_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES))
        return -EINVAL;
    frames = size / (SQ5_AA_UPMIX_SOURCE_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES);
    if (!frames || frames > SQ5_AA_UPMIX_MAX_OUTPUT_BYTES /
                            (SQ5_AA_UPMIX_TARGET_CHANNELS *
                             SQ5_AA_UPMIX_SAMPLE_BYTES)) {
        probe_log("aa_upmix.write rejected api=%s source_bytes=%u frames=%u reason=capacity",
                  api, (unsigned)size, (unsigned)frames);
        return -EINVAL;
    }
    target_bytes = frames * SQ5_AA_UPMIX_TARGET_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES;

    pthread_mutex_lock(&aa.io_lock);
    pthread_mutex_lock(&aa.state_lock);
    if ((aa.writes % SQ5_AA_LEVEL_POLL_WRITES) == 0u) {
        int level = requested_level();
        if (level != aa.active_level) {
            spa_stereo_upmix_set_level(&aa.dsp, level);
            aa.active_level = level;
            probe_log("aa_upmix.level value=%d source=shared-menu", level);
        }
    }
    for (frame_offset = 0; frame_offset < frames;
         frame_offset += SQ5_AA_UPMIX_MAX_FRAMES) {
        size_t chunk_frames = frames - frame_offset;
        int chunk_result;
        if (chunk_frames > SQ5_AA_UPMIX_MAX_FRAMES)
            chunk_frames = SQ5_AA_UPMIX_MAX_FRAMES;
        chunk_result = spa_stereo_upmix_process_s16(
            &aa.dsp,
            (const int16_t *)buffer + frame_offset * SQ5_AA_UPMIX_SOURCE_CHANNELS,
            chunk_frames,
            aa.output + frame_offset * SQ5_AA_UPMIX_TARGET_CHANNELS,
            chunk_frames);
        if (chunk_result != SPA_STEREO_UPMIX_OK) {
            dry_expand(
                (const int16_t *)buffer +
                    frame_offset * SQ5_AA_UPMIX_SOURCE_CHANNELS,
                chunk_frames,
                aa.output + frame_offset * SQ5_AA_UPMIX_TARGET_CHANNELS);
            aa.failures++;
        }
    }
    write_number = ++aa.writes;
    pthread_mutex_unlock(&aa.state_lock);
    set_real_io(1);
    written = real(handle, aa.output, target_bytes);
    set_real_io(0);
    pthread_mutex_lock(&aa.state_lock);
    if (written >= 0 && (size_t)written < target_bytes) aa.short_writes++;
    if (write_number == 1 || write_number % 1024u == 0)
        probe_log("aa_upmix.write api=%s n=%llu frames=%u result=%d/%u dsp_fail=%llu short=%llu",
                  api, (unsigned long long)write_number, (unsigned)frames,
                  (int)written, (unsigned)target_bytes,
                  (unsigned long long)aa.failures, (unsigned long long)aa.short_writes);
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    if (written <= 0) return written;
    if ((size_t)written >= target_bytes) return (ssize_t)size;
    return (ssize_t)sq5_aa_upmix_source_bytes((size_t)written);
}

HOOK_EXPORT ssize_t snd_pcm_plugin_write(snd_pcm_t *handle, const void *buffer, size_t size)
{
    static write_fn real;
    if (!real) real = (write_fn)dlsym(RTLD_NEXT, "snd_pcm_plugin_write");
    return upmix_write(handle, buffer, size, real, "plugin");
}

HOOK_EXPORT ssize_t snd_pcm_write(snd_pcm_t *handle, const void *buffer, size_t size)
{
    static write_fn real;
    if (!real) real = (write_fn)dlsym(RTLD_NEXT, "snd_pcm_write");
    return upmix_write(handle, buffer, size, real, "raw");
}

HOOK_EXPORT int snd_pcm_close(snd_pcm_t *handle)
{
    static close_fn real;
    int tracked, result;
    uint64_t writes, prepares, flushes, infos, statuses, sad_rewrites;
    uint64_t failures, short_writes;
    if (!real) real = (close_fn)dlsym(RTLD_NEXT, "snd_pcm_close");
    if (!real) return -1;
    pthread_mutex_lock(&aa.state_lock);
    tracked = handle && handle == aa.handle;
    pthread_mutex_unlock(&aa.state_lock);
    if (!tracked) return real(handle);
    pthread_mutex_lock(&aa.io_lock);
    set_real_io(1);
    result = real(handle);
    set_real_io(0);
    pthread_mutex_lock(&aa.state_lock);
    writes = aa.writes; prepares = aa.prepares; flushes = aa.flushes;
    infos = aa.infos; statuses = aa.statuses;
    sad_rewrites = aa.sad_rewrites;
    failures = aa.failures; short_writes = aa.short_writes;
    stop_dsp_locked();
    aa.handle = NULL;
    aa.media_fd = -1;
    aa.expanded = 0;
    aa.source_channels = aa.target_channels = 0;
    aa.source_fragment = aa.target_fragment = 0;
    pthread_mutex_unlock(&aa.state_lock);
    pthread_mutex_unlock(&aa.io_lock);
    probe_log("aa_upmix.close result=%d infos=%llu statuses=%llu sad=%llu prepares=%llu flushes=%llu writes=%llu dsp_fail=%llu short=%llu", result,
              (unsigned long long)infos, (unsigned long long)statuses,
              (unsigned long long)sad_rewrites,
              (unsigned long long)prepares, (unsigned long long)flushes,
              (unsigned long long)writes, (unsigned long long)failures,
              (unsigned long long)short_writes);
    return result;
}
