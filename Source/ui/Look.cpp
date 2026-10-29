#include "ui/Look.h"

namespace phaselab::ui
{

juce::String number (double value, int places, bool english, bool showPlus)
{
    // juce::String (double, 0) would print every digit; round explicitly.
    auto s = places <= 0 ? juce::String (juce::roundToInt (std::abs (value))) : juce::String (std::abs (value), places);
    if (! english)
        s = s.replace (".", ",");
    if (value < 0.0 && s.containsAnyOf ("123456789"))
        return juce::String::fromUTF8 ("\xe2\x88\x92") + s;
    return showPlus ? "+" + s : s;
}

juce::String frequency (double hz, bool english)
{
    if (hz >= 1000.0)
        return number (hz / 1000.0, hz >= 10000.0 ? 1 : 2, english) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

Look::Look()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::bar);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff394b54));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffc4d0d6));
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xffe7f3f4));
    setColour (juce::TooltipWindow::backgroundColourId, colours::bar);
    setColour (juce::TooltipWindow::textColourId, colours::text);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const auto special = button.findColour (juce::TextButton::buttonOnColourId);
    const bool amberButton = button.getProperties().contains ("amber");

    juce::Colour fill = on ? (amberButton ? special : juce::Colour (0xff394b54)) : juce::Colour (0xff262e34);
    if (highlighted && ! on)
        fill = fill.brighter (0.08f);
    if (down)
        fill = fill.brighter (0.15f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? (amberButton ? special : colours::a) : juce::Colour (0xff56616a));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    if (on && ! amberButton)
    {
        g.setColour (colours::a);
        g.fillRect (r.removeFromBottom (2.0f));
    }
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int height)
{
    return juce::FontOptions (juce::jmin (15.0f, height * 0.5f));
}

void Look::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.isEnabled() ? juce::Colour (0xff262e34) : juce::Colour (0xff22292e));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (0xff56616a).withAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 14.0f, cy = height * 0.5f;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (colours::sub.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (14.5f);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, colours::text.withAlpha (box.isEnabled() ? 1.0f : 0.45f));
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                             juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = y + height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, cy - 2.5f, (float) width, 5.0f);
    g.setColour (colours::line);
    g.fillRoundedRectangle (track, 2.5f);
    g.setColour (colours::a);
    g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

} // namespace phaselab::ui
