/* Symbols MU0918 deva-ctrl-qc.so imports that the SDP io-audio/libc lack.
 * Linked into the harness libcsdIpcClient.so next to luka's csd_stub.c. */
#include <stdio.h>

/* Harman io-audio additions: serialise subchannel state changes.  One client
 * in this guest, so serialisation is moot. */
void ado_pcm_subchn_state_lock(void *subchn) { (void)subchn; }
void ado_pcm_subchn_state_unlock(void *subchn) { (void)subchn; }

/* PPS config parsing: report "no more data" (PPS_END). */
int ppsparse(char **ppsdata, const char * const *objnames,
             const char * const *attrnames, char **vals, int *misc)
{
    (void)ppsdata; (void)objnames; (void)attrnames; (void)vals; (void)misc;
    fprintf(stderr, "extra_stub: ppsparse called\n");
    return 0;
}

int rtpd_open(void) { return -1; }
int rtpd_close(int h) { (void)h; return 0; }
int rtpd_read(int h, void *b, int n) { (void)h; (void)b; (void)n; return -1; }
int rtpd_write(int h, const void *b, int n) { (void)h; (void)b; (void)n; return -1; }
int rtpd_ioctl(int h, int c, void *b, int n) { (void)h; (void)c; (void)b; (void)n; return -1; }
