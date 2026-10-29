/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only QSA capability query for Android Auto's media device.
 *
 * This opens mpl6_gal_ent and calls snd_pcm_channel_info only. It never submits
 * parameters, prepares, starts, or writes to the device.
 */
#include <stdio.h>
#include <string.h>
#include <sys/asoundlib.h>

int main(int argc, char **argv)
{
    char *name = argc > 1 ? argv[1] : "mpl6_gal_ent";
    snd_pcm_t *pcm = 0;
    snd_pcm_channel_info_t info;
    int opened = snd_pcm_open_name(&pcm, name, SND_PCM_OPEN_PLAYBACK);
    int queried = -1;

    printf("device=%s\n", name);
    printf("open_result=%d\n", opened);
    if (opened != 0 || !pcm) return 2;

    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    queried = snd_pcm_channel_info(pcm, &info);
    printf("info_result=%d\n", queried);
    if (queried == 0) {
        printf("voices_min=%d\nvoices_max=%d\n", info.min_voices, info.max_voices);
        printf("rate_min=%d\nrate_max=%d\n", info.min_rate, info.max_rate);
        printf("fragment_min=%d\nfragment_max=%d\n",
               info.min_fragment_size, info.max_fragment_size);
        printf("formats=0x%x\n", (unsigned)info.formats);
    }
    snd_pcm_close(pcm);
    return queried == 0 ? 0 : 3;
}
