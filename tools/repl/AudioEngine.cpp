#include "AudioEngine.h"
#include <cmath>

namespace ideath { namespace repl {

void AudioEngine::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    osc_.prepare(sampleRate);
    wt_.prepare(sampleRate);
    env_.prepare(sampleRate);
    filter_.prepare(sampleRate);
    crush_.prepare(sampleRate);
    delay_.prepare(sampleRate, 2.0f); // max 2 seconds delay
    tapeDelay_.prepare(sampleRate, 2.0f);
    comb_.prepare(sampleRate, 0.1f);
    lfo_.prepare(sampleRate);
    fg_.prepare(sampleRate);
    porta_.prepare(sampleRate);
    fm_.prepare(sampleRate);
    comp_.prepare(sampleRate);
    reverb_.prepare(sampleRate);
    hallReverb_.prepare(sampleRate);
    shimmerReverb_.prepare(sampleRate);
    wavefolder_.prepare(sampleRate);
    unison_.prepare(sampleRate);
    looper_.prepare(sampleRate, 30.0f); // max 30 seconds loop
    karplus_.prepare(sampleRate);
    modal_.prepare(sampleRate);
    harmonic_.prepare(sampleRate);
    // 2-second ring buffer matches DelayLine / TapeDelay headroom; gives
    // up to 2 s of recall material at positionScatter=1.0.
    granular_.prepare(sampleRate, static_cast<int>(2.0f * sampleRate));
    pitchEnv_.prepare(sampleRate);
    gainSmoother_.prepare(sampleRate);
    gainSmoother_.setTime(0.005f); // 5ms fade
    gainSmoother_.setValue(0.0f);
    gainSmoother_.setTarget(0.0f);

    seq_ = {};
    seqStep_ = 0;
    seqSampleCounter_ = 0;
    seqSamplesPerStep_ = 0;
    seqGateSamples_ = 0;
    seqGateOpen_ = false;
}

void AudioEngine::applyPendingState(SharedState& shared)
{
    if (shared.paramsReady.load(std::memory_order_acquire))
    {
        VoiceParams newParams = shared.staging;
        shared.paramsReady.store(false, std::memory_order_release);

        // Wavetable: only rebuild when shape actually changes
        if (newParams.source == SourceType::Wavetable &&
            (newParams.wtShape != params_.wtShape || params_.source != SourceType::Wavetable))
        {
            switch (newParams.wtShape)
            {
                case WtShape::Square:   wt_ = Wavetable::squareTable(); break;
                case WtShape::Saw:      wt_ = Wavetable::sawTable(); break;
                case WtShape::Triangle: wt_ = Wavetable::triangleTable(); break;
                case WtShape::Sine:     wt_ = Wavetable::sineTable(); break;
            }
            wt_.prepare(sampleRate_);
        }

        // Update gain target for smooth transitions
        if (newParams.source != SourceType::None)
            gainSmoother_.setTarget(1.0f);
        else
            gainSmoother_.setTarget(0.0f);

        // Looper actions
        switch (newParams.loopAction)
        {
            case VoiceParams::LoopAction::Record:  looper_.record(); break;
            case VoiceParams::LoopAction::Stop:    looper_.stop(); break;
            case VoiceParams::LoopAction::Play:    looper_.play(); break;
            case VoiceParams::LoopAction::Overdub: looper_.overdub(); break;
            case VoiceParams::LoopAction::Off:     looper_.reset(); break;
            case VoiceParams::LoopAction::None:    break;
        }
        newParams.loopAction = VoiceParams::LoopAction::None;

        params_ = newParams;

        // Function generator: sync rise/fall/curve every block (setters are
        // modulation-safe).  setCycle drives the run-switch — setCycle(true)
        // from Idle auto-starts the cycle; setCycle(false) lets the current
        // segment complete before going idle.
        fg_.setRise(params_.fgRise);
        fg_.setFall(params_.fgFall);
        fg_.setCurve(params_.fgCurve);
        fg_.setCycle(params_.fgTarget != FgTarget::Off && params_.fgCycle);

        // Modal resonator: count / decay / inharmonicity are non-modulated
        // and live-tweakable, so they're applied at block boundary.
        // Fundamental tracks `freq` per-sample (in process()) because it's
        // a modulatable pitch parameter.  setPartialDecay short-circuits when
        // the value hasn't moved, so calling it for every partial each block
        // is essentially free after the first call.
        modal_.setPartialCount(params_.modalPartials);
        modal_.setInharmonicity(params_.modalInharmonicity);
        for (int i = 0; i < ideath::ModalResonator::kMaxPartials; ++i)
            modal_.setPartialDecay(i, params_.modalDecay);

        // HarmonicOscillator: bands + partial count are block-rate.  The
        // fundamental is pitch-modulated and so pushed per-sample below.
        // setBands writes all 32 partial amplitudes; setPartialCount caps
        // how many are summed in process() (CPU knob).
        harmonic_.setBands(params_.harmonicLow,
                           params_.harmonicMid,
                           params_.harmonicHigh,
                           params_.harmonicShape);
        harmonic_.setPartialCount(params_.harmonicPartials);
    }

    if (shared.stopRequested.load(std::memory_order_acquire))
    {
        stopped_ = true;
        env_.noteOff();
        gainSmoother_.setTarget(0.0f);
        shared.stopRequested.store(false, std::memory_order_release);
    }

    // Check for note events
    int noteOn = shared.noteOnCounter.load(std::memory_order_acquire);
    if (noteOn != lastNoteOn_)
    {
        lastNoteOn_ = noteOn;
        stopped_ = false;
        baseFreq_ = params_.frequency;
        porta_.setTarget(baseFreq_);
        // Set gain immediately — envelope handles amplitude shaping.
        // Ramping gain per-note caused clicks from 0→1 transients.
        gainSmoother_.setValue(1.0f);

        if (params_.envelopeEnabled)
        {
            env_.setAttack(params_.attack);
            env_.setDecay(params_.decay);
            env_.setSustain(params_.sustain);
            env_.setRelease(params_.release);
            env_.noteOn();
        }

        // Pitch envelope
        if (params_.pitchEnvEnabled)
        {
            pitchEnv_.setDecay(params_.pitchEnvDecay);
            pitchEnv_.trigger(1.0f);
        }

        // Function generator: one-shot mode fires on note-on.  Cycle mode
        // is free-running (started in applyPendingState), so ignore noteOn.
        if (params_.fgTarget != FgTarget::Off && !params_.fgCycle)
            fg_.trigger();

        // FM synth note on
        if (params_.source == SourceType::FM)
        {
            fm_.setAlgorithm(params_.fmAlgorithm);
            for (int i = 0; i < 4; ++i)
            {
                fm_.setRatio(i, params_.fmRatios[i]);
                fm_.setLevel(i, params_.fmLevels[i]);
                fm_.setFeedback(i, params_.fmFeedback[i]);
            }
            fm_.noteOn(baseFreq_);
        }

        // Karplus-Strong: re-pluck on note-on. Pitch/decay/damping/exciter
        // are applied per-sample inside process() (setters are modulation-
        // safe and cheap).
        if (params_.source == SourceType::KarplusStrong)
            karplus_.pluck();

        // Modal resonator: strike on every note-on so each note rings the
        // bell.  Per-note params are pushed into the modal source from the
        // audio loop below.
        if (params_.source == SourceType::Modal)
            modal_.strike(1.0f);
    }

    int noteOff = shared.noteOffCounter.load(std::memory_order_acquire);
    if (noteOff != lastNoteOff_)
    {
        lastNoteOff_ = noteOff;
        env_.noteOff();
        fm_.noteOff();
    }

    // --- Sequencer ---
    if (shared.seqReady.load(std::memory_order_acquire))
    {
        seq_ = shared.seqStaging;
        shared.seqReady.store(false, std::memory_order_release);

        if (seq_.running && seq_.numSteps > 0)
        {
            seqStep_ = 0;
            seqSampleCounter_ = 0;
            seqSamplesPerStep_ = static_cast<int>(sampleRate_ * 60.0f / seq_.bpm);
            seqGateSamples_ = seqSamplesPerStep_ * static_cast<int>(seq_.gatePercent) / 100;
            seqGateOpen_ = false;

            // Trigger first step immediately
            float freq = seq_.frequencies[0];
            if (freq > 0.0f)
            {
                params_.frequency = freq;
                stopped_ = false;
                baseFreq_ = freq;
                porta_.setTarget(freq);
                seqVelocity_ = seq_.velocities[0] > 0.0f ? seq_.velocities[0] : 1.0f;
                gainSmoother_.setValue(seqVelocity_);

                if (params_.envelopeEnabled)
                {
                    env_.setAttack(params_.attack);
                    env_.setDecay(params_.decay);
                    env_.setSustain(params_.sustain);
                    env_.setRelease(params_.release);
                    env_.noteOn();
                }
                if (params_.pitchEnvEnabled)
                {
                    pitchEnv_.setDecay(params_.pitchEnvDecay);
                    pitchEnv_.trigger(1.0f);
                }
                if (params_.fgTarget != FgTarget::Off && !params_.fgCycle)
                    fg_.trigger();
                if (params_.source == SourceType::FM)
                    fm_.noteOn(freq);
                if (params_.source == SourceType::KarplusStrong)
                    karplus_.pluck();
                if (params_.source == SourceType::Modal)
                    modal_.strike(seqVelocity_);
                seqGateOpen_ = true;
            }
        }
        else
        {
            seq_.running = false;
            if (seqGateOpen_)
            {
                env_.noteOff();
                seqGateOpen_ = false;
            }
        }
    }
}

void AudioEngine::advanceSequencer()
{
    if (!seq_.running || seq_.numSteps <= 0) return;

    // Gate off at gate percent of step
    if (seqGateOpen_ && seqSampleCounter_ >= seqGateSamples_)
    {
        env_.noteOff();
        fm_.noteOff();
        // When envelope is on, the ADSR release handles the fade —
        // don't ramp gain to avoid direction-change clicks.
        // When envelope is off, the gain smoother is the only amplitude
        // control, so it must ramp down on gate-off.
        if (!params_.envelopeEnabled)
            gainSmoother_.setTarget(0.0f);
        seqGateOpen_ = false;
    }

    ++seqSampleCounter_;
    if (seqSampleCounter_ < seqSamplesPerStep_) return;

    // Advance to next step
    seqSampleCounter_ = 0;
    seqStep_ = (seqStep_ + 1) % seq_.numSteps;

    float freq = seq_.frequencies[seqStep_];
    if (freq > 0.0f)
    {
        params_.frequency = freq;
        stopped_ = false;
        baseFreq_ = freq;
        porta_.setTarget(freq);
        seqVelocity_ = seq_.velocities[seqStep_] > 0.0f ? seq_.velocities[seqStep_] : 1.0f;
        // With envelope: set gain immediately (ADSR retrigger fade handles
        // the transition). Without envelope: ramp to avoid gain-step clicks.
        if (params_.envelopeEnabled)
            gainSmoother_.setValue(seqVelocity_);
        else
            gainSmoother_.setTarget(seqVelocity_);

        if (params_.envelopeEnabled)
        {
            env_.setAttack(params_.attack);
            env_.setDecay(params_.decay);
            env_.setSustain(params_.sustain);
            env_.setRelease(params_.release);
            env_.noteOn();
        }
        if (params_.pitchEnvEnabled)
        {
            pitchEnv_.setDecay(params_.pitchEnvDecay);
            pitchEnv_.trigger(1.0f);
        }
        if (params_.fgTarget != FgTarget::Off && !params_.fgCycle)
            fg_.trigger();
        if (params_.source == SourceType::FM)
            fm_.noteOn(freq);
        if (params_.source == SourceType::KarplusStrong)
            karplus_.pluck();
        if (params_.source == SourceType::Modal)
            modal_.strike(seqVelocity_);
        seqGateOpen_ = true;
    }
    else
    {
        // Rest: let envelope release naturally
        env_.noteOff();
        fm_.noteOff();
        if (!params_.envelopeEnabled)
            gainSmoother_.setTarget(0.0f);
        seqGateOpen_ = false;
    }
}

float AudioEngine::process()
{
    advanceSequencer();

    float gain = gainSmoother_.process();

    if (gain < 0.0001f && params_.source == SourceType::None)
    {
        // Fully faded out, safe to reset
    if (!delayCleared_)
    {
        delay_.reset();
        tapeDelay_.reset();
        comb_.reset();
        lfo_.reset();
        fg_.reset();
        granular_.reset();
        delayCleared_ = true;
        }
        return 0.0f;
    }
    delayCleared_ = false;

    // --- Portamento ---
    // Minimum 3ms glide for sequencer retriggers to avoid clicks
    // from abrupt frequency changes through resonant filters.
    float portaTime = params_.portaTime;
    if (seq_.running && portaTime < 0.003f)
        portaTime = 0.003f;
    porta_.setTime(portaTime);
    porta_.setTarget(params_.frequency);
    float freq = porta_.process();

    // --- Pitch envelope (kick/perc sweep) ---
    if (params_.pitchEnvEnabled)
    {
        float pitchMod = pitchEnv_.process() * params_.pitchEnvAmount;
        freq *= std::pow(2.0f, pitchMod / 12.0f);
    }

    // --- LFO modulation ---
    float lfoVal = 0.0f;
    if (params_.lfoTarget != LfoTarget::Off)
    {
        lfo_.setRate(params_.lfoRate);

        switch (params_.lfoWaveform)
        {
            case LfoWaveform::Sine:          lfo_.setWaveform(LFO::Waveform::Sine); break;
            case LfoWaveform::Triangle:      lfo_.setWaveform(LFO::Waveform::Triangle); break;
            case LfoWaveform::Square:        lfo_.setWaveform(LFO::Waveform::Square); break;
            case LfoWaveform::Saw:           lfo_.setWaveform(LFO::Waveform::Saw); break;
            case LfoWaveform::SampleAndHold: lfo_.setWaveform(LFO::Waveform::SampleAndHold); break;
        }

        lfo_.setPolarity(LFO::Polarity::Bipolar);
        lfoVal = lfo_.process();

        if (params_.lfoTarget == LfoTarget::Pitch)
            freq *= std::pow(2.0f, lfoVal * params_.lfoDepth / 1200.0f); // depth in cents
    }

    // --- Function generator modulation (West Coast Contour) ---
    // FG output is unipolar [0, 1]; depth in cents for pitch/filter targets.
    float fgVal = 0.0f;
    if (params_.fgTarget != FgTarget::Off)
    {
        fgVal = fg_.process();
        if (params_.fgTarget == FgTarget::Pitch)
            freq *= std::pow(2.0f, fgVal * params_.fgDepth / 1200.0f);
    }

    // --- Source ---
    float sample = 0.0f;

    switch (params_.source)
    {
        case SourceType::Oscillator:
            osc_.setFrequency(freq);
            sample = osc_.process(params_.oscWaveform == OscWaveform::Saw ? 1.0f : 0.0f);
            break;

        case SourceType::Wavetable:
            wt_.setFrequency(freq);
            sample = wt_.process();
            break;

        case SourceType::Noise:
            sample = noise_.process();
            break;

        case SourceType::FM:
            sample = fm_.process();
            break;

        case SourceType::Unison:
            unison_.setFrequency(freq);
            unison_.setVoiceCount(params_.unisonVoices);
            unison_.setDetune(params_.unisonDetune);
            sample = unison_.process(params_.oscWaveform == OscWaveform::Saw ? 1.0f : 0.0f);
            break;

        case SourceType::KarplusStrong:
            // Setters are modulation-safe and cheap; sync them every sample
            // so LFO / FG / pitch-env modulations on `freq` track in tune.
            karplus_.setFrequency(freq);
            karplus_.setDecay(params_.ksDecay);
            karplus_.setDamping(params_.ksDamping);
            karplus_.setExciter(params_.ksExciter);
            sample = karplus_.process();
            break;

        case SourceType::Modal:
            // setFundamental recomputes every partial's BP coefficient, so
            // it's the one parameter that's expensive per sample.  We still
            // push it each sample because pitch can be modulated (LFO / FG /
            // pitch-env / portamento) — the cost is amortised across whatever
            // the bell sequence demands.  Count / decay / inharmonicity are
            // applied at block boundary (applyPendingState) instead.
            modal_.setFundamental(freq);
            sample = modal_.process();
            break;

        case SourceType::Harmonic:
            // setFrequency short-circuits when the value hasn't moved, so
            // per-sample calls under modulation are cheap.  Bands / count
            // are block-rate (applyPendingState).
            harmonic_.setFrequency(freq);
            sample = harmonic_.process();
            break;

        case SourceType::None:
            break;
    }

    // --- Filter (SVFilter — modulation-safe) ---
    // Filter runs before the envelope (standard subtractive: VCO → VCF → VCA).
    // This ensures the ADSR retrigger fade masks any filter state transients.
    if (params_.filterType != FilterType::Off)
    {
        float filterFreq = params_.filterFreq;

        // LFO → filter modulation
        if (params_.lfoTarget == LfoTarget::Filter)
            filterFreq *= std::pow(2.0f, lfoVal * params_.lfoDepth / 1200.0f);

        // Function generator → filter modulation
        if (params_.fgTarget == FgTarget::Filter)
            filterFreq *= std::pow(2.0f, fgVal * params_.fgDepth / 1200.0f);

        filter_.setCutoff(filterFreq);
        // Map Biquad Q range to SVFilter resonance (0–0.9).
        // Q=0.707 → res=0, Q=20 → res≈0.86. Capped to avoid ringing.
        float res = (1.0f - (0.707f / std::max(params_.filterQ, 0.707f))) * 0.9f;
        filter_.setResonance(res);

        switch (params_.filterType)
        {
            case FilterType::Lowpass:  filter_.setMode(SVFilter::Mode::Lowpass); break;
            case FilterType::Highpass: filter_.setMode(SVFilter::Mode::Highpass); break;
            case FilterType::Bandpass: filter_.setMode(SVFilter::Mode::Bandpass); break;
            default: break;
        }

        sample = filter_.process(sample);
    }

    // --- Envelope (VCA — after filter so retrigger fade masks filter transients) ---
    if (params_.envelopeEnabled)
    {
        float envVal = env_.process();
        sample *= envVal;
    }

    // --- Compressor ---
    if (params_.compEnabled)
    {
        comp_.setThreshold(params_.compThreshold);
        comp_.setRatio(params_.compRatio);
        comp_.setAttack(params_.compAttack);
        comp_.setRelease(params_.compRelease);
        comp_.setMakeup(params_.compMakeup);
        sample = comp_.process(sample);
    }

    // --- BitCrusher ---
    if (params_.crushEnabled)
    {
        crush_.setBitDepth(params_.crushBits);
        crush_.setDownsampleRate(params_.crushRate);
        sample = crush_.process(sample);
    }

    // --- Saturation ---
    if (params_.satEnabled)
        sample = Saturation::tanhDrive(sample, params_.satDrive);

    // --- Wavefolder ---
    if (params_.foldEnabled)
    {
        wavefolder_.setDrive(params_.foldDrive);
        wavefolder_.setMix(params_.foldMix);
        sample = wavefolder_.process(sample);
    }

    // --- Delay ---
    if (params_.delayEnabled)
    {
        delay_.setDelay(params_.delayTime);
        delay_.setFeedback(params_.delayFeedback);
        delay_.setMix(0.5f);
        sample = delay_.process(sample);
    }

    // --- TapeDelay ---
    if (params_.tapeDelayEnabled)
    {
        tapeDelay_.setDelay(params_.tapeDelayTime);
        tapeDelay_.setFeedback(params_.tapeDelayFeedback);
        tapeDelay_.setMix(0.5f);
        tapeDelay_.setWowDepth(params_.tapeDelayWowDepth);
        tapeDelay_.setWowRate(params_.tapeDelayWowRate);
        tapeDelay_.setFlutterDepth(params_.tapeDelayFlutterDepth);
        tapeDelay_.setFlutterRate(params_.tapeDelayFlutterRate);
        tapeDelay_.setLowpass(params_.tapeDelayLowpass);
        tapeDelay_.setHighpass(params_.tapeDelayHighpass);
        tapeDelay_.setDrive(params_.tapeDelayDrive);
        sample = tapeDelay_.process(sample);
    }

    // --- CombFilter ---
    if (params_.combEnabled)
    {
        comb_.setDelay(params_.combDelay);
        comb_.setFeedback(params_.combFeedback);
        comb_.setDamp(params_.combDamp);
        comb_.setMix(params_.combMix);
        sample = comb_.process(sample);
    }

    // --- Looper (FeedbackBuffer) ---
    {
        looper_.setFeedback(params_.loopFeedback);
        looper_.setMix(params_.loopMix);
        sample = looper_.process(sample);
    }

    // --- Granular (ring-buffer grain cloud) ---
    if (params_.granularEnabled)
    {
        granular_.setGrainRate(params_.granularRate);
        granular_.setGrainSize(params_.granularSize);
        granular_.setPitchSpread(params_.granularPitchSpread);
        granular_.setPositionScatter(params_.granularScatter);
        granular_.setFreeze(params_.granularFreeze);
        granular_.writeSample(sample);
        const float wet = granular_.process();
        sample = sample * (1.0f - params_.granularMix) + wet * params_.granularMix;
    }

    // --- Reverb (mono sum of stereo output) ---
    if (params_.reverbType != ReverbType::Off)
    {
        switch (params_.reverbType)
        {
            case ReverbType::Room:
            {
                reverb_.setSize(params_.reverbSize);
                reverb_.setDamp(params_.reverbDamp);
                reverb_.setMix(params_.reverbMix);
                reverb_.setFreeze(params_.reverbFreeze);
                auto [l, r] = reverb_.process(sample);
                sample = (l + r) * 0.5f;
                break;
            }
            case ReverbType::Hall:
            {
                hallReverb_.setSize(params_.reverbSize);
                hallReverb_.setDamp(params_.reverbDamp);
                hallReverb_.setPreDelay(params_.reverbPreDelay);
                hallReverb_.setModDepth(params_.reverbModDepth);
                hallReverb_.setMix(params_.reverbMix);
                hallReverb_.setFreeze(params_.reverbFreeze);
                auto [l, r] = hallReverb_.process(sample);
                sample = (l + r) * 0.5f;
                break;
            }
            case ReverbType::Shimmer:
            {
                shimmerReverb_.setSize(params_.reverbSize);
                shimmerReverb_.setDamp(params_.reverbDamp);
                shimmerReverb_.setShimmer(params_.reverbShimmer);
                shimmerReverb_.setMix(params_.reverbMix);
                shimmerReverb_.setFreeze(params_.reverbFreeze);
                auto [l, r] = shimmerReverb_.process(sample);
                sample = (l + r) * 0.5f;
                break;
            }
            default: break;
        }
    }

    // --- LFO → Volume ---
    if (params_.lfoTarget == LfoTarget::Volume)
        sample *= 1.0f + lfoVal * params_.lfoDepth * 0.01f;

    // --- Function generator → Volume ---
    if (params_.fgTarget == FgTarget::Volume)
        sample *= 1.0f + fgVal * params_.fgDepth * 0.01f;

    // --- Master volume with gain smoothing ---
    sample *= params_.volume * gain;

    // --- Soft clip: avoid hard discontinuity from resonant filter peaks ---
    // PeakLimiter on master handles final brickwall.
    sample = Saturation::tanhDrive(sample, 1.0f);

    return sample;
}

}} // namespace ideath::repl
