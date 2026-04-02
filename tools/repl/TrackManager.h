#pragma once

#include "AudioEngine.h"
#include "ScopeBuffer.h"
#include "SharedState.h"
#include <ideath/PeakLimiter.h>
#include <array>
#include <atomic>

namespace ideath { namespace repl {

static constexpr int kMaxTracks = 8;

struct TrackMixState {
    std::atomic<bool> muted{false};
    std::atomic<bool> solo{false};
    std::atomic<float> volume{1.0f};
};

class TrackManager
{
public:
    void prepare(float sampleRate);

    /// Apply pending state for all tracks (audio thread).
    void applyPendingState();

    /// Process one sample: mix all active tracks (audio thread).
    float process();

    SharedState& getShared(int trackIndex) { return shared_[trackIndex]; }
    TrackMixState& getMix(int trackIndex) { return mix_[trackIndex]; }
    PeakLimiter& getLimiter() { return limiter_; }
    ScopeBuffer& getScope() { return scope_; }

    std::atomic<bool> limiterEnabled{true};

private:
    std::array<AudioEngine, kMaxTracks> engines_;
    std::array<SharedState, kMaxTracks> shared_;
    std::array<TrackMixState, kMaxTracks> mix_;
    PeakLimiter limiter_;
    ScopeBuffer scope_;
};

}} // namespace ideath::repl
