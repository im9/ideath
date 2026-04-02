#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

namespace ideath { namespace repl {

/// Renders FFT spectrum using Unicode Braille characters.
/// Uses the same ScopeBuffer ring buffer — FFT is computed on the display thread.
class SpectrumRenderer
{
public:
    static constexpr size_t kFFTSize = 2048;

    /// Compute magnitude spectrum from time-domain samples and render.
    /// `samples` should be at least kFFTSize floats.
    /// `sampleRate` for frequency axis labels.
    /// `width` = character columns, `height` = character rows.
    static std::string render(const float* samples, size_t numSamples,
                              float sampleRate = 44100.0f,
                              int width = 72, int height = 14)
    {
        if (numSamples == 0 || width < 4 || height < 2)
            return "(no data)\n";

        // Use up to kFFTSize samples
        size_t N = std::min(numSamples, kFFTSize);
        // Pad to power of 2
        size_t fftN = 1;
        while (fftN < N) fftN <<= 1;

        // Copy + Hann window
        std::vector<float> re(fftN, 0.0f);
        std::vector<float> im(fftN, 0.0f);
        for (size_t i = 0; i < N; ++i)
        {
            float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i)
                                                / static_cast<float>(N - 1)));
            re[i] = samples[i] * w;
        }

        // In-place radix-2 FFT
        fft(re.data(), im.data(), fftN);

        // Compute magnitude in dB (only first half — positive frequencies)
        size_t numBins = fftN / 2;
        std::vector<float> magDb(numBins);
        float peakDb = -200.0f;
        for (size_t i = 0; i < numBins; ++i)
        {
            float mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) / static_cast<float>(fftN);
            float db = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -120.0f;
            magDb[i] = db;
            if (db > peakDb) peakDb = db;
        }

        // dB range: peak down to -90 dB
        float dbFloor = std::max(peakDb - 90.0f, -120.0f);
        float dbRange = peakDb - dbFloor;
        if (dbRange < 1.0f) dbRange = 1.0f;

        // Map bins to display columns (log frequency scale)
        // Frequency range: 20 Hz to Nyquist
        int pxW = width * 2;
        int pxH = height * 4;
        float freqMin = 20.0f;
        float freqMax = sampleRate * 0.5f;
        float logMin = std::log10(freqMin);
        float logMax = std::log10(freqMax);

        // For each pixel column, find the corresponding frequency range and max dB
        std::vector<float> colDb(pxW, dbFloor);
        float binWidth = sampleRate / static_cast<float>(fftN);

        for (int x = 0; x < pxW; ++x)
        {
            float logF0 = logMin + (logMax - logMin) * static_cast<float>(x) / static_cast<float>(pxW);
            float logF1 = logMin + (logMax - logMin) * static_cast<float>(x + 1) / static_cast<float>(pxW);
            float f0 = std::pow(10.0f, logF0);
            float f1 = std::pow(10.0f, logF1);

            int bin0 = std::max(1, static_cast<int>(f0 / binWidth));
            int bin1 = std::max(bin0 + 1, static_cast<int>(f1 / binWidth) + 1);
            bin1 = std::min(bin1, static_cast<int>(numBins));

            float maxDb = dbFloor;
            for (int b = bin0; b < bin1; ++b)
            {
                if (magDb[b] > maxDb) maxDb = magDb[b];
            }
            colDb[x] = maxDb;
        }

        // Map dB to pixel Y (top = peak, bottom = floor)
        std::vector<int> barHeight(pxW);
        for (int x = 0; x < pxW; ++x)
        {
            float norm = (colDb[x] - dbFloor) / dbRange;
            norm = std::max(0.0f, std::min(1.0f, norm));
            barHeight[x] = static_cast<int>(norm * pxH + 0.5f);
        }

        // Braille dot bits
        static const int dotBits[4][2] = {
            {0x01, 0x08},
            {0x02, 0x10},
            {0x04, 0x20},
            {0x40, 0x80}
        };

        // Frequency labels for reference lines
        static const float refFreqs[] = {100.0f, 1000.0f, 10000.0f};
        static const char* refLabels[] = {"100", "1k", "10k"};
        int refCols[3];
        for (int i = 0; i < 3; ++i)
        {
            float logF = std::log10(refFreqs[i]);
            float t = (logF - logMin) / (logMax - logMin);
            refCols[i] = static_cast<int>(t * width);
            refCols[i] = std::clamp(refCols[i], 0, width - 1);
        }

        std::ostringstream oss;

        // Top border with dB label
        char topLabel[16];
        std::snprintf(topLabel, sizeof(topLabel), "%+.0f", peakDb);
        oss << topLabel;
        int pad = 4 - static_cast<int>(std::strlen(topLabel));
        for (int i = 0; i < pad; ++i) oss << ' ';
        for (int x = 0; x < width; ++x) oss << "\u2584";
        oss << '\n';

        // Render rows
        for (int row = 0; row < height; ++row)
        {
            int cellTop = row * 4;

            // Row label (dB markers at top, middle, bottom)
            if (row == height / 2)
            {
                float midDb = peakDb - dbRange * 0.5f;
                char label[8];
                std::snprintf(label, sizeof(label), "%+.0f", midDb);
                oss << label;
                int lpad = 4 - static_cast<int>(std::strlen(label));
                for (int i = 0; i < lpad; ++i) oss << ' ';
            }
            else
            {
                oss << "    ";
            }

            for (int col = 0; col < width; ++col)
            {
                int bits = 0;

                for (int dx = 0; dx < 2; ++dx)
                {
                    int px = col * 2 + dx;
                    int bh = barHeight[px];
                    // Bar grows from bottom up
                    int barTop = pxH - bh;

                    for (int dy = 0; dy < 4; ++dy)
                    {
                        int pixY = cellTop + dy;
                        if (pixY >= barTop)
                            bits |= dotBits[dy][dx];
                    }
                }

                oss << encodeUTF8(0x2800 + bits);
            }
            oss << '\n';
        }

        // Bottom border with dB floor
        char botLabel[16];
        std::snprintf(botLabel, sizeof(botLabel), "%+.0f", dbFloor);
        oss << botLabel;
        pad = 4 - static_cast<int>(std::strlen(botLabel));
        for (int i = 0; i < pad; ++i) oss << ' ';
        for (int x = 0; x < width; ++x) oss << "\u2580";
        oss << '\n';

        // Frequency axis
        oss << "    ";
        std::string freqLine(width, ' ');
        for (int i = 0; i < 3; ++i)
        {
            int pos = refCols[i];
            const char* lbl = refLabels[i];
            int len = static_cast<int>(std::strlen(lbl));
            // Center label on position
            int start = std::max(0, pos - len / 2);
            if (start + len > width) start = width - len;
            for (int c = 0; c < len; ++c)
                freqLine[start + c] = lbl[c];
        }
        oss << freqLine << '\n';

        return oss.str();
    }

    /// Total lines rendered (for ANSI cursor movement).
    static constexpr int totalLines(int height) { return height + 4; }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    /// In-place radix-2 Cooley-Tukey FFT.
    static void fft(float* re, float* im, size_t N)
    {
        // Bit-reversal permutation
        size_t j = 0;
        for (size_t i = 1; i < N - 1; ++i)
        {
            size_t bit = N >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }

        // Butterfly stages
        for (size_t len = 2; len <= N; len <<= 1)
        {
            float angle = -2.0f * kPi / static_cast<float>(len);
            float wRe = std::cos(angle);
            float wIm = std::sin(angle);

            for (size_t i = 0; i < N; i += len)
            {
                float curRe = 1.0f, curIm = 0.0f;
                for (size_t k = 0; k < len / 2; ++k)
                {
                    size_t u = i + k;
                    size_t v = i + k + len / 2;
                    float tRe = curRe * re[v] - curIm * im[v];
                    float tIm = curRe * im[v] + curIm * re[v];
                    re[v] = re[u] - tRe;
                    im[v] = im[u] - tIm;
                    re[u] += tRe;
                    im[u] += tIm;
                    float newRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = newRe;
                }
            }
        }
    }

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
