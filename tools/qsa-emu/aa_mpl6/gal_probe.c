/* GAL-shaped QSA client for the QEMU audio harness.
 *
 * Does what MU0918 GAL does on mpl6_gal_ent (gal.json media sink: pluginAware,
 * fragsMin/Max 1, 8192-byte stereo packets): open by name, size the period
 * from snd_pcm_plugin_info, plugin params S16/48k/2ch, setup, SAD SET devctl,
 * prepare, write 2048-frame packets, status, close.  Run it with and without
 * the Android Auto upmix interposer preloaded.
 *
 * Signal: packets 0..N/2-1 carry a 1 kHz tone on LEFT only, the rest a 1 kHz
 * tone on RIGHT only, so the six captured channels show where each side goes.
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <devctl.h>
#include <sys/asoundlib.h>

#define PACKET_FRAMES 2048
#define PACKETS 32
#define SAD_SET 0x8018fa01

static void info_line(const char *what, const snd_pcm_channel_info_t *i)
{
    printf("GALPROBE %s voices=%d..%d rate=%d..%d frag=%d..%d buffer=%d formats=0x%x\n",
           what, i->min_voices, i->max_voices, i->min_rate, i->max_rate,
           i->min_fragment_size, i->max_fragment_size, i->max_buffer_size,
           (unsigned)i->formats);
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "mpl6_gal_ent";
    snd_pcm_t *pcm = NULL;
    snd_pcm_channel_info_t info;
    snd_pcm_channel_params_t params;
    snd_pcm_channel_setup_t setup;
    snd_pcm_channel_status_t status;
    static int16_t packet[PACKET_FRAMES * 2];
    unsigned char sad[24];
    int rc, fd, p, f, written = 0, retries = 0, info_dev;
    long sample = 0;

    /* The serial console is shared with io-audio's stub tracing; keep this
     * run's lines (and the hook's, which also go to stdout) in a file. */
    if (argc > 2 && !freopen(argv[2], "w", stdout)) return 9;
    setvbuf(stdout, 0, _IONBF, 0);
    rc = snd_pcm_open_name(&pcm, (char *)name, SND_PCM_OPEN_PLAYBACK);
    printf("GALPROBE open %s -> %d\n", name, rc);
    if (rc) return 1;
    /* Harness-only: the guest has no mmap transport (plugin params would
     * fail ENOENT), exactly as luka's qsa_probe found.  GAL on the car uses
     * whatever libasound picks; the plugin conversion chain is the same. */
    snd_pcm_plugin_set_disable(pcm, PLUGIN_DISABLE_MMAP);

    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    rc = snd_pcm_channel_info(pcm, &info);
    printf("GALPROBE channel_info -> %d\n", rc);
    info_line("channel_info", &info);
    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    rc = snd_pcm_plugin_info(pcm, &info);
    printf("GALPROBE plugin_info -> %d\n", rc);
    info_line("plugin_info", &info);
    info_dev = info.max_fragment_size;

    /* GAL leaves channel/mode at defaults; the interposer normalises them. */
    memset(&params, 0, sizeof(params));
    params.channel = SND_PCM_CHANNEL_PLAYBACK;
    params.mode = SND_PCM_MODE_BLOCK;
    params.start_mode = SND_PCM_START_FULL;
    params.stop_mode = SND_PCM_STOP_STOP;
    params.format.interleave = 1;
    params.format.format = SND_PCM_SFMT_S16_LE;
    params.format.rate = 48000;
    params.format.voices = 2;
    params.buf.block.frag_size = info_dev;
    params.buf.block.frags_min = 1;
    params.buf.block.frags_max = 1;
    rc = snd_pcm_plugin_params(pcm, &params);
    printf("GALPROBE plugin_params frag=%d -> %d why=0x%x\n", info_dev, rc,
           (unsigned)params.why_failed);
    if (rc) return 2;

    memset(&setup, 0, sizeof(setup));
    setup.channel = SND_PCM_CHANNEL_PLAYBACK;
    rc = snd_pcm_plugin_setup(pcm, &setup);
    printf("GALPROBE plugin_setup -> %d voices=%d rate=%d format=%d frag=%d frags=%d\n",
           rc, setup.format.voices, setup.format.rate, setup.format.format,
           setup.buf.block.frag_size, setup.buf.block.frags);

    /* GAL's stock stereo SAD header (asad c0, 1536 kbit/s, 16 bit). */
    fd = snd_pcm_file_descriptor(pcm, SND_PCM_CHANNEL_PLAYBACK);
    memset(sad, 0, sizeof(sad));
    /* GAL/deva layout: pcm_conf(asad) +1, codec_type +3, width +4, rate +8,
     * bit_rate +16 (measured with GALPROBE_SAD_PATTERN). */
    sad[1] = 0xc0;
    sad[3] = 0x01;
    sad[4] = 0x10;
    sad[8] = 0x80; sad[9] = 0xbb;
    sad[16] = 0x00; sad[17] = 0x70; sad[18] = 0x17; sad[19] = 0x00;
    /* Layout probe: a distinct byte at every offset, so the driver's own
     * convert_sad_header_to_csd log shows which offset feeds which field. */
    if (getenv("GALPROBE_SAD_PATTERN")) {
        int i;
        for (i = 0; i < (int)sizeof(sad); i++) sad[i] = (unsigned char)(0x40 + i);
    }
    rc = devctl(fd, SAD_SET, sad, sizeof(sad), NULL);
    printf("GALPROBE sad_set fd=%d -> %d (%s)\n", fd, rc, rc ? strerror(rc) : "ok");

    rc = snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK);
    printf("GALPROBE prepare -> %d\n", rc);

    for (p = 0; p < PACKETS; p++) {
        int right = p >= PACKETS / 2;
        ssize_t w;
        for (f = 0; f < PACKET_FRAMES; f++, sample++) {
            int16_t v = (int16_t)(12000.0 * sin(2.0 * M_PI * 1000.0 * sample / 48000.0));
            packet[2 * f] = right ? 0 : v;
            packet[2 * f + 1] = right ? v : 0;
        }
        for (;;) {
            w = snd_pcm_plugin_write(pcm, packet, sizeof(packet));
            if (w == (ssize_t)sizeof(packet)) break;
            /* No DSP drains the ring in this guest; recover like qsa_probe. */
            memset(&status, 0, sizeof(status));
            status.channel = SND_PCM_CHANNEL_PLAYBACK;
            snd_pcm_plugin_status(pcm, &status);
            if (retries < 4)
                printf("GALPROBE write %d -> %d status=%d count=%d free=%d; prepare\n",
                       p, (int)w, status.status, status.count, status.free);
            if (++retries > 400 ||
                snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK) < 0) {
                printf("GALPROBE giving up at packet %d\n", p);
                goto done;
            }
            if (w > 0) break;  /* partial: QSA keeps the rest; move on */
        }
        written++;
    }
done:
    memset(&status, 0, sizeof(status));
    status.channel = SND_PCM_CHANNEL_PLAYBACK;
    rc = snd_pcm_plugin_status(pcm, &status);
    printf("GALPROBE wrote %d packets (%d retries); status -> %d state=%d count=%d free=%d scount=%u\n",
           written, retries, rc, status.status, status.count, status.free,
           (unsigned)status.scount);
    snd_pcm_plugin_flush(pcm, SND_PCM_CHANNEL_PLAYBACK);
    rc = snd_pcm_close(pcm);
    printf("GALPROBE close -> %d\n", rc);
    return 0;
}
