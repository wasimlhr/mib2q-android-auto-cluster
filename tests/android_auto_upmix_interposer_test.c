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
static unsigned native_params_calls;
static unsigned native_write_calls;
static size_t native_write_bytes;
static unsigned native_prepare_calls;
static unsigned native_flush_calls;
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
    info->min_fragment_size = 3072;
    info->max_fragment_size = (int)caps_fragment;
    info->formats = 1u << SND_PCM_SFMT_S16_LE;
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
    int16_t pcm[768 * 2] = { 0 };
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

    /* Stock MU0918 caps: pass GAL's exact stereo request through. */
    params = stereo_params();
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(native_setup_state.format.voices == 2);
    assert(native_setup_state.buf.block.frag_size == 9216);
    assert(!aa.expanded);

    /* Validated/patched caps: expand QSA only; GAL still sees stereo. */
    caps_voices = 6;
    caps_fragment = 9216;
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
    assert(snd_pcm_plugin_flush(handle, SND_PCM_CHANNEL_CAPTURE) == 0);
    assert(native_flush_calls == 1 && aa.flushes == 1);

    /* Reconfiguration to an unsupported format must remove all stale 6ch
     * state before falling back to the native request. */
    params = stereo_params();
    params.format.rate = 44100;
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(!aa.expanded && native_setup_state.format.voices == 2);
    assert(snd_pcm_plugin_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(native_write_calls == 3 && native_write_bytes == sizeof(pcm));
    assert(native_params_calls == 3);
    assert(snd_pcm_close(handle) == 0);
    assert(native_flush_calls == 2);
    assert(unlink(level_path) == 0);
    assert(!aa.expanded && aa.handle == NULL);
    puts("android_auto_upmix_interposer_test: PASS");
    return 0;
}
