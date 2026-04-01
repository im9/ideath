#include "TrackManager.h"

namespace ideath { namespace repl {

void TrackManager::prepare(float sampleRate)
{
    for (auto& e : engines_)
        e.prepare(sampleRate);

    limiter_.prepare(sampleRate);
    limiter_.setThreshold(0.0f);   // 0 dB brickwall
    limiter_.setLookahead(0.005f); // 5ms lookahead
    limiter_.setRelease(0.05f);    // 50ms release
}

void TrackManager::applyPendingState()
{
    for (int i = 0; i < kMaxTracks; ++i)
        engines_[i].applyPendingState(shared_[i]);
}

float TrackManager::process()
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

    float sum = 0.0f;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (mix_[i].muted.load(std::memory_order_relaxed))
            continue;
        if (anySolo && !mix_[i].solo.load(std::memory_order_relaxed))
            continue;

        float s = engines_[i].process();
        s *= mix_[i].volume.load(std::memory_order_relaxed);
        sum += s;
    }

    if (limiterEnabled.load(std::memory_order_relaxed))
        return limiter_.process(sum);

    // Fallback hard clamp when limiter is off
    if (sum > 1.0f) sum = 1.0f;
    else if (sum < -1.0f) sum = -1.0f;
    return sum;
}

}} // namespace ideath::repl
