#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace pbui;

static juce::Font fnt (float h, int style = juce::Font::plain) { return juce::Font (juce::FontOptions (h, style)); }
static const juce::Colour kStemCol[pb::kStems] = { juce::Colour (0xffff5a4f), juce::Colour (0xffffb300), juce::Colour (0xff4fc3f7),
                                                   juce::Colour (0xff9be15d), juce::Colour (0xffc792ea), juce::Colour (0xff9aa3b2) };
static const juce::Colour kAccent (0xff2fd6c4);

//==============================================================================
struct KnobSlider : public juce::Slider { juce::Colour col { 0xffffffff }; };

PBLookAndFeel::PBLookAndFeel()
{
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffa7adb8));
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff101114));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5a626d));
}
juce::Font PBLookAndFeel::getTextButtonFont (juce::TextButton& b, int) { return fnt (b.getProperties().contains ("small") ? 11.0f : 12.0f, juce::Font::bold); }

void PBLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? b.findColour (juce::TextButton::buttonOnColourId)
                    : juce::Colour (0xff1d2026).brighter (down ? 0.45f : (over ? 0.12f : 0.0f)));
    g.fillRoundedRectangle (r, 6.0f);
    if (! on) { g.setColour (juce::Colour (0xff2e323a)); g.drawRoundedRectangle (r, 6.0f, 1.0f); }
}

void PBLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool, bool)
{
    g.setColour (juce::Colour (0xff6a717a));
    for (int i = 0; i < 3; ++i) { const float o = 5.0f + (float) i * 5.0f; g.drawLine ((float) w - 3.0f, (float) h - o, (float) w - o, (float) h - 3.0f, 1.6f); }
}

void PBLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto* ks = dynamic_cast<KnobSlider*> (&s);
    const juce::Colour col = ks != nullptr ? ks->col : juce::Colours::white;
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    auto outer = juce::Rectangle<float> (D, D).withCentre (b.getCentre());
    const auto c = outer.getCentre();
    const float angle = a0 + pos * (a1 - a0), ringR = D * 0.5f - 2.5f;
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
    value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, angle, true);
    g.setColour (juce::Colour (0x28ffffff));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto body = outer.reduced (D * 0.14f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (body.translated (0.0f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff484c54), body.getX() + body.getWidth() * 0.3f, body.getY() + body.getWidth() * 0.25f,
                                             juce::Colour (0xff121417), body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (body, 1.2f);
    const float d = body.getWidth(), sx = std::sin (angle), sy = -std::cos (angle);
    g.setColour (juce::Colours::white);
    g.drawLine (c.x + sx * d * 0.18f, c.y + sy * d * 0.18f, c.x + sx * d * 0.46f, c.y + sy * d * 0.46f, 2.6f);
}

//==============================================================================
class KnobUnit : public juce::Component
{
public:
    KnobUnit (APVTS& apvts, const juce::String& pid, const juce::String& nm, int size, juce::Colour col, std::function<juce::String (double)> f)
        : name (nm), knobSize (size), fmt (std::move (f)), valueCol (col)
    {
        slider.col = col;
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (190);
        addAndMakeVisible (slider);
        att = std::make_unique<APVTS::SliderAttachment> (apvts, pid, slider);
        if (auto* prm = apvts.getParameter (pid)) slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }
    void resized() override { slider.setBounds ((getWidth() - knobSize) / 2, 0, knobSize, knobSize); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff9aa0aa)); g.setFont (fnt (11.0f, juce::Font::bold));
        g.drawText (name, 0, knobSize + 2, getWidth(), 14, juce::Justification::centred);
        g.setColour (valueCol); g.setFont (fnt (11.0f));
        g.drawText (fmt (slider.getValue()), 0, knobSize + 16, getWidth(), 14, juce::Justification::centred);
    }
    int totalHeight() const { return knobSize + 31; }
private:
    KnobSlider slider; juce::String name; int knobSize; std::function<juce::String (double)> fmt; juce::Colour valueCol;
    std::unique_ptr<APVTS::SliderAttachment> att;
};
static juce::String fmt1 (double v) { return juce::String (v, 1); }
static juce::String fmtDb (double v) { return juce::String (v, 1) + " dB"; }
static juce::String fmtMs (double v) { return juce::String ((int) std::round (v)) + " ms"; }

//==============================================================================
// Una columna por pieza: nivel, tail, mute/solo y reemplazo con samples propios (se pueden arrastrar archivos encima)
class StripBox : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    StripBox (ParaBataProcessor& p, int stemIdx) : proc (p), stem (stemIdx), col (kStemCol[stemIdx]), id (kStemIds[stemIdx])
    {
        auto& ap = p.apvts;
        const bool hit = stem < pb::kHitStems;
        level = std::make_unique<KnobUnit> (ap, id + "_lvl", "LEVEL", 56, col, fmtDb);
        level->setBounds (hit ? 8 : 60, 52, 80, level->totalHeight());
        addAndMakeVisible (*level);
        if (hit)
        {
            tail = std::make_unique<KnobUnit> (ap, id + "_tail", "TAIL", 56, col, fmtMs);
            tail->setBounds (98, 52, 80, tail->totalHeight());
            addAndMakeVisible (*tail);
            sampleLvl = std::make_unique<KnobUnit> (ap, id + "_slvl", "SAMPLE", 52, col, fmtDb);
            sampleLvl->setBounds (53, 424, 80, sampleLvl->totalHeight());
            addAndMakeVisible (*sampleLvl);
        }
        for (auto* b : { &mute, &solo }) { b->setClickingTogglesState (true); b->getProperties().set ("small", true); addAndMakeVisible (*b); }
        mute.setButtonText ("MUTE"); solo.setButtonText ("SOLO");
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe5534b));
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff2c230));
        mute.setBounds (14, 162, 76, 28); solo.setBounds (96, 162, 76, 28);
        muteAtt = std::make_unique<APVTS::ButtonAttachment> (ap, id + "_mute", mute);
        soloAtt = std::make_unique<APVTS::ButtonAttachment> (ap, id + "_solo", solo);
        if (hit)
        {
            const char* names[3] = { "Off", "Layer", "Replace" };
            for (int i = 0; i < 3; ++i)
            {
                auto& b = modeBtn[i];
                b.setButtonText (names[i]); b.setClickingTogglesState (true); b.setRadioGroupId (500 + stem);
                b.setColour (juce::TextButton::buttonOnColourId, col); b.getProperties().set ("small", true);
                b.setBounds (14 + i * 53, 232, 51, 26);
                b.onClick = [this, i] { if (auto* prm = proc.apvts.getParameter (id + "_mode")) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture(); } };
                addAndMakeVisible (b);
            }
            modeAtt = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (id + "_mode"), [this] (float v)
            { for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == (int) std::round (v), juce::dontSendNotification); });
            modeAtt->sendInitialUpdate();
            loadBtn.setButtonText ("CARGAR"); clearBtn.setButtonText ("QUITAR");
            for (auto* b : { &loadBtn, &clearBtn }) { b->getProperties().set ("small", true); addAndMakeVisible (*b); }
            loadBtn.setBounds (14, 390, 76, 28); clearBtn.setBounds (96, 390, 76, 28);
            loadBtn.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Elegi hasta 4 golpes (de suave a fuerte)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const juce::FileChooser& fc) { proc.loadSamples (stem, fc.getResults(), false); repaint(); });
            };
            clearBtn.onClick = [this] { proc.clearSamples (stem); repaint(); };
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15171c)); g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colour (0xff262930)); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        juce::Path top; top.addRoundedRectangle (0.0f, 0.0f, r.getWidth(), 4.0f, 10.0f, 10.0f, true, true, false, false);
        g.setColour (col); g.fillPath (top);
        g.setFont (fnt (13.0f, juce::Font::bold)); g.drawText (kStemNames[stem], 14, 10, 160, 18, juce::Justification::centredLeft);
        // actividad (cuanto esta "abierta" la pieza)
        g.setColour (juce::Colour (0xff0d0e11)); g.fillRoundedRectangle (14.0f, 34.0f, 158.0f, 8.0f, 3.0f);
        g.setColour (col); g.fillRoundedRectangle (14.0f, 34.0f, 158.0f * juce::jlimit (0.0f, 1.0f, activity), 8.0f, 3.0f);
        if (stem >= pb::kHitStems)
        {
            g.setColour (juce::Colour (0xff6c727c)); g.setFont (fnt (11.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Todo lo que no es un golpe: ambiente, resonancias y lo que no se pudo asignar"), 14, 232, 158, 70, juce::Justification::topLeft, 5);
            return;
        }
        g.setColour (juce::Colour (0xff868c96)); g.setFont (fnt (10.5f, juce::Font::bold));
        g.drawText ("REEMPLAZO", 14, 208, 158, 16, juce::Justification::centredLeft);
        // lista de samples
        const auto names = proc.getSampleNames (stem);
        g.setColour (juce::Colour (0xff0d0e11)); g.fillRoundedRectangle (14.0f, 266.0f, 158.0f, 118.0f, 6.0f);
        g.setFont (fnt (11.0f));
        if (names.isEmpty())
        {
            g.setColour (juce::Colour (0xff5d636d));
            g.drawFittedText (juce::String::fromUTF8 ("Arrastr\xc3\xa1 tus golpes ac\xc3\xa1,\no us\xc3\xa1 CARGAR"), 20, 270, 146, 114, juce::Justification::centred, 3);
        }
        else
            for (int i = 0; i < names.size(); ++i)
            {
                g.setColour (col.withAlpha (0.9f));
                g.drawText (juce::String (i + 1) + "  " + names[i], 22, 272 + i * 28, 144, 24, juce::Justification::centredLeft, true);
            }
        g.setColour (juce::Colour (0xff6c727c)); g.setFont (fnt (10.5f));
        g.drawText ("golpes detectados: " + juce::String (hits), 14, 512, 158, 16, juce::Justification::centredLeft);
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return stem < pb::kHitStems && ! f.isEmpty(); }
    void filesDropped (const juce::StringArray& f, int, int) override
    {
        juce::Array<juce::File> files; for (auto& p : f) files.add (juce::File (p));
        proc.loadSamples (stem, files, false); repaint();
    }

    void update()
    {
        const float a = proc.activity[stem].load(); const int h = proc.hitCount[stem].load();
        if (std::abs (a - activity) > 0.004f || h != hits) { activity = a; hits = h; repaint(); }
    }

private:
    ParaBataProcessor& proc; int stem; juce::Colour col; juce::String id;
    float activity = 0.0f; int hits = 0;
    std::unique_ptr<KnobUnit> level, tail, sampleLvl;
    juce::TextButton mute, solo, loadBtn, clearBtn, modeBtn[3];
    std::unique_ptr<APVTS::ButtonAttachment> muteAtt, soloAtt;
    std::unique_ptr<juce::ParameterAttachment> modeAtt;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class ParaBataEditor::Content : public juce::Component, private juce::Timer
{
public:
    Content (ParaBataProcessor& p, std::function<void (float)> scaleCb) : setScale (std::move (scaleCb))
    {
        for (int i = 0; i < pb::kStems; ++i)
        {
            strips[(size_t) i] = std::make_unique<StripBox> (p, i);
            strips[(size_t) i]->setBounds (24 + i * 193, 100, 186, 532);
            addAndMakeVisible (*strips[(size_t) i]);
        }
        const char* id[] = { "sens", "focus", "kicktom", "hatcym", "velsens" };
        const char* nm[] = { "SENS", "FOCUS", "KICK<>TOM", "HAT<>CYM", "VELOCITY" };
        for (int i = 0; i < 5; ++i)
        {
            knobs[(size_t) i] = std::make_unique<KnobUnit> (p.apvts, id[i], nm[i], 44, kAccent, fmt1);
            knobs[(size_t) i]->setBounds (330 + i * 92, 6, 88, knobs[(size_t) i]->totalHeight());
            addAndMakeVisible (*knobs[(size_t) i]);
        }
        midi.setButtonText ("MIDI OUT"); midi.setClickingTogglesState (true); midi.setColour (juce::TextButton::buttonOnColourId, kAccent);
        midi.setBounds (806, 30, 90, 30); addAndMakeVisible (midi);
        midiAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "midiout", midi);
        const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f }; const char* labels[] = { "75%", "100%", "125%", "150%" };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = scaleButtons[(size_t) i];
            b.setButtonText (labels[i]); b.setClickingTogglesState (true); b.setRadioGroupId (101);
            b.setBounds (960 + i * 58 - 20, 36, 52, 26); b.getProperties().set ("small", true);
            const float s = scales[i]; b.onClick = [this, s] { setScale (s); };
            addAndMakeVisible (b);
        }
        startTimerHz (30);
    }
    ~Content() override { stopTimer(); }

    void updateScaleHighlight (float s)
    {
        const float sc[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        for (int i = 0; i < 4; ++i) scaleButtons[(size_t) i].setToggleState (std::abs (sc[i] - s) < 0.02f, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1c21), 600.0f, 0.0f, juce::Colour (0xff0b0c0f), 600.0f, (float) designH, false));
        g.fillAll();
        g.setGradientFill (juce::ColourGradient (kAccent.withAlpha (0.10f), 600.0f, -40.0f, kAccent.withAlpha (0.0f), 600.0f, 380.0f, true));
        g.fillAll();
        g.setColour (juce::Colour (0xff2a2d34)); g.drawRect (getLocalBounds(), 1);
        g.setFont (fnt (24.0f, juce::Font::bold));
        g.setColour (juce::Colours::white); g.drawText ("PARA", 26, 12, 80, 32, juce::Justification::centredLeft);
        g.setColour (kAccent); g.drawText ("BATA", 94, 12, 90, 32, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff868c96)); g.setFont (fnt (10.5f, juce::Font::bold));
        g.drawText ("DRUM STEM SEPARATOR  -  VST3", 28, 46, 240, 14, juce::Justification::centredLeft);
        g.setFont (fnt (10.0f)); g.setColour (juce::Colour (0xff6c727c));
        g.drawText (juce::String::fromUTF8 ("Salidas: Mix + una por pieza (activalas en tu DAW)"), 28, 64, 290, 14, juce::Justification::centredLeft);
        g.drawText ("ESCALA", 866, 40, 66, 18, juce::Justification::centredRight);
    }

private:
    void timerCallback() override { for (auto& s : strips) s->update(); }
    std::function<void (float)> setScale;
    std::array<std::unique_ptr<StripBox>, pb::kStems> strips;
    std::array<std::unique_ptr<KnobUnit>, 5> knobs;
    juce::TextButton midi;
    std::unique_ptr<APVTS::ButtonAttachment> midiAtt;
    std::array<juce::TextButton, 4> scaleButtons;
};

//==============================================================================
ParaBataEditor::ParaBataEditor (ParaBataProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    const int savedW = juce::jlimit (600, 1800, (int) proc.apvts.state.getProperty ("uiWidth", designW));
    content = std::make_unique<Content> (p, [this] (float s) { setSize (juce::roundToInt ((float) designW * s), juce::roundToInt ((float) designH * s)); });
    content->setLookAndFeel (&laf);
    addAndMakeVisible (*content);
    setResizeLimits (600, 320, 1800, 960);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setSize (savedW, juce::roundToInt ((double) savedW * designH / designW));
}
ParaBataEditor::~ParaBataEditor() { content->setLookAndFeel (nullptr); }
void ParaBataEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content->setBounds (0, 0, designW, designH);
    content->setTransform (juce::AffineTransform::scale (s));
    content->updateScaleHighlight (s);
    proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}
