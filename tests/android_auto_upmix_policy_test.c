#include <assert.h>
#include <stdio.h>
#include "android_auto/hook/src/audio_upmix_policy.h"

int main(void)
{
    struct sq5_aa_pcm_geometry source = {48000, 2, 3072}, target = {0};
    struct sq5_aa_pcm_caps stock = {2, 3072};
    struct sq5_aa_pcm_caps patched = {6, 9216};

    assert(sq5_aa_upmix_select_source_fragment(3072, &patched) == 3072);
    assert(sq5_aa_upmix_select_source_fragment(9216, &patched) == 3072);
    assert(sq5_aa_upmix_select_source_fragment(3073, &patched) == 0);
    assert(sq5_aa_upmix_select_source_fragment(3072, &stock) == 0);
    assert(!sq5_aa_upmix_plan(&source, &stock, &target));
    assert(sq5_aa_upmix_plan(&source, &patched, &target));
    assert(target.rate == 48000 && target.channels == 6 && target.fragment_bytes == 9216);
    assert(sq5_aa_upmix_source_bytes(9216) == 3072);
    assert(sq5_aa_upmix_source_bytes(4608) == 1536);

    /* deva's one-bit hardware format: S16 counts as-is, the mpl1 mirror's
     * S32 0x4800 fragment is QSA's 9216-byte S16 period (768 frames). */
    assert(sq5_aa_upmix_device_sample_bytes(SQ5_AA_UPMIX_FMT_S16_LE) == 2);
    assert(sq5_aa_upmix_device_sample_bytes(SQ5_AA_UPMIX_FMT_S32_LE) == 4);
    assert(sq5_aa_upmix_device_sample_bytes(SQ5_AA_UPMIX_FMT_S16_LE |
                                            SQ5_AA_UPMIX_FMT_S32_LE) == 0);
    assert(sq5_aa_upmix_device_sample_bytes(1u << 8) == 0);
    assert(sq5_aa_upmix_client_bytes(0x4800, 4) == 9216);
    assert(sq5_aa_upmix_client_bytes(0x2D000, 4) == 92160);
    assert(sq5_aa_upmix_client_bytes(9216, 2) == 9216);
    assert(sq5_aa_upmix_client_bytes(9216, 0) == 0);
    patched.max_fragment_bytes = sq5_aa_upmix_client_bytes(0x4800, 4);
    assert(sq5_aa_upmix_select_source_fragment(3072, &patched) == 3072);
    assert(sq5_aa_upmix_plan(&source, &patched, &target));
    assert(target.fragment_bytes == 9216);
    patched.max_fragment_bytes = 9216;

    source.rate = 96000; assert(!sq5_aa_upmix_plan(&source, &patched, &target));
    source.rate = 48000; source.channels = 1;
    assert(!sq5_aa_upmix_plan(&source, &patched, &target));
    source.channels = 2; source.fragment_bytes = 3073;
    assert(!sq5_aa_upmix_plan(&source, &patched, &target));
    source.fragment_bytes = (SQ5_AA_UPMIX_MAX_FRAMES + 1u) * 4u;
    patched.max_fragment_bytes = source.fragment_bytes * 3u;
    assert(!sq5_aa_upmix_plan(&source, &patched, &target));

    puts("android_auto_upmix_policy_test: PASS");
    return 0;
}
