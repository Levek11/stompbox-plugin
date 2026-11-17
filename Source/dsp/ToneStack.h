#pragma once

#include <array>
#include <cmath>
#include "Filters.h"

// Royal 30 Top Boost tone stack.
//
// The AC30 Top Boost network is the classic "FMV" (Fender/Marshall/Vox) passive
// tone stack, with the Middle pot replaced by a fixed 10k resistor. We use the
// closed-form 3rd-order transfer function of that network (D. Yeh & J. O. Smith,
// "Discretization of the '59 Fender Bassman Tone Stack", DAFx 2006), with the
// AC30 Top Boost component values:
//
//   R1 = 1M   treble pot (log)      C1 = 50 pF   treble cap
//   R2 = 1M   bass pot (log)        C2 = 22 nF   bass cap
//   R3 = 10k  fixed "mid" resistor  C3 = 22 nF   mid cap
//   R4 = 100k slope resistor
//
// The stack is driven by a cathode follower on the real amp (low source
// impedance), which matches the ideal-voltage-source assumption of the model.
// The analog transfer function is mapped to digital with the bilinear transform.

namespace stompbox::dsp
{

class ToneStack
{
public:
    struct Values
    {
        double R1 = 1.0e6, R2 = 1.0e6, R3 = 10.0e3, R4 = 100.0e3;
        double C1 = 50.0e-12, C2 = 22.0e-9, C3 = 22.0e-9;
    };

    void prepare (double newSampleRate) noexcept { fs = newSampleRate; reset(); }
    void reset() noexcept { x1 = x2 = x3 = y1 = y2 = y3 = 0.0; }

    /** t, l in [0, 1] are the pot positions *after* taper (fraction of resistance).
        m is fixed at 1 (the 10k resistor is fully "in"). */
    void setControls (double t, double l, double m = 1.0) noexcept
    {
        const Values& v = values;
        const double R1 = v.R1, R2 = v.R2, R3 = v.R3, R4 = v.R4;
        const double C1 = v.C1, C2 = v.C2, C3 = v.C3;
        const double m2 = m * m;

        // Analog coefficients (numerator has no constant term).
        const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);

        const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
                        - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                        + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);

        const double C123 = C1 * C2 * C3;
        const double b3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4)
                        - m2 * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                        + m * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                        + t * C123 * R1 * R3 * R4
                        - t * m * C123 * R1 * R3 * R4
                        + t * l * C123 * R1 * R2 * R4;

        const double a0 = 1.0;
        const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);

        const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                        + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4
                           + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);

        const double a3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4)
                        - m2 * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                        + m * (C123 * R3 * R3 * R4 + C123 * R1 * R3 * R3 - C123 * R1 * R3 * R4)
                        + l * C123 * R1 * R2 * R4
                        + C123 * R1 * R3 * R4;

        bilinear ({ 0.0, b1, b2, b3 }, { a0, a1, a2, a3 });
    }

    inline double process (double x) noexcept
    {
        const double y = B[0] * x + B[1] * x1 + B[2] * x2 + B[3] * x3
                       - A[1] * y1 - A[2] * y2 - A[3] * y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
    }

    /** Digital magnitude response at a frequency (for tests). */
    double magnitudeAt (double hz) const noexcept
    {
        const double w = 2.0 * pi * hz / fs;
        double nr = 0, ni = 0, dr = 0, di = 0;
        for (int n = 0; n < 4; ++n)
        {
            nr += B[(size_t) n] * std::cos (-w * n); ni += B[(size_t) n] * std::sin (-w * n);
            dr += A[(size_t) n] * std::cos (-w * n); di += A[(size_t) n] * std::sin (-w * n);
        }
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    Values values;

private:
    // Substitute s = c (1 - z^-1) / (1 + z^-1), c = 2 fs, into a 3rd-order ratio.
    void bilinear (const std::array<double, 4>& b, const std::array<double, 4>& a) noexcept
    {
        const double c = 2.0 * fs;
        // (1 - z)^k (1 + z)^(3-k) expanded, coefficient of z^-n, for k = 0..3
        static constexpr double P[4][4] = {
            { 1,  3,  3,  1 },   // (1+z)^3
            { 1,  1, -1, -1 },   // (1-z)(1+z)^2
            { 1, -1, -1,  1 },   // (1-z)^2(1+z)
            { 1, -3,  3, -1 },   // (1-z)^3
        };

        std::array<double, 4> nb {}, na {};
        double ck = 1.0;
        for (int k = 0; k < 4; ++k)
        {
            for (int n = 0; n < 4; ++n)
            {
                nb[(size_t) n] += b[(size_t) k] * ck * P[k][n];
                na[(size_t) n] += a[(size_t) k] * ck * P[k][n];
            }
            ck *= c;
        }

        for (size_t n = 0; n < 4; ++n)
        {
            B[n] = nb[n] / na[0];
            A[n] = na[n] / na[0];
        }
    }

    double fs = 48000.0;
    std::array<double, 4> B { 1, 0, 0, 0 }, A { 1, 0, 0, 0 };
    double x1 = 0, x2 = 0, x3 = 0, y1 = 0, y2 = 0, y3 = 0;
};

} // namespace stompbox::dsp
