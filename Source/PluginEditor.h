#pragma once
#include "PluginProcessor.h"

namespace pbui { constexpr int designW = 1200, designH = 640; }

class PBLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PBLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool, bool) override;
};

class ParaBataEditor : public juce::AudioProcessorEditor
{
public:
    explicit ParaBataEditor (ParaBataProcessor&);
    ~ParaBataEditor() override;
    void resized() override;

private:
    class Content;
    ParaBataProcessor& proc;
    PBLookAndFeel laf;
    std::unique_ptr<Content> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaBataEditor)
};
