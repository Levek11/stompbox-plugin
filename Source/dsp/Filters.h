#pragma once

#include <cmath>
#include <algorithm>

// Small, allocation-free filter building blocks used by the amp model.
// All state is kept in double precision: several of these run at low cutoff
// frequencies at oversampled rates (up to ~200 kHz), where float state gets noisy.

namespace stompbox::dsp
{

constexpr double pi = 3.14159265358979323846;

//==============================================================================
/** One-pole filter in topology-preserving (TPT) form. Gives LP and HP outputs
    from a single state and stays stable under fast cutoff modulation. */
class OnePole
{
public:
    void setCutoff (double cutoffHz, double sampleRate) noexcept
    {
        cutoffHz = std::clamp (cutoffHz, 1.0, 0.49 * sampleRate);
        const double g = std::tan (pi * cutoffHz / sampleRate);
        G = g / (1.0 + g);
    }

    void reset (double value = 0.0) noexcept { s = value; }

    inline double lowpass (double x) noexcept
    {
        const double v = (x - s) * G;
        const double y = v + s;
        s = y + v;
        return y;
    }

    inline double highpass (double x) noexcept { return x - lowpass (x); }

private:
    double G = 0.5, s = 0.0;
};

//==============================================================================
/** Biquad in transposed direct form II (RBJ cookbook designs). */
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighpass (double f, double q, double fs) noexcept
    {
        const double w = 2.0 * pi * clampF (f, fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
    }

    void setLowpass (double f, double q, double fs) noexcept
    {
        const double w = 2.0 * pi * clampF (f, fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
    }

    void setPeak (double f, double q, double gainDb, double fs) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * pi * clampF (f, fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
    }

    void setLowShelf (double f, double gainDb, double fs) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * pi * clampF (f, fs) / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }

    void setHighShelf (double f, double gainDb, double fs) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * pi * clampF (f, fs) / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }

private:
    static double clampF (double f, double fs) noexcept { return std::clamp (f, 1.0, 0.49 * fs); }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
};

//==============================================================================
/** First-order shelf between two RC corner frequencies (pole/zero pair),
    bilinear-transformed. Used for cathode-bypass and "Cut" networks. */
class PoleZero
{
public:
    /** H(s) = k * (1 + s/wz) / (1 + s/wp) */
    void set (double zeroHz, double poleHz, double k, double fs) noexcept
    {
        const double c = 2.0 * fs;
        const double wz = 2.0 * pi * zeroHz, wp = 2.0 * pi * poleHz;
        const double nb0 = k * (1.0 + c / wz), nb1 = k * (1.0 - c / wz);
        const double na0 = 1.0 + c / wp, na1 = 1.0 - c / wp;
        b0 = nb0 / na0; b1 = nb1 / na0; a1 = na1 / na0;
    }

    void reset() noexcept { z = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }

private:
    double b0 = 1, b1 = 0, a1 = 0, z = 0;
};

} // namespace stompbox::dsp
