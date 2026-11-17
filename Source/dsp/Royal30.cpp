#include "Royal30.h"

namespace stompbox::dsp
{

//==============================================================================
void Royal30Amp::prepare (double oversampledRate)
{
    fs = oversampledRate;

    // V1, Top Boost channel: 220k plate load, fully bypassed cathode.
    TriodeStage::Config v1;
    v1.gain = 55.0; v1.hasCouplingCap = false; v1.couplingHz = 20.0; v1.millerHz = 20000.0;
    v1TopBoost.prepare (v1, fs);

    // V1, Normal channel: similar stage, voiced a little darker afterwards.
    TriodeStage::Config vn = v1;
    vn.gain = 50.0; vn.millerHz = 14000.0;
    v1Normal.prepare (vn, fs);

    // V2a, Top Boost gain stage: partially bypassed cathode keeps the lows
    // from flubbing out when it clips.
    TriodeStage::Config v2;
    v2.gain = 45.0; v2.couplingHz = 30.0; v2.millerHz = 15000.0;
    v2.unbypassedRatio = 0.5; v2.bypassHz = 110.0;
    v2TopBoost.prepare (v2, fs);

    // V3 mixer stage.
    TriodeStage::Config v3;
    v3.gain = 35.0; v3.couplingHz = 25.0; v3.millerHz = 12000.0;
    v3Mixer.prepare (v3, fs);

    toneStack.prepare (fs);
    normalVoicing.setCutoff (5500.0, fs);

    piCoupling.setCutoff (15.0, fs);
    powerCoupling.setCutoff (20.0, fs);

    sagAttack  = 1.0 - std::exp (-1.0 / (0.008 * fs));
    sagRelease = 1.0 - std::exp (-1.0 / (0.150 * fs));

    otHighpass.setHighpass (45.0, 0.707, fs);
    otLowpass.setLowpass (15000.0, 0.707, fs);
    speakerResonance.setPeak (105.0, 1.4, 2.5, fs);
    voiceCoilRise.setHighShelf (3500.0, 2.5, fs);

    channelFadeStep = 1.0 / (royal30::channelFadeMs * 0.001 * fs);
    cutCached = -1.0;
    reset();
}

void Royal30Amp::reset()
{
    v1TopBoost.reset(); v2TopBoost.reset(); v1Normal.reset(); v3Mixer.reset();
    toneStack.reset(); normalVoicing.reset(); cutNetwork.reset();
    piCoupling.reset(); powerCoupling.reset();
    otHighpass.reset(); otLowpass.reset(); speakerResonance.reset(); voiceCoilRise.reset();
    sagEnv = 0.0;
    channelWeight = tb ? 1.0 : 0.0;
}

void Royal30Amp::setControls (bool topBoost, double newVolumeGain, double treble, double bass,
                              double cutAmount, double newMasterGain, bool updateToneStack) noexcept
{
    tb = topBoost;
    volumeGain = newVolumeGain;
    masterGain = newMasterGain;

    if (updateToneStack)
        toneStack.setControls (treble, bass);

    if (cutAmount != cutCached)
    {
        cutCached = cutAmount;
        // Cut pot (250k) in series with 10k and a 4.7 nF cap, across the two
        // PI plates (~47k source impedance each). Per half-circuit, that is a
        // shunt of (Rpot + 10k)/2 in series with 2C, giving a treble shelf.
        const double rPot = 250.0e3 * (1.0 - cutAmount) * (1.0 - cutAmount);
        const double rHalf = (rPot + 10.0e3) * 0.5;
        const double cEff = 2.0 * 4.7e-9;
        const double rOut = 47.0e3;
        const double zeroHz = 1.0 / (2.0 * pi * rHalf * cEff);
        const double poleHz = 1.0 / (2.0 * pi * (rOut + rHalf) * cEff);
        cutNetwork.set (zeroHz, poleHz, 1.0, fs);
    }
}

//==============================================================================
void Royal30::prepare (double oversampledRate)
{
    fs = oversampledRate;
    for (auto& a : amps) a.prepare (fs);
    firstBlock = true;
}

void Royal30::reset()
{
    for (auto& a : amps) a.reset();
    firstBlock = true;
}

void Royal30::setSettings (const Royal30Settings& s) noexcept
{
    target.topBoost.store (s.topBoost);
    target.volume.store (s.volume);
    target.treble.store (s.treble);
    target.bass.store (s.bass);
    target.cut.store (s.cut);
    target.master.store (s.master);
}

void Royal30::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    constexpr int controlInterval = 16;
    const int rampSteps = std::max (1, (int) (0.02 * fs / controlInterval)); // ~20 ms

    const bool topBoost = target.topBoost.load();
    const double vol = audioTaper (target.volume.load());
    const double mas = audioTaper (target.master.load());
    const double tre = audioTaper (target.treble.load());   // 1M log pots
    const double bas = audioTaper (target.bass.load());
    const double cutv = target.cut.load();

    if (firstBlock)
    {
        volume.snap (vol); master.snap (mas); treble.snap (tre); bass.snap (bas); cut.snap (cutv);
        for (auto& a : amps) a.setControls (topBoost, vol, tre, bas, cutv, mas, true);
        firstBlock = false;
    }
    else
    {
        volume.setTarget (vol, rampSteps); master.setTarget (mas, rampSteps);
        treble.setTarget (tre, rampSteps); bass.setTarget (bas, rampSteps);
        cut.setTarget (cutv, rampSteps);
    }

    const int nAmps = std::min (numChannels, 2);
    lastTopBoost = topBoost;

    for (int start = 0; start < numSamples; start += controlInterval)
    {
        const int n = std::min (controlInterval, numSamples - start);

        volume.tick(); master.tick(); cut.tick();
        const bool toneChanged = treble.tick() | bass.tick();

        for (int c = 0; c < nAmps; ++c)
        {
            auto& amp = amps[(size_t) c];
            amp.setControls (topBoost, volume.current, treble.current, bass.current,
                             cut.current, master.current, toneChanged);

            float* d = channels[c] + start;
            for (int i = 0; i < n; ++i)
                d[i] = (float) (amp.process ((double) d[i] * voltsAtFullScale) * outputTrim);
        }
    }
}

} // namespace stompbox::dsp
