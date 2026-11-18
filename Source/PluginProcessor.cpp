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

    // Re-apply the IR at the new sample rate.
    const juce::ScopedLock sl (irLock);
    if (currentIR.isValid())
        engine.getCab().load (currentIR);
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

    engine.process (buffer, numIn, isNonRealtime());
}

//==============================================================================
juce::Result StompBoxProcessor::loadIRFile (const juce::File& file)
{
    IRData ir;
    auto r = IRData::loadFromFile (file, ir);
    if (r.failed())
        return r;

    globalSettings().setValue ("lastIRFolder", file.getParentDirectory().getFullPathName());
    applyIR (std::move (ir));
    return r;
}

void StompBoxProcessor::clearIR()
{
    applyIR ({});
}

void StompBoxProcessor::applyIR (IRData ir)
{
    {
        const juce::ScopedLock sl (irLock);
        currentIR = std::move (ir);
        if (currentIR.isValid())
            engine.getCab().load (currentIR);
        else
            engine.getCab().clear();
    }
    ++irVersion;
}

IRData StompBoxProcessor::getIR() const
{
    const juce::ScopedLock sl (irLock);
    return currentIR;
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
    state.removeChild (state.getChildWithName (board::BoardModel::treeType), nullptr);
    {
        const juce::ScopedLock sl (irLock);
        if (currentIR.isValid())
            state.appendChild (currentIR.toValueTree(), nullptr);
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
    auto boardTree = tree.getChildWithName (board::BoardModel::treeType);
    tree.removeChild (boardTree, nullptr);

    apvts.replaceState (tree);
    applyIR (IRData::fromValueTree (irTree));
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
