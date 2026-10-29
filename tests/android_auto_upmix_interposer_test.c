/* Exercise the real Android Auto QSA interposer against fake native QSA
 * boundaries.  This proves the fail-closed stock path, six-channel geometry,
 * caller-facing stereo setup and write-byte translation. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *test_dlsym(void *, const char *);
#define dlsym test_dlsym
#include "../android_auto/hook/src/audio_upmix.c"
#undef dlsym

static snd_pcm_t media = { 1 };
static snd_pcm_channel_setup_t native_setup_state;
static unsigned caps_voices = 2;
static unsigned caps_fragment = 3072;
static unsigned caps_min_fragment = 3072;
static unsigned caps_buffer = 0x7800;
static unsigned native_params_calls;
static unsigned native_write_calls;
static size_t native_write_bytes;
static unsigned native_prepare_calls;
static unsigned native_flush_calls;
static unsigned native_devctl_calls;
static int native_devctl_fd;
static uint32_t native_devctl_command;
static size_t native_devctl_bytes;
static uint8_t native_devctl_payload[SQ5_AA_SAD_BYTES];
static int nested_setup_voices;

void probe_log(const char *format, ...)
{
    (void)format;
}

static int native_open(snd_pcm_t **handle, char *name, int mode)
{
    (void)name;
    (void)mode;
    *handle = &media;
    return 0;
}

static int native_info(snd_pcm_t *handle, snd_pcm_channel_info_t *info)
{
    assert(handle == &media);
    info->min_voices = 2;
    info->max_voices = (int)caps_voices;
    info->min_rate = info->max_rate = 48000;
    info->min_fragment_size = (int)caps_min_fragment;
    info->max_fragment_size = (int)caps_fragment;
    info->max_buffer_size = (int)caps_buffer;
    info->fragment_align = 4;
    info->formats = 1u << SND_PCM_SFMT_S16_LE;
    return 0;
}

static int native_plugin_info(snd_pcm_t *handle, snd_pcm_channel_info_t *info)
{
    /* libasound's plugin info reads the channel record through its PLT; the
     * interposer must translate the outer result exactly once. */
    return snd_pcm_channel_info(handle, info);
}

static int native_status(snd_pcm_t *handle, snd_pcm_channel_status_t *status)
{
    assert(handle == &media && status);
    status->status = SND_PCM_STATUS_RUNNING;
    status->count = 9216;
    status->free = 18432;
    status->scount = 92160;
    return 0;
}

static int native_fd(snd_pcm_t *handle, int channel)
{
    assert(handle == &media);
    assert(channel == SND_PCM_CHANNEL_PLAYBACK);
    return 73;
}

static int native_devctl(int fd, int command, void *data, size_t bytes, int *info)
{
    ++native_devctl_calls;
    native_devctl_fd = fd;
    native_devctl_command = (uint32_t)command;
    native_devctl_bytes = bytes;
    memset(native_devctl_payload, 0, sizeof(native_devctl_payload));
    if (data && bytes <= sizeof(native_devctl_payload))
        memcpy(native_devctl_payload, data, bytes);
    if (info) *info = 19;
    return 0;
}

static int native_params(snd_pcm_t *handle, snd_pcm_channel_params_t *params)
{
    assert(handle == &media && params);
    ++native_params_calls;
    memset(&native_setup_state, 0, sizeof(native_setup_state));
    native_setup_state.channel = params->channel;
    native_setup_state.mode = params->mode;
    native_setup_state.format = params->format;
    native_setup_state.buf.block = params->buf.block;
    return 0;
}

static int native_setup(snd_pcm_t *handle, snd_pcm_channel_setup_t *setup)
{
    assert(handle == &media && setup);
    *setup = native_setup_state;
    return 0;
}

static int native_prepare(snd_pcm_t *handle, int channel)
{
    snd_pcm_channel_setup_t setup;
    assert(handle == &media);
    assert(channel == SND_PCM_CHANNEL_PLAYBACK);
    ++native_prepare_calls;
    memset(&setup, 0, sizeof(setup));
    setup.channel = channel;
    /* Reproduce QSA's PLT callback during prepare.  The interposer must let
     * this nested libasound call see physical six-channel geometry. */
    assert(snd_pcm_plugin_setup(handle, &setup) == 0);
    nested_setup_voices = setup.format.voices;
    return nested_setup_voices == 6 ? 0 : -1;
}

static int native_flush(snd_pcm_t *handle, int channel)
{
    assert(handle == &media);
    assert(channel == SND_PCM_CHANNEL_PLAYBACK);
    ++native_flush_calls;
    return 0;
}

static ssize_t native_raw_write(snd_pcm_t *handle, const void *data, size_t bytes)
{
    assert(handle == &media && data);
    ++native_write_calls;
    native_write_bytes = bytes;
    return (ssize_t)bytes;
}

static ssize_t native_plugin_write(snd_pcm_t *handle, const void *data, size_t bytes)
{
    /* Match QSA's plugin boundary calling into the raw boundary.  The
     * interposer must not expand the already-six-channel buffer twice. */
    return snd_pcm_write(handle, data, bytes);
}

static int native_close(snd_pcm_t *handle)
{
    assert(handle == &media);
    /* Reproduce libasound's close-adjacent PLT callback.  The close wrapper
     * must mark the real QSA call as owned so this does not relock. */
    assert(snd_pcm_plugin_flush(handle, SND_PCM_CHANNEL_PLAYBACK) == 0);
    return 0;
}

static void *test_dlsym(void *scope, const char *name)
{
    (void)scope;
#define SYMBOL(n, f) if (!strcmp(name, n)) return (void *)(uintptr_t)(f)
    SYMBOL("snd_pcm_open_name", native_open);
    SYMBOL("snd_pcm_channel_info", native_info);
    SYMBOL("snd_pcm_plugin_info", native_plugin_info);
    SYMBOL("snd_pcm_channel_status", native_status);
    SYMBOL("snd_pcm_plugin_status", native_status);
    SYMBOL("snd_pcm_file_descriptor", native_fd);
    SYMBOL("devctl", native_devctl);
    SYMBOL("snd_pcm_plugin_params", native_params);
    SYMBOL("snd_pcm_plugin_setup", native_setup);
    SYMBOL("snd_pcm_plugin_prepare", native_prepare);
    SYMBOL("snd_pcm_plugin_flush", native_flush);
    SYMBOL("snd_pcm_plugin_write", native_plugin_write);
    SYMBOL("snd_pcm_write", native_raw_write);
    SYMBOL("snd_pcm_close", native_close);
#undef SYMBOL
    return NULL;
}

static snd_pcm_channel_params_t stereo_params(void)
{
    snd_pcm_channel_params_t params;
    memset(&params, 0, sizeof(params));
    params.channel = SND_PCM_CHANNEL_PLAYBACK;
    params.mode = SND_PCM_MODE_BLOCK;
    params.format.format = SND_PCM_SFMT_S16_LE;
    params.format.rate = 48000;
    params.format.voices = 2;
    /* MU0918 GAL seeds this with the patched sink's physical maximum. The
     * interposer must report a 3072-byte logical stereo period back to GAL. */
    params.buf.block.frag_size = 9216;
    params.buf.block.frags_min = 2;
    params.buf.block.frags_max = 4;
    return params;
}

int main(void)
{
    const char *level_path = "/tmp/sq5-aa-upmix-test-level";
    snd_pcm_t *handle = NULL;
    snd_pcm_channel_params_t params;
    snd_pcm_channel_setup_t setup;
    snd_pcm_channel_info_t info;
    snd_pcm_channel_status_t status;
    uint8_t sad[SQ5_AA_SAD_BYTES] = { 0 };
    uint8_t wrong_sad[SQ5_AA_SAD_BYTES] = { 0 };
    int16_t pcm[768 * 2] = { 0 };
    int16_t gal_packet[2048 * 2] = { 0 };
    FILE *level_file;

    assert(setenv("SQ5_AA_UPMIX", "on", 1) == 0);
    assert(unsetenv("SQ5_AA_UPMIX_MASK") == 0);
    assert(setenv("SQ5_AA_UPMIX_LEVEL_FILE", level_path, 1) == 0);
    level_file = fopen(level_path, "w");
    assert(level_file);
    assert(fputs("1\n", level_file) >= 0);
    assert(fclose(level_file) == 0);
    assert(snd_pcm_open_name(&handle, "mpl6_gal_ent", SND_PCM_OPEN_PLAYBACK) == 0);
    assert(handle == &media);
    assert(snd_pcm_file_descriptor(handle, SND_PCM_CHANNEL_PLAYBACK) == 73);
    assert(aa.media_fd == 73);
    sad[1] = 1;
    sad[3] = SQ5_AA_SAD_STEREO;
    sad[12] = SQ5_AA_SAMPLE_WIDTH;
    store_le32(sad + 8, SQ5_AA_STEREO_BITRATE);

    /* The header must remain stereo until the physical six-channel setup has
     * succeeded. This prevents a failed upmix negotiation from lying to CSD. */
    assert(devctl(73, (int)SQ5_AA_SAD_SET_COMMAND, sad, sizeof(sad), NULL) == 0);
    assert(native_devctl_calls == 1 && native_devctl_payload[3] == SQ5_AA_SAD_STEREO);
    assert(aa.sad_rewrites == 0);

    /* Stock MU0918 caps: GAL's info and its exact stereo request pass through. */
    memset(&info, 0, sizeof(info));
    assert(snd_pcm_channel_info(handle, &info) == 0);
    assert(info.max_voices == 2 && info.max_fragment_size == 3072 && aa.infos == 0);
    params = stereo_params();
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(native_setup_state.format.voices == 2);
    assert(native_setup_state.buf.block.frag_size == 9216);
    assert(!aa.expanded);
    memset(&status, 0, sizeof(status));
    assert(snd_pcm_plugin_status(handle, &status) == 0);
    assert(status.count == 9216 && aa.statuses == 0);

    /* Patched MU0918 caps: GAL is shown the stereo device it is served, so it
     * requests the stock 3072-byte period (car run 310 failed on 9216). */
    caps_voices = 6;
    caps_fragment = caps_min_fragment = 9216;
    caps_buffer = 0x16800;
    memset(&info, 0, sizeof(info));
    assert(snd_pcm_channel_info(handle, &info) == 0);
    assert(info.min_voices == 2 && info.max_voices == 2);
    assert(info.min_fragment_size == 3072 && info.max_fragment_size == 3072);
    assert(info.max_buffer_size == 30720 && info.fragment_align == 4);
    memset(&info, 0, sizeof(info));
    assert(snd_pcm_plugin_info(handle, &info) == 0);
    assert(info.max_voices == 2 && info.max_fragment_size == 3072);
    assert(aa.infos == 2);
    params = stereo_params();
    params.buf.block.frag_size = info.max_fragment_size;
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(aa.expanded && aa.source_fragment == 3072 && aa.target_fragment == 9216);
    assert(native_setup_state.format.voices == 6);
    assert(native_setup_state.buf.block.frag_size == 9216);

    /* Exact stock GAL metadata becomes a truthful six-slot declaration. The
     * caller's buffer is immutable from GAL's point of view. */
    assert(devctl(73, (int)SQ5_AA_SAD_SET_COMMAND, sad, sizeof(sad), NULL) == 0);
    assert(native_devctl_calls == 2 && native_devctl_fd == 73);
    assert(native_devctl_command == SQ5_AA_SAD_SET_COMMAND);
    assert(native_devctl_bytes == SQ5_AA_SAD_BYTES);
    assert(native_devctl_payload[3] == SQ5_AA_SAD_5_1);
    assert(load_le32(native_devctl_payload + 8) == SQ5_AA_5_1_BITRATE);
    assert(native_devctl_payload[12] == SQ5_AA_SAMPLE_WIDTH);
    assert(sad[3] == SQ5_AA_SAD_STEREO);
    assert(load_le32(sad + 8) == SQ5_AA_STEREO_BITRATE);
    assert(aa.sad_rewrites == 1);

    /* Wrong descriptor, command, or payload is never rewritten. */
    assert(devctl(74, (int)SQ5_AA_SAD_SET_COMMAND, sad, sizeof(sad), NULL) == 0);
    assert(native_devctl_payload[3] == SQ5_AA_SAD_STEREO);
    memcpy(wrong_sad, sad, sizeof(wrong_sad));
    wrong_sad[1] = 2;
    assert(devctl(73, (int)SQ5_AA_SAD_SET_COMMAND,
                  wrong_sad, sizeof(wrong_sad), NULL) == 0);
    assert(native_devctl_payload[3] == SQ5_AA_SAD_STEREO);
    assert(devctl(73, (int)(SQ5_AA_SAD_SET_COMMAND - 1u),
                  sad, sizeof(sad), NULL) == 0);
    assert(native_devctl_payload[3] == SQ5_AA_SAD_STEREO);
    assert(aa.sad_rewrites == 1);
    memset(&setup, 0, sizeof(setup));
    assert(snd_pcm_plugin_setup(handle, &setup) == 0);
    assert(setup.format.voices == 2 && setup.buf.block.frag_size == 3072);
    memset(&status, 0, sizeof(status));
    assert(snd_pcm_plugin_status(handle, &status) == 0);
    assert(status.status == SND_PCM_STATUS_RUNNING);
    assert(status.count == 3072 && status.free == 6144 && status.scount == 30720);
    memset(&status, 0, sizeof(status));
    assert(snd_pcm_channel_status(handle, &status) == 0);
    assert(status.count == 3072 && aa.statuses == 2);

    /* An older GAL request seeded with the physical maximum still expands. */
    params = stereo_params();
    /* MU0918 GAL leaves these at default values.  The exact playback handle
     * is already known, so the interposer must normalize its private copy. */
    params.channel = SND_PCM_CHANNEL_CAPTURE;
    params.mode = SND_PCM_MODE_UNKNOWN;
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(native_setup_state.channel == SND_PCM_CHANNEL_PLAYBACK);
    assert(native_setup_state.mode == SND_PCM_MODE_BLOCK);
    assert(native_setup_state.format.voices == 6);
    assert(native_setup_state.buf.block.frag_size == 9216);
    assert(native_setup_state.buf.block.frags_min == 2);
    assert(native_setup_state.buf.block.frags_max == 8);
    assert(aa.active_level == 1);
    memset(&setup, 0, sizeof(setup));
    assert(snd_pcm_plugin_setup(handle, &setup) == 0);
    assert(setup.format.voices == 2 && setup.buf.block.frag_size == 3072);
    assert(snd_pcm_plugin_prepare(handle, SND_PCM_CHANNEL_CAPTURE) == 0);
    assert(native_prepare_calls == 1 && nested_setup_voices == 6);
    assert(aa.prepares == 1);

    level_file = fopen(level_path, "w");
    assert(level_file);
    assert(fputs("4\n", level_file) >= 0);
    assert(fclose(level_file) == 0);
    assert(snd_pcm_plugin_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(aa.active_level == 4);
    assert(native_write_calls == 1 && native_write_bytes == 9216);
    assert(snd_pcm_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(native_write_calls == 2 && native_write_bytes == 9216);
    /* Run 310 delivered 8202-byte channel messages: a 10-byte envelope plus
     * 8192 bytes/2048 stereo frames of PCM.  The DSP processes this in two
     * bounded calls, but QSA must receive one expanded 24576-byte write. */
    assert(snd_pcm_plugin_write(handle, gal_packet, sizeof(gal_packet)) ==
           (ssize_t)sizeof(gal_packet));
    assert(native_write_calls == 3 && native_write_bytes == 24576);
    assert(snd_pcm_plugin_flush(handle, SND_PCM_CHANNEL_CAPTURE) == 0);
    assert(native_flush_calls == 1 && aa.flushes == 1);

    /* Reconfiguration to an unsupported format must remove all stale 6ch
     * state before falling back to the native request. */
    params = stereo_params();
    params.format.rate = 44100;
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(!aa.expanded && native_setup_state.format.voices == 2);
    assert(snd_pcm_plugin_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(native_write_calls == 4 && native_write_bytes == sizeof(pcm));
    assert(native_params_calls == 4);
    assert(snd_pcm_close(handle) == 0);
    assert(native_flush_calls == 2);
    assert(unlink(level_path) == 0);
    assert(!aa.expanded && aa.handle == NULL);
    puts("android_auto_upmix_interposer_test: PASS");
    return 0;
}
