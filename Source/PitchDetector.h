#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// YIN pitch detector (de Cheveigne & Kawahara, 2002), tuned for guitar:
// ~70 Hz .. ~1400 Hz. Window is ~23 ms, so even low E (82 Hz) gets >1.9 periods.
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        window = std::max (256, (int) std::lround (sr * 0.0232));
        window &= ~3;                                   // multiple of 4 for unrolling
        tauMin = std::max (2, (int) (sr / 1400.0));
        tauMax = (int) (sr / 70.0) + 2;
        cm.assign ((size_t) tauMax + 2, 1.0f);
    }

    // samples x[0..requiredSamples()-1], oldest first, newest last
    int requiredSamples() const { return window + tauMax + 2; }

    bool detect (const float* x, float threshold, float& freq, float& confidence)
    {
        double running = 0.0;
        cm[0] = 1.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            const float* p = x;
            const float* q = x + tau;
            for (int j = 0; j < window; j += 4)
            {
                const float d0 = p[j]     - q[j];
                const float d1 = p[j + 1] - q[j + 1];
                const float d2 = p[j + 2] - q[j + 2];
                const float d3 = p[j + 3] - q[j + 3];
                a0 += d0 * d0; a1 += d1 * d1; a2 += d2 * d2; a3 += d3 * d3;
            }
            const double diff = (double) (a0 + a1 + a2 + a3);
            running += diff;
            cm[(size_t) tau] = running > 1e-12 ? (float) (diff * tau / running) : 1.0f;
        }

        if (running < 1e-9)
            return false;

        int tau = tauMin;
        for (; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau])
                    ++tau;
                break;
            }
        }
        if (tau >= tauMax)
            return false;

        // parabolic interpolation
        const float s0 = cm[(size_t) tau - 1], s1 = cm[(size_t) tau], s2 = cm[(size_t) tau + 1];
        const float denom = s0 + s2 - 2.0f * s1;
        float shift = denom != 0.0f ? 0.5f * (s0 - s2) / denom : 0.0f;
        shift = std::max (-1.0f, std::min (1.0f, shift));

        freq = (float) (sr / ((double) tau + shift));
        confidence = 1.0f - s1;
        return true;
    }

private:
    double sr = 44100.0;
    int window = 1024, tauMin = 32, tauMax = 640;
    std::vector<float> cm;
};
