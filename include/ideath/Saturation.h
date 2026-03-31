#pragma once

namespace ideath {

/// Soft-clipping saturation functions.
namespace Saturation {

/// tanh-based soft clip. drive: amount of gain before tanh (>= 1.0).
float tanhDrive(float input, float drive);

/// Simpler polynomial soft clip (cheaper than tanh).
float softClip(float input);

} // namespace Saturation

} // namespace ideath
