#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace stompbox;

StompBoxProcessor::StompBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "StompBox", params::createLayout())
{
    pChannel = apvts.getRawParameterValue (params::channel);
    pVolume  = apvts.getRawParameterValue (params::volume);
    pTreble  = apvts.getRawParameterValue (params::treble);
    pBass    = apvts.getRawParameterValue (params::bass);
    pCut     = apvts.getRawParameterValue (params::cut);
    pMaster  = apvts.getRawParameterValue (params::master);
    pOutput  = apvts.getRawParameterValue (params::output);

    engine.setBoard (board);

    const int q = globalSettings().getIntValue ("quality", (int) Quality::studio);
    engine.setQuality (q == (int) Quality::live ? Quality::live : Quality::studio);
}

//==============================================================================
bool StompBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono -> mono (Logic's "Mono" on a mono track: both amps hear the same
    // signal, so one amp's output is the whole sound), mono -> stereo, and
    // stereo -> stereo (each side into its own Royal 30).
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return (in == mono && out == mono)
        || (in == mono && out == stereo)
        || (in == stereo && out == stereo);
}

void StompBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (Engine::latencySamples);   // always 0

    // Re-apply the IRs at the new sample rate.
    const juce::ScopedLock sl (irLock);
    engine.getCab().load (irs[0], irs[1], cabLinked);
}

void StompBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    dsp::Royal30Settings s;
    s.topBoost = pChannel->load() > 0.5f;
    s.volume = pVolume->load();
    s.treble = pTreble->load();
    s.bass   = pBass->load();
    s.cut    = pCut->load();
    s.master = pMaster->load();
    engine.setAmpSettings (s);
    engine.setOutputDb (pOutput->load());

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    engine.setTempo (bpm);

    engine.process (buffer, numIn, isNonRealtime());
}

//==============================================================================
juce::Result StompBoxProcessor::loadIRFile (const juce::File& file, int side)
{
    IRData ir;
    auto r = IRData::loadFromFile (file, ir);
    if (r.failed())
        return r;

    globalSettings().setValue ("lastIRFolder", file.getParentDirectory().getFullPathName());
    {
        const juce::ScopedLock sl (irLock);
        irs[(size_t) juce::jlimit (0, 1, side)] = std::move (ir);
    }
    applyIRs();
    return r;
}

void StompBoxProcessor::clearIR (int side)
{
    {
        const juce::ScopedLock sl (irLock);
        irs[(size_t) juce::jlimit (0, 1, side)] = {};
    }
    applyIRs();
}

void StompBoxProcessor::setCabLinked (bool linked)
{
    {
        const juce::ScopedLock sl (irLock);
        cabLinked = linked;
    }
    applyIRs();
}

bool StompBoxProcessor::isCabLinked() const
{
    const juce::ScopedLock sl (irLock);
    return cabLinked;
}

void StompBoxProcessor::applyIRs()
{
    {
        const juce::ScopedLock sl (irLock);
        engine.getCab().load (irs[0], irs[1], cabLinked);
    }
    ++irVersion;
    if (juce::MessageManager::existsAndIsCurrentThread())
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

IRData StompBoxProcessor::getIR (int side) const
{
    const juce::ScopedLock sl (irLock);
    return irs[(size_t) juce::jlimit (0, 1, side)];
}

juce::File StompBoxProcessor::getLastIRFolder() const
{
    const juce::File f (const_cast<StompBoxProcessor*> (this)->globalSettings().getValue ("lastIRFolder"));
    return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

float StompBoxProcessor::getEditorScale() const
{
    constexpr float defaultScale = 0.88f;
    const auto v = (float) const_cast<StompBoxProcessor*> (this)->globalSettings().getDoubleValue ("editorScale", defaultScale);
    return juce::jlimit (0.6f, 1.5f, v);
}

void StompBoxProcessor::setEditorScale (float scale)
{
    globalSettings().setValue ("editorScale", juce::jlimit (0.6, 1.5, (double) scale));
}

//==============================================================================
void StompBoxProcessor::setQuality (Quality q)
{
    engine.setQuality (q);
    globalSettings().setValue ("quality", (int) q);
}

juce::PropertiesFile& StompBoxProcessor::globalSettings()
{
    if (settings == nullptr)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "StompBox";
        o.folderName = "StompBox";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    return *settings;
}

//==============================================================================
// State = all parameters + the cab IR itself (so a Logic project restores the
// full rig even if the IR file is gone). Live/Studio is global, not saved here.
void StompBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (IRData::treeType), nullptr);
    state.removeChild (state.getChildWithName (IRData::rightTreeType), nullptr);
    state.removeChild (state.getChildWithName (board::BoardModel::treeType), nullptr);
    {
        const juce::ScopedLock sl (irLock);
        if (irs[0].isValid())
            state.appendChild (irs[0].toValueTree (IRData::treeType), nullptr);
        if (irs[1].isValid())
            state.appendChild (irs[1].toValueTree (IRData::rightTreeType), nullptr);
        state.setProperty ("cabLinked", cabLinked, nullptr);
    }
    state.appendChild (getBoard().toValueTree(), nullptr);
    state.setProperty ("stateVersion", 1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void StompBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto irTree = tree.getChildWithName (IRData::treeType);
    tree.removeChild (irTree, nullptr);
    auto irRightTree = tree.getChildWithName (IRData::rightTreeType);
    tree.removeChild (irRightTree, nullptr);
    const bool linked = (bool) tree.getProperty ("cabLinked", true);   // older projects: one IR, linked
    auto boardTree = tree.getChildWithName (board::BoardModel::treeType);
    tree.removeChild (boardTree, nullptr);

    apvts.replaceState (tree);
    {
        const juce::ScopedLock sl (irLock);
        irs[0] = IRData::fromValueTree (irTree, IRData::treeType);
        irs[1] = IRData::fromValueTree (irRightTree, IRData::rightTreeType);
        cabLinked = linked;
    }
    applyIRs();
    editBoard ([&] (board::BoardModel& b) { b.fromValueTree (boardTree); });
}

//==============================================================================
juce::AudioProcessorEditor* StompBoxProcessor::createEditor()
{
    return new StompBoxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StompBoxProcessor();
}
