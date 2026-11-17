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

juce::ValueTree IRData::toValueTree() const
{
    juce::ValueTree t (treeType);
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

IRData IRData::fromValueTree (const juce::ValueTree& t)
{
    IRData ir;
    if (! t.hasType (treeType))
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
void CabIR::prepare (const juce::dsp::ProcessSpec& spec)
{
    convolution.prepare ({ spec.sampleRate, spec.maximumBlockSize, 2 });

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
    convolution.reset();
    for (auto& f : fallback) { f.hp.reset(); f.lp1.reset(); f.lp2.reset(); f.presence.reset(); }
}

void CabIR::load (const IRData& ir)
{
    if (! ir.isValid())
    {
        clear();
        return;
    }

    // Normalise to unit energy so every IR lands at a similar loudness
    // (JUCE's own normalisation leaves IRs 18 dB quieter).
    juce::AudioBuffer<float> copy (ir.samples);
    double energy = 0.0;
    for (int i = 0; i < copy.getNumSamples(); ++i)
        energy += (double) copy.getSample (0, i) * copy.getSample (0, i);
    copy.applyGain ((float) (1.0 / std::sqrt (juce::jmax (energy, 1.0e-12))));

    convolution.loadImpulseResponse (std::move (copy), ir.sampleRate,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     juce::dsp::Convolution::Normalise::no);
    active.store (true);
}

void CabIR::clear()
{
    active.store (false);
}

void CabIR::process (juce::dsp::AudioBlock<float> block) noexcept
{
    const bool useIR = active.load();
    if (useIR != wasActive)
    {
        reset();
        wasActive = useIR;
    }

    if (useIR)
    {
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        convolution.process (ctx);
        return;
    }

    const auto nCh = juce::jmin ((int) block.getNumChannels(), 2);
    for (int c = 0; c < nCh; ++c)
    {
        auto& f = fallback[(size_t) c];
        float* d = block.getChannelPointer ((size_t) c);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            double x = f.hp.process (d[i]);
            x = f.lp1.process (x);
            x = f.lp2.process (x);
            d[i] = (float) f.presence.process (x);
        }
    }
}

} // namespace stompbox
