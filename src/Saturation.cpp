#include <ideath/Saturation.h>
#include <cmath>

namespace ideath {
namespace Saturation {

float tanhDrive(float input, float drive)
{
    return std::tanh(input * drive);
}

float softClip(float input)
{
    // Cubic soft clip: y = x - x^3/3  for |x| <= 1, else ±2/3.
    if (input > 1.0f)
        return 2.0f / 3.0f;
    if (input < -1.0f)
        return -2.0f / 3.0f;
    return input - (input * input * input) / 3.0f;
}

} // namespace Saturation
} // namespace ideath
