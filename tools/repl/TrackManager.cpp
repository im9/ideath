#include "TrackManager.h"
#include <algorithm>

namespace ideath { namespace repl {

void TrackManager::prepare(float sampleRate)
{
    for (auto& e : engines_)
        e.prepare(sampleRate);

    limiter_.prepare(sampleRate);
    limiter_.setThreshold(0.0f);   // 0 dB brickwall
    limiter_.setLookahead(0.005f); // 5ms lookahead (also the attack ramp)
    limiter_.setRelease(0.05f);    // 50ms release
}

void TrackManager::applyPendingState()
{
    for (int i = 0; i < kMaxTracks; ++i)
        engines_[i].applyPendingState(shared_[i]);
}

std::pair<float, float> TrackManager::process()
{
    // Check if any track has solo
    bool anySolo = false;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (mix_[i].solo.load(std::memory_order_relaxed))
        {
            anySolo = true;
            break;
        }
    }

    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (mix_[i].muted.load(std::memory_order_relaxed))
            continue;
        if (anySolo && !mix_[i].solo.load(std::memory_order_relaxed))
            continue;

        auto [l, r] = engines_[i].process();
        const float vol = mix_[i].volume.load(std::memory_order_relaxed);
        sumL += l * vol;
        sumR += r * vol;
    }

    float outL, outR;
    if (limiterEnabled.load(std::memory_order_relaxed))
    {
        // Linked stereo: one gain from the louder side on both channels,
        // so the image holds when only one side limits.
        const auto [l, r] = limiter_.processStereo(sumL, sumR);
        outL = l;
        outR = r;
    }
    else
    {
        // Fallback hard clamp when limiter is off
        outL = std::clamp(sumL, -1.0f, 1.0f);
        outR = std::clamp(sumR, -1.0f, 1.0f);
    }

    scope_.push(0.5f * (outL + outR));
    return { outL, outR };
}

}} // namespace ideath::repl
