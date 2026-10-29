#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace phaselab::ui
{

// Colours of the preview in docs/spec/PhaseLab_Konzept_Bus_und_Channel_Analyzer.html
namespace colours
{
    inline const juce::Colour panel { 0xff292f34 }, headTop { 0xff3b444a }, headBottom { 0xff2d353a },
        bar { 0xff232a2f }, graph { 0xff171f24 }, field { 0xff20292f }, monitor { 0xff252d32 },
        strip { 0xff30383e }, line { 0xff46515a }, grid { 0xff344149 }, dark { 0xff131b20 },
        text { 0xffdce2e5 }, sub { 0xffaebbc2 }, a { 0xff80c2c8 }, b { 0xffd2ad79 }, amber { 0xffe0b77a },
        peak { 0xffdab77d }, good { 0xff82c4ae }, bad { 0xffc8766a }, loss { 0xffd99383 },
        warnBack { 0xff4a3d27 }, warnText { 0xfff0d3a2 }, adviceBack { 0xff333331 }, adviceText { 0xffd1c9b9 },
        adviceTitle { 0xffe1c599 };
}

// UI text in German or English (D-06). Sources are UTF-8.
inline juce::String tr (bool english, const char* german, const char* englishText)
{
    return juce::String::fromUTF8 (english ? englishText : german);
}

// Number with a comma in German, a point in English, and a real minus sign.
juce::String number (double value, int places, bool english, bool showPlus = false);
juce::String frequency (double hz, bool english);

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

} // namespace phaselab::ui
