#include "CabIR.h"

namespace stompbox
{

//==============================================================================
juce::Result IRData::loadFromFile (const juce::File& file, IRData& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return juce::Result::fail ("Couldn't read \"" + file.getFileName() + "\". Use a WAV or AIFF impulse response.");

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail ("\"" + file.getFileName() + "\" is empty.");

    const auto maxLen = (juce::int64) std::ceil (reader->sampleRate * maxSeconds);
    const int length = (int) std::min (reader->lengthInSamples, maxLen);
    const int chans = (int) juce::jlimit (1u, 8u, reader->numChannels);

    juce::AudioBuffer<float> raw (chans, length);
    reader->read (&raw, 0, length, 0, true, chans > 1);

    IRData ir;
    ir.name = file.getFileNameWithoutExtension();
    ir.sampleRate = reader->sampleRate;
    ir.samples.setSize (1, length);
    ir.samples.clear();
    for (int c = 0; c < chans; ++c)
        ir.samples.addFrom (0, 0, raw, c, 0, length, 1.0f / (float) chans);

    if (ir.samples.getMagnitude (0, 0, length) <= 1.0e-6f)
        return juce::Result::fail ("\"" + file.getFileName() + "\" is silent.");

    out = std::move (ir);
    return juce::Result::ok();
}

bool IRData::sameAs (const IRData& o) const
{
    if (sampleRate != o.sampleRate || samples.getNumSamples() != o.samples.getNumSamples() || isValid() != o.isValid())
        return false;
    for (int i = 0; i < samples.getNumSamples(); ++i)
        if (samples.getSample (0, i) != o.samples.getSample (0, i))
            return false;
    return true;
}

juce::ValueTree IRData::toValueTree (const juce::Identifier& type) const
{
    juce::ValueTree t (type);
    if (! isValid())
        return t;

    t.setProperty ("name", name, nullptr);
    t.setProperty ("sampleRate", sampleRate, nullptr);
    t.setProperty ("numSamples", samples.getNumSamples(), nullptr);

    // Little-endian float32 samples, base64 encoded.
    juce::MemoryBlock mb ((size_t) samples.getNumSamples() * sizeof (float));
    auto* dst = static_cast<char*> (mb.getData());
    const float* src = samples.getReadPointer (0);
    for (int i = 0; i < samples.getNumSamples(); ++i)
    {
        juce::uint32 bits;
        std::memcpy (&bits, src + i, sizeof (bits));
        bits = juce::ByteOrder::swapIfBigEndian (bits);
        std::memcpy (dst + (size_t) i * 4, &bits, 4);
    }
    t.setProperty ("data", mb.toBase64Encoding(), nullptr);
    return t;
}

IRData IRData::fromValueTree (const juce::ValueTree& t, const juce::Identifier& type)
{
    IRData ir;
    if (! t.hasType (type))
        return ir;

    const int n = (int) t.getProperty ("numSamples", 0);
    const double sr = (double) t.getProperty ("sampleRate", 0.0);
    juce::MemoryBlock mb;
    if (n <= 0 || sr <= 0.0 || ! mb.fromBase64Encoding (t.getProperty ("data").toString())
        || mb.getSize() != (size_t) n * 4)
        return ir;

    ir.name = t.getProperty ("name").toString();
    ir.sampleRate = sr;
    ir.samples.setSize (1, n);
    const auto* src = static_cast<const char*> (mb.getData());
    float* dst = ir.samples.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        juce::uint32 bits;
        std::memcpy (&bits, src + (size_t) i * 4, 4);
        bits = juce::ByteOrder::swapIfBigEndian (bits);
        std::memcpy (dst + i, &bits, sizeof (float));
    }
    return ir;
}

std::vector<float> IRData::overview (int buckets) const
{
    std::vector<float> v ((size_t) juce::jmax (0, buckets), 0.0f);
    if (! isValid() || buckets <= 0)
        return v;

    // Show the first ~60 ms, where a cab IR's energy lives.
    const int len = juce::jmin (samples.getNumSamples(), (int) (sampleRate * 0.06) + 1);
    const float peak = juce::jmax (1.0e-9f, samples.getMagnitude (0, 0, len));
    for (int b = 0; b < buckets; ++b)
    {
        const int s0 = b * len / buckets, s1 = juce::jmax (s0 + 1, (b + 1) * len / buckets);
        v[(size_t) b] = samples.getMagnitude (0, s0, juce::jmin (s1, len) - s0) / peak;
    }
    return v;
}

//==============================================================================
static int onsetIndex (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples();
    const float peak = b.getMagnitude (0, 0, n);
    for (int i = 0; i < n; ++i)
        if (std::abs (b.getSample (0, i)) >= 0.1f * peak)
            return i;
    return 0;
}

static void trimFront (IRData& ir, int count)
{
    count = juce::jlimit (0, ir.samples.getNumSamples() - 1, count);
    if (count == 0) return;
    juce::AudioBuffer<float> t (1, ir.samples.getNumSamples() - count);
    t.copyFrom (0, 0, ir.samples, 0, count, t.getNumSamples());
    ir.samples = std::move (t);
}

void CabIR::alignPair (IRData& a, IRData& b)
{
    if (! a.isValid() || ! b.isValid())
        return;

    // 1. Both start a fraction of a millisecond before their first strong
    //    peak (leading silence and pre-delay removed; no latency added).
    for (auto* ir : { &a, &b })
    {
        const int pre = (int) std::round (0.0002 * ir->sampleRate);
        trimFront (*ir, onsetIndex (ir->samples) - pre);
    }

    // 2. Same sample rate: refine the offset with a cross-correlation over the
    //    first 2 ms (within +-0.5 ms), and fix inverted polarity.
    if (a.sampleRate != b.sampleRate)
        return;
    const int window = juce::jmin ((int) (0.002 * a.sampleRate), a.samples.getNumSamples(), b.samples.getNumSamples());
    const int maxLag = (int) (0.0005 * a.sampleRate);
    const float* x = a.samples.getReadPointer (0);
    const float* y = b.samples.getReadPointer (0);
    double best = 0.0; int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const int j = i + lag;
            if (j >= 0 && j < b.samples.getNumSamples()) sum += (double) x[i] * y[j];
        }
        if (std::abs (sum) > std::abs (best)) { best = sum; bestLag = lag; }
    }
    if (bestLag > 0) trimFront (b, bestLag);
    else if (bestLag < 0) trimFront (a, -bestLag);
    if (best < 0.0) b.samples.applyGain (-1.0f);
}

//==============================================================================
void CabIR::prepare (const juce::dsp::ProcessSpec& spec)
{
    for (auto& c : conv)
        c.prepare ({ spec.sampleRate, spec.maximumBlockSize, 1 });

    for (auto& f : fallback)
    {
        f.hp.setHighpass (80.0, 0.707, spec.sampleRate);
        f.lp1.setLowpass (5000.0, 0.6, spec.sampleRate);
        f.lp2.setLowpass (6500.0, 0.9, spec.sampleRate);
        f.presence.setPeak (2200.0, 1.0, 3.0, spec.sampleRate);
    }
    reset();
}

void CabIR::reset()
{
    for (auto& c : conv) c.reset();
    for (auto& f : fallback) { f.hp.reset(); f.lp1.reset(); f.lp2.reset(); f.presence.reset(); }
}

void CabIR::loadSide (int side, const IRData& ir)
{
    if (! ir.isValid())
    {
        active[(size_t) side].store (false);
        return;
    }

    // Normalise to unit energy so every IR lands at a similar loudness
    // (JUCE's own normalisation leaves IRs 18 dB quieter).
    juce::AudioBuffer<float> copy (ir.samples);
    double energy = 0.0;
    for (int i = 0; i < copy.getNumSamples(); ++i)
        energy += (double) copy.getSample (0, i) * copy.getSample (0, i);
    copy.applyGain ((float) (1.0 / std::sqrt (juce::jmax (energy, 1.0e-12))));

    conv[(size_t) side].loadImpulseResponse (std::move (copy), ir.sampleRate,
                                             juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::yes,
                                             juce::dsp::Convolution::Normalise::no);
    active[(size_t) side].store (true);
}

void CabIR::load (const IRData& left, const IRData& right, bool linked)
{
    IRData l = left.isValid() ? left : right;
    IRData r = (linked || ! right.isValid()) ? l : right;

    const bool different = l.isValid() && r.isValid() && ! l.sameAs (r);
    if (different)
        alignPair (l, r);

    loadSide (0, l);
    loadSide (1, r);
    dual.store (different);
}

void CabIR::process (juce::dsp::AudioBlock<float> block) noexcept
{
    const auto nCh = juce::jmin ((int) block.getNumChannels(), 2);
    for (int c = 0; c < nCh; ++c)
    {
        const bool useIR = active[(size_t) c].load();
        auto& f = fallback[(size_t) c];
        if (useIR != wasActive[(size_t) c])
        {
            conv[(size_t) c].reset();
            f.hp.reset(); f.lp1.reset(); f.lp2.reset(); f.presence.reset();
            wasActive[(size_t) c] = useIR;
        }

        auto one = block.getSingleChannelBlock ((size_t) c);
        if (useIR)
        {
            juce::dsp::ProcessContextReplacing<float> ctx (one);
            conv[(size_t) c].process (ctx);
            continue;
        }

        float* d = one.getChannelPointer (0);
        for (size_t i = 0; i < one.getNumSamples(); ++i)
        {
            double x = f.hp.process (d[i]);
            x = f.lp1.process (x);
            x = f.lp2.process (x);
            d[i] = (float) f.presence.process (x);
        }
    }
}

} // namespace stompbox
