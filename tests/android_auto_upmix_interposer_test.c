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

static ssize_t native_write(snd_pcm_t *handle, const void *data, size_t bytes)
{
    assert(handle == &media && data);
    ++native_write_calls;
    native_write_bytes = bytes;
    return (ssize_t)bytes;
}

static int native_close(snd_pcm_t *handle)
{
    assert(handle == &media);
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
    SYMBOL("snd_pcm_plugin_write", native_write);
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
    params.buf.block.frag_size = 3072;
    params.buf.block.frags_min = 2;
    params.buf.block.frags_max = 4;
    return params;
}

int main(void)
{
    snd_pcm_t *handle = NULL;
    snd_pcm_channel_params_t params;
    snd_pcm_channel_setup_t setup;
    int16_t pcm[768 * 2] = { 0 };

    assert(setenv("SQ5_AA_UPMIX", "on", 1) == 0);
    assert(unsetenv("SQ5_AA_UPMIX_MASK") == 0);
    assert(snd_pcm_open_name(&handle, "mpl6_gal_ent", SND_PCM_OPEN_PLAYBACK) == 0);
    assert(handle == &media);

    /* Stock MU0918 caps: pass GAL's exact stereo request through. */
    params = stereo_params();
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(native_setup_state.format.voices == 2);
    assert(native_setup_state.buf.block.frag_size == 3072);
    assert(!aa.expanded);

    /* Validated/patched caps: expand QSA only; GAL still sees stereo. */
    caps_voices = 6;
    caps_fragment = 9216;
    params = stereo_params();
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(native_setup_state.format.voices == 6);
    assert(native_setup_state.buf.block.frag_size == 9216);
    assert(native_setup_state.buf.block.frags_min == 4);
    assert(native_setup_state.buf.block.frags_max == 6);
    memset(&setup, 0, sizeof(setup));
    assert(snd_pcm_plugin_setup(handle, &setup) == 0);
    assert(setup.format.voices == 2 && setup.buf.block.frag_size == 3072);

    assert(snd_pcm_plugin_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(native_write_calls == 1 && native_write_bytes == 9216);

    /* Reconfiguration to an unsupported format must remove all stale 6ch
     * state before falling back to the native request. */
    params = stereo_params();
    params.format.rate = 44100;
    assert(snd_pcm_plugin_params(handle, &params) == 0);
    assert(!aa.expanded && native_setup_state.format.voices == 2);
    assert(snd_pcm_plugin_write(handle, pcm, sizeof(pcm)) == (ssize_t)sizeof(pcm));
    assert(native_write_calls == 2 && native_write_bytes == sizeof(pcm));
    assert(native_params_calls == 3);
    assert(snd_pcm_close(handle) == 0);
    assert(!aa.expanded && aa.handle == NULL);
    puts("android_auto_upmix_interposer_test: PASS");
    return 0;
}
