#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <vector>

namespace ideath { namespace repl {

/// Lock-free ring buffer for capturing audio samples for scope display.
/// Audio thread writes via push(), command thread reads via snapshot().
class ScopeBuffer
{
public:
    static constexpr size_t kCapacity = 2048;

    /// Push one sample (audio thread).
    void push(float sample)
    {
        if (!enabled_.load(std::memory_order_relaxed))
            return;
        buf_[writePos_.load(std::memory_order_relaxed) % kCapacity] = sample;
        writePos_.fetch_add(1, std::memory_order_release);
    }

    /// Enable/disable capture.
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }

    /// Copy the most recent `count` samples into `out`. Returns actual count copied.
    size_t snapshot(float* out, size_t count) const
    {
        size_t wp = writePos_.load(std::memory_order_acquire);
        if (wp == 0) return 0;
        size_t available = std::min(count, std::min(wp, kCapacity));
        size_t start = wp - available;
        for (size_t i = 0; i < available; ++i)
            out[i] = buf_[(start + i) % kCapacity];
        return available;
    }

    /// Render oscilloscope using Unicode Braille characters.
    /// Each braille char is a 2x4 dot grid, giving effective resolution
    /// of (width*2) x (height*4) pixels.
    /// `width` = character columns, `height` = character rows.
    /// `limiterGrDb` = optional limiter gain reduction in dB (pass 0 to hide).
    static std::string render(const float* samples, size_t numSamples,
                              int width = 72, int height = 12,
                              float limiterGrDb = 0.0f)
    {
        if (numSamples == 0 || width < 2 || height < 2)
            return "(no data)\n";

        // Effective pixel dimensions
        int pxW = width * 2;
        int pxH = height * 4;

        // Downsample to pxW columns
        std::vector<float> cols(pxW);
        for (int x = 0; x < pxW; ++x)
        {
            size_t s0 = static_cast<size_t>(x) * numSamples / pxW;
            size_t s1 = static_cast<size_t>(x + 1) * numSamples / pxW;
            if (s1 <= s0) s1 = s0 + 1;
            if (s1 > numSamples) s1 = numSamples;

            float val = 0.0f;
            for (size_t s = s0; s < s1; ++s)
            {
                if (std::fabs(samples[s]) > std::fabs(val))
                    val = samples[s];
            }
            cols[x] = val;
        }

        // Auto-scale to peak
        float peak = 0.0f;
        for (int x = 0; x < pxW; ++x)
            peak = std::max(peak, std::fabs(cols[x]));
        if (peak < 1e-6f) peak = 1.0f;

        // Map samples to pixel Y coordinates
        std::vector<int> py(pxW);
        for (int x = 0; x < pxW; ++x)
        {
            float normalized = cols[x] / peak;
            // Map [-1, 1] to [0, pxH-1], top = +1
            int y = static_cast<int>((1.0f - normalized) * 0.5f * (pxH - 1) + 0.5f);
            py[x] = std::max(0, std::min(pxH - 1, y));
        }

        // Braille dot positions within a character cell (2 cols x 4 rows):
        //   col0: row0=0x01, row1=0x02, row2=0x04, row3=0x40
        //   col1: row0=0x08, row1=0x10, row2=0x20, row3=0x80
        // Braille base codepoint: U+2800
        static const int dotBits[4][2] = {
            {0x01, 0x08},
            {0x02, 0x10},
            {0x04, 0x20},
            {0x40, 0x80}
        };

        int midPixelY = pxH / 2;

        std::ostringstream oss;

        // Top border
        oss << " +1 ";
        for (int x = 0; x < width; ++x) oss << "\u2584";
        oss << '\n';

        for (int row = 0; row < height; ++row)
        {
            // Row label
            if (row == height / 2)
                oss << "  0 ";
            else
                oss << "    ";

            int cellTop = row * 4; // top pixel Y of this cell

            for (int col = 0; col < width; ++col)
            {
                int bits = 0;

                // Draw center line (zero crossing) as faint dots
                for (int dy = 0; dy < 4; ++dy)
                {
                    int pixY = cellTop + dy;
                    if (pixY == midPixelY)
                    {
                        bits |= dotBits[dy][0];
                        bits |= dotBits[dy][1];
                    }
                }

                // Draw waveform — for each of the 2 sub-columns, check if
                // the waveform pixel falls within this cell's 4 rows.
                for (int dx = 0; dx < 2; ++dx)
                {
                    int px = col * 2 + dx;
                    int sampleY = py[px];

                    // Draw the point
                    if (sampleY >= cellTop && sampleY < cellTop + 4)
                    {
                        int dy = sampleY - cellTop;
                        bits |= dotBits[dy][dx];
                    }

                    // Connect vertically to previous column to avoid gaps
                    if (px > 0)
                    {
                        int prevY = py[px - 1];
                        int yMin = std::min(prevY, sampleY);
                        int yMax = std::max(prevY, sampleY);
                        for (int fy = yMin; fy <= yMax; ++fy)
                        {
                            if (fy >= cellTop && fy < cellTop + 4)
                            {
                                int dy = fy - cellTop;
                                bits |= dotBits[dy][dx];
                            }
                        }
                    }
                }

                // Encode as UTF-8 braille character (U+2800 + bits)
                int codepoint = 0x2800 + bits;
                oss << encodeUTF8(codepoint);
            }
            oss << '\n';
        }

        // Bottom border
        oss << " -1 ";
        for (int x = 0; x < width; ++x) oss << "\u2580";
        oss << '\n';

        // Compute max sample-to-sample delta (click detector)
        float maxDelta = 0.0f;
        for (size_t i = 1; i < numSamples; ++i)
        {
            float d = std::fabs(samples[i] - samples[i - 1]);
            if (d > maxDelta) maxDelta = d;
        }

        // Stats line
        oss << "     peak=" << peak << "  d=" << maxDelta;
        if (maxDelta > 0.1f)
            oss << " !!";
        if (limiterGrDb < -0.01f)
            oss << "  GR=" << limiterGrDb << "dB";
        oss << '\n';

        return oss.str();
    }

private:
    std::array<float, kCapacity> buf_{};
    std::atomic<size_t> writePos_{0};
    std::atomic<bool> enabled_{false};

    /// Encode a Unicode codepoint (U+2800..U+28FF) to UTF-8 string.
    static std::string encodeUTF8(int cp)
    {
        std::string s;
        if (cp < 0x80)
        {
            s += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return s;
    }
};

}} // namespace ideath::repl
