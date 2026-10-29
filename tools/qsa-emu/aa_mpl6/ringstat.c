/* Summarise the DMA ring fragments luka's csd_stub saved under /tmp.
 * Each /tmp/resmgr_dma_<ring>_<ringbytes>.bin holds the accepted resource-
 * manager writes, back to back.  Ring = 10 fragments of 768 frames, so the
 * voice count and sample width follow from the fragment size.  For every
 * 8 fragments print the RMS of each channel (high 16 bits for S32). */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    DIR *d = opendir("/tmp");
    struct dirent *e;
    if (!d) { puts("RINGSTAT no /tmp"); return 1; }
    while ((e = readdir(d))) {
        char path[256];
        unsigned ring, ringbytes, frag, width, voices, k, ch;
        FILE *f;
        long size, frames, fr;
        double acc[8];
        if (sscanf(e->d_name, "resmgr_dma_%u_%u.bin", &ring, &ringbytes) != 2) continue;
        snprintf(path, sizeof path, "/tmp/%s", e->d_name);
        f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        frag = ringbytes / 10;
        /* 768 frames per fragment: bytes per frame = frag / 768. */
        width = 0; voices = 0;
        if (frag % 768 == 0) {
            unsigned bpf = frag / 768;
            if (bpf % 4 == 0 && bpf / 4 >= 1 && bpf / 4 <= 8) { width = 4; voices = bpf / 4; }
            else if (bpf % 2 == 0) { width = 2; voices = bpf / 2; }
        }
        printf("RINGSTAT file=%s bytes=%ld frag=%u width=%u voices=%u\n",
               e->d_name, size, frag, width, voices);
        if (!voices || voices > 8) { fclose(f); continue; }
        frames = size / (long)(width * voices);
        memset(acc, 0, sizeof acc);
        for (fr = 0, k = 0; fr < frames; fr++) {
            for (ch = 0; ch < voices; ch++) {
                double v;
                if (width == 4) {
                    int32_t s; if (fread(&s, 4, 1, f) != 1) goto out;
                    v = (double)(int16_t)((uint32_t)s >> 16);
                } else {
                    int16_t s; if (fread(&s, 2, 1, f) != 1) goto out;
                    v = s;
                }
                acc[ch] += v * v;
            }
            if ((fr + 1) % (768 * 8) == 0 || fr + 1 == frames) {
                long n = (fr % (768 * 8)) + 1;
                printf("RINGSTAT ring=%u frags=%3ld..%3ld rms", ring,
                       (long)(k * 8), (fr) / 768);
                for (ch = 0; ch < voices; ch++) {
                    printf(" c%u=%6.0f", ch, sqrt(acc[ch] / n));
                    acc[ch] = 0;
                }
                printf("\n");
                k++;
            }
        }
out:
        fclose(f);
    }
    closedir(d);
    return 0;
}
