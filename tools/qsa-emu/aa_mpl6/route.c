/* List card and mixer switches and their raw values; optionally write one.
 *   route                         list everything
 *   route <name> <off>=<byte>...  write raw bytes into the named mixer switch
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/asoundlib.h>

static void dump(const char *kind, snd_switch_t *sw, int rc)
{
    int j;
    printf("ROUTE %s '%s' rc=%d type=%u subtype=%u raw", kind, sw->name, rc,
           (unsigned)sw->type, (unsigned)sw->subtype);
    for (j = 0; j < 32; j++) printf("%s%02x", (j % 4) ? "" : " ", sw->value.raw[j]);
    printf("\n");
}

int main(int argc, char **argv)
{
    snd_ctl_t *ctl = NULL;
    snd_switch_list_t list;
    snd_switch_list_item_t items[64];
    snd_switch_t sw;
    int rc, i, pass;

    setvbuf(stdout, 0, _IONBF, 0);
    rc = snd_ctl_open(&ctl, 0);
    printf("ROUTE ctl_open -> %d\n", rc);
    if (rc) return 1;

    if (argc > 2) {
        memset(&sw, 0, sizeof(sw));
        strncpy(sw.name, argv[1], sizeof(sw.name) - 1);
        rc = snd_ctl_mixer_switch_read(ctl, 0, &sw);
        dump("before", &sw, rc);
        for (i = 2; i < argc; i++) {
            unsigned off, val;
            if (sscanf(argv[i], "%u=%u", &off, &val) == 2 && off < 32)
                sw.value.raw[off] = (uint8_t)val;
        }
        rc = snd_ctl_mixer_switch_write(ctl, 0, &sw);
        printf("ROUTE write '%s' -> %d\n", sw.name, rc);
        memset(sw.value.raw, 0, sizeof(sw.value.raw));
        rc = snd_ctl_mixer_switch_read(ctl, 0, &sw);
        dump("after", &sw, rc);
        snd_ctl_close(ctl);
        return 0;
    }

    for (pass = 0; pass < 2; pass++) {
        memset(&list, 0, sizeof(list));
        memset(items, 0, sizeof(items));
        list.pswitches = items;
        list.switches_size = 64;
        rc = pass ? snd_ctl_mixer_switch_list(ctl, 0, &list) : snd_ctl_switch_list(ctl, &list);
        printf("ROUTE %s list -> %d switches=%d over=%d\n", pass ? "mixer" : "card",
               rc, list.switches, list.switches_over);
        for (i = 0; !rc && i < list.switches && i < 64; i++) {
            memset(&sw, 0, sizeof(sw));
            strncpy(sw.name, items[i].name, sizeof(sw.name) - 1);
            rc = pass ? snd_ctl_mixer_switch_read(ctl, 0, &sw) : snd_ctl_switch_read(ctl, &sw);
            dump(pass ? "mixer" : "card", &sw, rc);
            rc = 0;
        }
    }
    snd_ctl_close(ctl);
    return 0;
}
