#include <ideath/FMSynth.h>
#include <algorithm>

namespace ideath {

void FMSynth::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    for (auto& op : ops_)
        op.prepare(sampleRate);
    reset();
}

void FMSynth::reset()
{
    for (auto& op : ops_)
        op.reset();
}

void FMSynth::noteOn(float freqHz, float velocity)
{
    baseFreq_ = freqHz;
    velocity_ = velocity;
    updatePhaseIncrements();
    for (auto& op : ops_)
    {
        op.phase = 0.0f;
        op.prevOut = 0.0f;
        op.env.noteOn();
    }
}

void FMSynth::noteOff()
{
    for (auto& op : ops_)
        op.env.noteOff();
}

bool FMSynth::isActive() const
{
    // Active if any carrier is active.
    // For simplicity, check all operators — a carrier with active envelope
    // means we should keep producing output.
    for (const auto& op : ops_)
    {
        if (op.env.isActive())
            return true;
    }
    return false;
}

void FMSynth::setAlgorithm(int algo)
{
    algorithm_ = std::max(0, std::min(algo, kNumAlgorithms - 1));
}

void FMSynth::setRatio(int op, float ratio)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].ratio = std::max(0.5f, std::min(ratio, 16.0f));
    updatePhaseIncrements();
}

void FMSynth::setLevel(int op, float level)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].level = std::max(0.0f, std::min(level, 1.0f));
}

void FMSynth::setFeedback(int op, float amount)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].feedback = std::max(0.0f, std::min(amount, 1.0f));
}

void FMSynth::setAttack(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setAttack(seconds);
}

void FMSynth::setDecay(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setDecay(seconds);
}

void FMSynth::setSustain(int op, float level)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setSustain(level);
}

void FMSynth::setRelease(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setRelease(seconds);
}

void FMSynth::updatePhaseIncrements()
{
    for (auto& op : ops_)
        op.phaseInc = baseFreq_ * op.ratio / sampleRate_;
}

float FMSynth::process()
{
    if (!isActive())
        return 0.0f;

    // Tick all operators. Aliases for readability.
    auto& op1 = ops_[0];
    auto& op2 = ops_[1];
    auto& op3 = ops_[2];
    auto& op4 = ops_[3];

    float out = 0.0f;
    int carrierCount = 1;

    // Algorithms (YM2612-inspired, matching inboil):
    //   OP indices: op1=carrier/out, op2..op4=modulators depending on algo
    switch (algorithm_)
    {
        case 0:
        {
            // OP4→OP3→OP2→OP1→out (serial)
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(o3);
            out = op1.tick(o2);
            carrierCount = 1;
            break;
        }
        case 1:
        {
            // OP4→OP3→OP2→out, OP1→out
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(o3);
            float o1 = op1.tick(0.0f);
            out = o2 + o1;
            carrierCount = 2;
            break;
        }
        case 2:
        {
            // OP4→OP3→out, OP4→OP2→OP1→out
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(o4);
            float o1 = op1.tick(o2);
            out = o3 + o1;
            carrierCount = 2;
            break;
        }
        case 3:
        {
            // OP4→OP3→out, OP2→OP1→out
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(0.0f);
            float o1 = op1.tick(o2);
            out = o3 + o1;
            carrierCount = 2;
            break;
        }
        case 4:
        {
            // OP3→OP2→out, OP4→OP1→out
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(0.0f);
            float o2 = op2.tick(o3);
            float o1 = op1.tick(o4);
            out = o2 + o1;
            carrierCount = 2;
            break;
        }
        case 5:
        {
            // OP4→OP3→out, OP2→out, OP1→out
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(0.0f);
            float o1 = op1.tick(0.0f);
            out = o3 + o2 + o1;
            carrierCount = 3;
            break;
        }
        case 6:
        {
            // OP4→OP3→out, OP2→out, OP1→out (OP1 with feedback emphasis)
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(o4);
            float o2 = op2.tick(0.0f);
            float o1 = op1.tick(0.0f);
            out = o3 + o2 + o1;
            carrierCount = 3;
            break;
        }
        case 7:
        {
            // OP4→out, OP3→out, OP2→out, OP1→out (full additive)
            float o4 = op4.tick(0.0f);
            float o3 = op3.tick(0.0f);
            float o2 = op2.tick(0.0f);
            float o1 = op1.tick(0.0f);
            out = o4 + o3 + o2 + o1;
            carrierCount = 4;
            break;
        }
        default:
            break;
    }

    out = out / static_cast<float>(carrierCount) * velocity_ * 0.85f;

    // Hard limit
    if (out > 1.0f) out = 1.0f;
    else if (out < -1.0f) out = -1.0f;

    return out;
}

} // namespace ideath
