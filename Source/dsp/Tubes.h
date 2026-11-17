#pragma once

#include <array>
#include <cmath>
#include <functional>
#include "Filters.h"

// Tube stage building blocks for the Royal 30.
//
// Signals inside the amp are kept in "volts" so that gain staging can follow
// the real circuit: grid swings of a volt or two, plate swings of tens of volts.
// Nonlinear transfer curves are precomputed into lookup tables (cheap enough
// to run two amps at 4x oversampling), while the memory effects that make
// tubes feel alive — grid-conduction bias shift and power-supply sag — run
// per sample.

namespace stompbox::dsp
{

//==============================================================================
/** Lookup table of a static transfer curve over [-range, range], with linear
    interpolation. Outside the range the curve is held flat (all curves used
    here saturate long before the edge). */
class TransferTable
{
public:
    static constexpr int size = 4096;

    void build (double newRange, const std::function<double (double)>& f)
    {
        range = newRange;
        scale = (size - 1) / (2.0 * range);
        for (int i = 0; i < size; ++i)
            table[(size_t) i] = f (-range + (double) i / scale);
    }

    inline double operator() (double x) const noexcept
    {
        const double pos = (x + range) * scale;
        if (pos <= 0.0) return table.front();
        if (pos >= size - 1) return table.back();
        const int i = (int) pos;
        const double frac = pos - i;
        return table[(size_t) i] + frac * (table[(size_t) i + 1] - table[(size_t) i]);
    }

private:
    std::array<double, size> table {};
    double range = 1.0, scale = 1.0;
};

//==============================================================================
/** Shared transfer curves, built once. */
struct TubeCurves
{
    TransferTable triode;     // ECC83 common-cathode stage, grid volts in -> normalised plate swing out
    TransferTable phaseInv;   // ECC83 long-tailed pair, near-symmetric
    TransferTable powerAmp;   // 4 x EL84 push-pull, cathode biased, no negative feedback

    static const TubeCurves& get()
    {
        static const TubeCurves curves;
        return curves;
    }

private:
    TubeCurves()
    {
        // --- ECC83 triode -------------------------------------------------
        // Plate current follows the Child-Langmuir 3/2 power law of the
        // effective grid voltage, with a smooth (softplus) knee into cutoff.
        // Bias sits ~1.5 V below the grid-conduction point; cutoff is ~2.5 V
        // below bias. Output is normalised so the small-signal slope at the
        // bias point is 1, and is limited on the "plate pulled down" side by
        // plate saturation. The stage's voltage gain is applied by the caller.
        constexpr double cutoffDistance = 2.5, knee = 6.0, plateSat = 2.0;
        auto softplus = [] (double u, double k) { return u * k > 30.0 ? u : std::log1p (std::exp (k * u)) / k; };
        auto ip = [&] (double vg) { return std::pow (softplus (vg + cutoffDistance, knee), 1.5); };
        const double ip0 = ip (0.0);
        const double slope0 = (ip (1.0e-4) - ip (-1.0e-4)) / 2.0e-4;

        triode.build (12.0, [&] (double x)
        {
            double f = (ip (x) - ip0) / slope0;
            if (f > 0.0) f = f / std::cbrt (1.0 + std::pow (f / plateSat, 3.0));
            return f;
        });

        // --- Long-tailed pair phase inverter --------------------------------
        // Differential pair: close to symmetric, with a little asymmetry from
        // unequal plate loads.
        phaseInv.build (12.0, [] (double x)
        {
            const double h = 2.4;
            const double y = (x + 0.06) / std::sqrt (1.0 + (x + 0.06) * (x + 0.06) / (h * h))
                           - 0.06 / std::sqrt (1.0 + 0.0036 / (h * h));
            return y;
        });

        // --- EL84 push-pull output stage --------------------------------------
        // Cathode-biased "class A" AC30 output: a soft, round knee with no
        // negative feedback to straighten it, plus slight tube mismatch.
        powerAmp.build (8.0, [] (double x)
        {
            auto shape = [] (double v) { return v / std::pow (1.0 + std::pow (std::abs (v), 2.5), 1.0 / 2.5); };
            constexpr double mismatch = 0.07;
            return shape (x + mismatch) - shape (mismatch);
        });
    }
};

//==============================================================================
/** One ECC83 common-cathode gain stage with its coupling cap, grid-conduction
    "blocking" bias shift, cathode bypass shelf and Miller-capacitance roll-off. */
class TriodeStage
{
public:
    struct Config
    {
        double gain = 50.0;            // small-signal voltage gain (bypassed)
        double couplingHz = 20.0;      // input coupling-cap high-pass
        double millerHz = 18000.0;     // plate-side roll-off
        double unbypassedRatio = 1.0;  // < 1: partially bypassed cathode (less low-end gain)
        double bypassHz = 100.0;       // cathode bypass corner
        bool hasCouplingCap = true;    // first stage is direct from the input jack
    };

    void prepare (const Config& newConfig, double fs)
    {
        cfg = newConfig;
        coupling.setCutoff (cfg.couplingHz, fs);
        miller.setCutoff (cfg.millerHz, fs);
        cathode.set (cfg.bypassHz, cfg.bypassHz / std::max (cfg.unbypassedRatio, 1.0e-3), cfg.unbypassedRatio, fs);
        chargeCoef = 1.0 - std::exp (-1.0 / (0.002 * fs));   // coupling cap charges in ~2 ms during grid conduction
        recoverCoef = std::exp (-1.0 / (0.030 * fs));        // ...and bleeds back through the grid leak in ~30 ms
        reset();
    }

    void reset()
    {
        coupling.reset(); miller.reset(); cathode.reset();
        bias = 0.0;
    }

    /** grid volts in -> plate volts out (inverting, like the real stage). */
    inline double process (double x) noexcept
    {
        double g = cfg.hasCouplingCap ? coupling.highpass (x) : x;
        g += bias;

        // Grid conduction: once the grid rises past the cathode, it draws
        // current, clamping the positive swing and charging the coupling cap,
        // which shifts the bias point colder for a few tens of ms.
        constexpr double conductionPoint = 1.2;
        if (g > conductionPoint)
        {
            const double over = g - conductionPoint;
            if (cfg.hasCouplingCap)
                bias -= over * chargeCoef;
            g = conductionPoint + over / (1.0 + 2.5 * over);
        }
        bias *= recoverCoef;

        const double shaped = TubeCurves::get().triode (g);
        const double plate = -cfg.gain * cathode.process (shaped);
        return miller.lowpass (plate);
    }

private:
    Config cfg;
    OnePole coupling, miller;
    PoleZero cathode;
    double bias = 0.0, chargeCoef = 0.0, recoverCoef = 1.0;
};

//==============================================================================
/** Cathode follower driving the tone stack: unity gain, clips softly when the
    preceding plate swing gets very large. */
inline double cathodeFollower (double x) noexcept
{
    constexpr double limit = 85.0;
    const double r = x / limit;
    return x / std::sqrt (std::sqrt (1.0 + r * r * r * r));
}

} // namespace stompbox::dsp
