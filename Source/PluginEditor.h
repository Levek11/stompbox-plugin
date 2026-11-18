#pragma once

#include "PluginProcessor.h"
#include "ui/StompLookAndFeel.h"
#include "ui/AmpCabTab.h"
#include "ui/PedalboardTab.h"

namespace stompbox::ui
{

/** Text tab with an accent underline when selected. */
struct TabButton : juce::Button
{
    explicit TabButton (const juce::String& t) : juce::Button (t) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** One half of the Live / Studio switch. */
struct SegmentButton : juce::Button
{
    explicit SegmentButton (const juce::String& t) : juce::Button (t) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** Everything inside the window, laid out at a fixed design size and scaled. */
class MainView : public juce::Component
{
public:
    static constexpr int designWidth = 1440, designHeight = 900, headerHeight = 64;

    explicit MainView (StompBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showTab (int index);
    void updateQualityButtons();
    void updateBoardSizeButtons();
    void setBoardSize (int size);

    StompBoxProcessor& processor;
    TabButton pedalboardTab { "Pedalboard" }, ampTab { "Amp / Cab" };
    SegmentButton liveButton { "Live" }, studioButton { "Studio" };
    SegmentButton sizeButtons[3] { SegmentButton { "Small" }, SegmentButton { "Medium" }, SegmentButton { "Large" } };
    PedalboardTab pedalboard;
    AmpCabTab ampCab;
};

} // namespace stompbox::ui

//==============================================================================
class StompBoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit StompBoxEditor (StompBoxProcessor&);
    ~StompBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StompBoxProcessor& processor;
    bool sizeRestored = false;
    stompbox::ui::StompLookAndFeel lookAndFeel;
    stompbox::ui::MainView view;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StompBoxEditor)
};
