// Inline per-sample processing for Royal30Amp (included from Royal30.h).

namespace stompbox::dsp
{

namespace royal30
{
    // Gain staging between stages (volts -> volts). These stand in for the
    // mixing resistors and the input sensitivity of each following stage.
    constexpr double topBoostMix   = 0.030;  // tone stack out -> V3 grid, via mixing network
    constexpr double normalMix     = 0.100;  // Normal volume -> V3 grid
    constexpr double piInputScale  = 0.50;   // master -> PI grid
    constexpr double piGain        = 25.0;   // long-tailed pair, per side
    constexpr double powerScale    = 1.0 / 18.0; // PI plate volts -> normalised EL84 drive
    constexpr double sagAmount     = 0.35;   // GZ34 rectifier + cathode bias "bloom"
    constexpr double channelFadeMs = 15.0;
}

inline double Royal30Amp::process (double x) noexcept
{
    using namespace royal30;

    // --- Channels -----------------------------------------------------------
    // Both preamps only run together during a channel-switch crossfade.
    double mix = 0.0;
    const bool runTB = channelWeight > 0.0;
    const bool runNormal = channelWeight < 1.0;

    if (runTB)
    {
        double a = v1TopBoost.process (x);
        a = v2TopBoost.process (a * volumeGain);
        a = cathodeFollower (a);
        a = toneStack.process (a);
        mix += channelWeight * a * topBoostMix;
    }

    if (runNormal)
    {
        double n = v1Normal.process (x);
        n = normalVoicing.lowpass (n * volumeGain);
        mix += (1.0 - channelWeight) * n * normalMix;
    }

    const double targetWeight = tb ? 1.0 : 0.0;
    if (channelWeight != targetWeight)
    {
        channelWeight += (tb ? channelFadeStep : -channelFadeStep);
        channelWeight = std::clamp (channelWeight, 0.0, 1.0);
        if (channelWeight == 0.0) { v1TopBoost.reset(); v2TopBoost.reset(); toneStack.reset(); }
        if (channelWeight == 1.0) { v1Normal.reset(); normalVoicing.reset(); }
    }

    // --- Mixer stage, master, phase inverter -----------------------------
    double v = v3Mixer.process (mix) * masterGain;
    v = piCoupling.highpass (v) * piInputScale;
    double piOut = TubeCurves::get().phaseInv (v) * piGain;

    // Cut control: pot + cap across the PI outputs, a variable treble cut.
    piOut = cutNetwork.process (piOut);

    // --- Power amp with sag -----------------------------------------------
    double drive = powerCoupling.highpass (piOut) * powerScale;
    drive /= (1.0 + sagAmount * sagEnv);
    double out = TubeCurves::get().powerAmp (drive);

    const double level = std::abs (out);
    sagEnv += (level > sagEnv ? sagAttack : sagRelease) * (level - sagEnv);
    out *= (1.0 - 0.1 * sagEnv);

    // --- Output transformer into a real speaker load (no negative feedback,
    // so the speaker's impedance curve shows up in the response) ------------
    out = otHighpass.process (out);
    out = otLowpass.process (out);
    out = speakerResonance.process (out);
    out = voiceCoilRise.process (out);
    return out;
}

} // namespace stompbox::dsp
