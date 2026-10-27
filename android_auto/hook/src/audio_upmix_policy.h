/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure geometry policy for the Android Auto GAL stereo -> PCM6 bridge.
 * Kept independent of QNX headers so the fail-closed decisions are host-tested. */
#ifndef SQ5_AA_AUDIO_UPMIX_POLICY_H
#define SQ5_AA_AUDIO_UPMIX_POLICY_H

#include <stddef.h>

enum {
    SQ5_AA_UPMIX_RATE = 48000,
    SQ5_AA_UPMIX_SOURCE_CHANNELS = 2,
    SQ5_AA_UPMIX_TARGET_CHANNELS = 6,
    SQ5_AA_UPMIX_SAMPLE_BYTES = 2,
    SQ5_AA_UPMIX_MAX_FRAMES = 1024
};

struct sq5_aa_pcm_geometry {
    unsigned rate;
    unsigned channels;
    size_t fragment_bytes;
};

struct sq5_aa_pcm_caps {
    unsigned max_channels;
    size_t max_fragment_bytes;
};

static int sq5_aa_upmix_plan(const struct sq5_aa_pcm_geometry *source,
                             const struct sq5_aa_pcm_caps *caps,
                             struct sq5_aa_pcm_geometry *target)
{
    size_t source_frame_bytes, frames, target_bytes;

    if (!source || !caps || !target || source->rate != SQ5_AA_UPMIX_RATE ||
        source->channels != SQ5_AA_UPMIX_SOURCE_CHANNELS || !source->fragment_bytes)
        return 0;
    source_frame_bytes = source->channels * SQ5_AA_UPMIX_SAMPLE_BYTES;
    if (source->fragment_bytes % source_frame_bytes) return 0;
    frames = source->fragment_bytes / source_frame_bytes;
    if (!frames || frames > SQ5_AA_UPMIX_MAX_FRAMES) return 0;
    target_bytes = frames * SQ5_AA_UPMIX_TARGET_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES;
    if (caps->max_channels < SQ5_AA_UPMIX_TARGET_CHANNELS ||
        caps->max_fragment_bytes < target_bytes)
        return 0;
    target->rate = source->rate;
    target->channels = SQ5_AA_UPMIX_TARGET_CHANNELS;
    target->fragment_bytes = target_bytes;
    return 1;
}

static size_t sq5_aa_upmix_source_bytes(size_t target_bytes)
{
    size_t target_frame = SQ5_AA_UPMIX_TARGET_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES;
    size_t source_frame = SQ5_AA_UPMIX_SOURCE_CHANNELS * SQ5_AA_UPMIX_SAMPLE_BYTES;

    return target_bytes / target_frame * source_frame;
}

#endif
