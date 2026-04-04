#include <ideath/Oscillator.h>

int main()
{
    ideath::Oscillator osc;
    osc.prepare(44100.0f);
    osc.setFrequency(440.0f);
    return osc.process(1.0f) != 0.0f ? 0 : 0;
}
