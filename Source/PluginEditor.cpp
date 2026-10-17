#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
    // The analog-recreation / drum-gate / 1176 language: flat, dark, no borders. Pale discs are
    // the things you turn; white and pale type is what is selected; the quieter grey names things.
    namespace Theme
    {
        const juce::Colour background { 0xff141617 }, panel { 0xff212427 }, text { 0xffffffff },
                           label { 0xffaab0b6 }, accent { 0xffd9483b }, pale { 0xffe6e9ec },
                           clipping { 0xffff5a5a }, field { 0xff3c4045 };

        // The VU face: the panel grey, a shade warmer; its hood is the plain panel grey.
        const juce::Colour face { 0xff2b2a25 }, hood = panel;
        // Its red zone, quieter than the clipping red it comes from.
        const juce::Colour redZone = clipping.withMultipliedSaturation (0.7f).withAlpha (0.7f);

        juce::Font make (const juce::Typeface::Ptr& typeface, float height, float tracking)
        {
            return juce::Font (juce::FontOptions (typeface).withHeight (height)).withExtraKerningFactor (tracking);
        }

        /** Labels are upper-case and tracked out; pass upper-case text. */
        juce::Font labelFont (float height = 11.0f)  { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.09f); }
        juce::Font titleFont (float height)          { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.18f); }
        juce::Font bodyFont (float height = 12.0f)   { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.01f); }
        juce::Font valueFont (float height = 12.0f)  { return make (juce::SharedResourcePointer<EditorFonts>()->mono, height, 0.0f); }

        int widthOf (const juce::Font& font, const juce::String& s)
        {
            return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, s)));
        }
    }

    // Spacing grammar of an option row: sibling options closest, the label further from its
    // first option.
    constexpr int kOptionGap = 10, kLabelGap = 18, kRowHeight = 18, kRowGap = 2;

    // The VU scale of the sibling 1176, which this meter shares.
    constexpr float kMeterMinDb = -20.0f, kMeterMaxDb = 3.0f;

    const juce::Identifier helpId ("help"), helpTitleId ("helpTitle"), editorScaleId ("editorScale");

    constexpr auto kDefaultHelpTitle = "LAAA";
    constexpr auto kDefaultHelp =
        "A Teletronix LA-2A optical leveling amplifier: a T4 photocell attenuator between tube gain stages, "
        "schematic-derived and calibrated rather than a measured unit. Hover over a control to see what it does; "
        "double-click a knob for its default, or its value to type one.";

    void setHelp (juce::Component& c, const juce::String& title, const juce::String& help)
    {
        c.getProperties().set (helpTitleId, title.toUpperCase());
        c.getProperties().set (helpId, help);
    }

    juce::String minusSign() { return juce::String::fromUTF8 ("\xE2\x88\x92"); }
    juce::String infinity()  { return minusSign() + juce::String::fromUTF8 ("\xE2\x88\x9E"); }
}

//==============================================================================
EditorFonts::EditorFonts()
    : inter (juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize)),
      mono (juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize))
{
}

//==============================================================================
void FlatLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const float angle = startAngle + position * (endAngle - startAngle);
    const float dot = juce::jmax (2.5f, radius * 0.11f);

    g.setColour (slider.isMouseOverOrDragging() ? Theme::pale.brighter (0.1f) : Theme::pale);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    g.setColour (Theme::background);
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (centre.getPointOnCircumference (radius * 0.72f, angle)));
}

void FlatLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Three short diagonals in the label grey, quiet until touched.
    g.setColour (Theme::label.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.3f));
    const float size = (float) juce::jmin (w, h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w - size * i, (float) h - 2.0f, (float) w - 2.0f, (float) h - size * i, 1.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& nameToUse, const juce::String& help,
            int diameterToUse, float sweepDegrees)
    : name (nameToUse.toUpperCase()), diameter (diameterToUse), attachment (*state.getParameter (paramId), slider)
{
    const float halfSweep = juce::degreesToRadians (sweepDegrees * 0.5f), twoPi = juce::MathConstants<float>::twoPi;
    slider.setRotaryParameters (twoPi - halfSweep, twoPi + halfSweep, true);
    slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
    slider.setDoubleClickReturnValue (true, state.getParameter (paramId)->convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    slider.setName (nameToUse);
    // The dials run 0 to 100, as on the hardware-style panel.
    slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    slider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue(); };
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    editor.setJustification (juce::Justification::centred);
    editor.setFont (Theme::valueFont (12.0f));
    editor.setIndents (0, 0);
    editor.setColour (juce::TextEditor::backgroundColourId, Theme::field);
    editor.setColour (juce::TextEditor::textColourId, Theme::pale);
    editor.setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.5f));
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.onReturnKey = [this] { finishEditing (true); };
    editor.onEscapeKey = [this] { finishEditing (false); };
    editor.onFocusLost = [this] { finishEditing (true); };
    addChildComponent (editor);

    setHelp (*this, name, help);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kLabelHeight + kGap - 1);
    slider.setBounds (area.removeFromTop (diameter + 2).withSizeKeepingCentre (diameter + 2, diameter + 2));
    editor.setBounds (getLocalBounds().removeFromBottom (kLabelHeight).withSizeKeepingCentre (64, kLabelHeight));
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (Theme::label);
    g.setFont (Theme::labelFont());
    g.drawText (name, area.removeFromTop (kLabelHeight), juce::Justification::centred, false);

    g.setFont (Theme::valueFont (12.0f));
    g.drawText (slider.getTextFromValue (slider.getValue()), area.removeFromBottom (kLabelHeight), juce::Justification::centred, false);
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < getHeight() - kLabelHeight - 2)
        return;

    editor.setText (slider.getTextFromValue (slider.getValue()), false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
}

void Knob::finishEditing (bool apply)
{
    if (! editor.isVisible())
        return;

    editor.setVisible (false);
    const auto text = editor.getText().trim();

    // Only text that names a value: anything else is ignored, never zeroed.
    if (apply && text.containsAnyOf ("0123456789"))
        slider.setValue (slider.getValueFromText (text), juce::sendNotificationSync);
    repaint();
}

//==============================================================================
OptionRow::OptionRow (juce::RangedAudioParameter& param, const juce::String& labelToUse, juce::StringArray optionsToUse,
                      const juce::String& help)
    : label (labelToUse.toUpperCase()), options (std::move (optionsToUse)),
      attachment (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    labelColumn = labelTextWidth() + kLabelGap;
    attachment.sendInitialUpdate();
    setHelp (*this, label, help);
}

int OptionRow::labelTextWidth() const
{
    return Theme::widthOf (Theme::labelFont(), label);
}

std::vector<juce::Range<int>> OptionRow::optionRanges() const
{
    std::vector<juce::Range<int>> ranges;
    const auto font = Theme::labelFont();
    int x = labelColumn;

    for (const auto& option : options)
    {
        const int width = Theme::widthOf (font, option.toUpperCase());
        ranges.push_back ({ x, x + width });
        x += width + kOptionGap;
    }
    return ranges;
}

int OptionRow::preferredWidth() const
{
    const auto ranges = optionRanges();
    return ranges.empty() ? labelColumn : ranges.back().getEnd();
}

int OptionRow::optionAt (int x) const
{
    const auto ranges = optionRanges();
    for (int i = 0; i < (int) ranges.size(); ++i)
        if (ranges[(size_t) i].expanded (kOptionGap / 2).contains (x))
            return i;
    return -1;
}

void OptionRow::paint (juce::Graphics& g)
{
    g.setFont (Theme::labelFont());
    g.setColour (Theme::label);
    g.drawText (label, 0, 0, labelColumn, getHeight(), juce::Justification::centredLeft, false);

    const auto ranges = optionRanges();
    for (int i = 0; i < options.size(); ++i)
    {
        const auto range = ranges[(size_t) i];
        g.setColour (i == selected ? Theme::pale : (i == hovered ? Theme::text : Theme::label.withAlpha (0.7f)));
        g.drawText (options[i].toUpperCase(), range.getStart(), 0, range.getLength() + 4, getHeight(),
                    juce::Justification::centredLeft, false);
    }
}

void OptionRow::mouseDown (const juce::MouseEvent& e)
{
    const int option = optionAt (e.x);
    if (option >= 0)
        attachment.setValueAsCompleteGesture ((float) option);
}

void OptionRow::mouseMove (const juce::MouseEvent& e)   { hovered = optionAt (e.x); repaint(); }
void OptionRow::mouseExit (const juce::MouseEvent&)     { hovered = -1; repaint(); }

//==============================================================================
namespace
{
    // The VU scale: deflection is proportional to the rectified voltage, +3 VU at full scale.
    // The -20 mark is the left end here (the meter law clamps the reading there).
    constexpr float kHalfSweep = 0.85f;   // radians either side of 12 o'clock
    constexpr float kArcTop = 25.0f;      // from the top of the face to the top of the arc
    constexpr float kHoodHeight = 19.0f, kCorner = 4.0f;
    constexpr float kMarks[] { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
}

VuMeter::VuMeter()
{
    setHelp (*this, "Meter",
             "A VU movement (300 ms to settle). On GR the needle rests at 0 and falls to the left as the optical cell "
             "takes gain away; on +4 / +10 it shows the output level, 0 VU = +4 or +10 dBu. In the hood: the "
             "gain reduction in dB. IN and OUT below: the level at the input and output, in dBFS (the louder channel).");
}

float VuMeter::angleFor (float db) noexcept
{
    const auto deflection = [] (float d) { return std::pow (10.0f, d / 20.0f); };
    const float lo = deflection (kMeterMinDb), hi = deflection (kMeterMaxDb);
    const float clamped = juce::jlimit (kMeterMinDb, kMeterMaxDb, db);
    return kHalfSweep * (2.0f * (deflection (clamped) - lo) / (hi - lo) - 1.0f);
}

juce::Point<float> VuMeter::pivot() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getCentreX(), r.getY() + kArcTop + radius() };
}

float VuMeter::radius() const
{
    return (float) getWidth() * 0.42f / std::sin (kHalfSweep);
}

void VuMeter::setReading (float newNeedleDb, float newGrDb)
{
    const bool needleMoved = std::abs (angleFor (newNeedleDb) - angleFor (needleDb)) > 0.0005f;
    const bool readoutChanged = juce::String (newGrDb, 1) != juce::String (grDb, 1);

    if (needleMoved || readoutChanged)
    {
        needleDb = newNeedleDb;
        grDb = newGrDb;
        repaint();
    }
}

void VuMeter::paint (juce::Graphics& g)
{
    // The face, and at its foot the hood over the needle's pivot, which carries the readout.
    const auto faceArea = getLocalBounds().toFloat();
    const auto hood = faceArea.withTop (faceArea.getBottom() - kHoodHeight);
    g.setColour (Theme::face);
    g.fillRoundedRectangle (faceArea, kCorner);
    juce::Path hoodShape;
    hoodShape.addRoundedRectangle (hood.getX(), hood.getY(), hood.getWidth(), hood.getHeight(), kCorner, kCorner, false, false, true, true);
    g.setColour (Theme::hood);
    g.fillPath (hoodShape);

    const auto centre = pivot();
    const float r = radius();

    // The arc: a thin line to 0, the red zone from 0 to +3.
    juce::Path line, red;
    line.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (kMeterMinDb), angleFor (0.0f), true);
    red.addCentredArc (centre.x, centre.y, r + 1.0f, r + 1.0f, 0.0f, angleFor (0.0f), angleFor (kMeterMaxDb), true);
    g.setColour (Theme::label.withAlpha (0.7f));
    g.strokePath (line, juce::PathStrokeType (1.0f));
    g.setColour (Theme::redZone);
    g.strokePath (red, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    // Marks and figures.
    g.setFont (Theme::valueFont (8.0f));
    for (const float mark : kMarks)
    {
        const float a = angleFor (mark);
        const bool plus = mark > 0.0f;
        g.setColour (plus ? Theme::redZone : Theme::label);
        g.drawLine ({ centre.getPointOnCircumference (r, a), centre.getPointOnCircumference (r + 5.0f, a) }, mark == 0.0f ? 1.5f : 1.0f);

        const auto text = mark < 0.0f ? minusSign() + juce::String ((int) -mark)
                                      : (plus ? "+" : "") + juce::String ((int) mark);
        const auto at = centre.getPointOnCircumference (r + 11.0f, a);
        g.drawText (text, juce::Rectangle<float> (22.0f, 10.0f).withCentre (at), juce::Justification::centred, false);
    }

    g.setColour (Theme::label.withAlpha (0.5f));
    g.setFont (Theme::titleFont (10.0f));
    g.drawText ("VU", juce::Rectangle<float> (faceArea.getX(), centre.y - r + 35.0f, faceArea.getWidth(), 12.0f),
                juce::Justification::centred, false);

    // The gain reduction, as a number.
    auto readout = hood.reduced (8.0f, 0.0f);
    g.setColour (Theme::label);
    g.setFont (Theme::labelFont (8.0f));
    g.drawText ("GR", readout.removeFromLeft (18.0f), juce::Justification::centredLeft, false);
    g.setFont (Theme::valueFont (9.0f));
    g.drawText ((grDb >= 0.05f ? minusSign() : juce::String()) + juce::String (grDb, 1) + " dB", readout,
                juce::Justification::centredLeft, false);

    // The needle, from its pivot under the hood.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (faceArea.withBottom (hood.getY()).toNearestInt());
    g.setColour (Theme::pale);
    g.drawLine ({ centre, centre.getPointOnCircumference (r + 2.0f, angleFor (needleDb)) }, 1.25f);
}

//==============================================================================
EditorView::EditorView (La2aProcessor& p)
    : processor (p),
      gain (p.apvts, ParamID::gain, "Gain",
            "The output make-up gain, after the T4 optical attenuator and the tube stages. The dial runs 0 to 100; "
            "around 20 is about unity gain with Peak Reduction at 0. It sets the level, not how much is compressed.",
            64),
      peakReduction (p.apvts, ParamID::peakReduction, "Peak Reduction",
                     "How hard the sidechain drives the T4 optical cell: higher settings reach compression at a lower input "
                     "level and take more gain away. The dial runs 0 to 100, as on the hardware.",
                     64),
      emphasis (p.apvts, ParamID::emphasis, "R37 Emphasis",
                "Sidechain emphasis (R37 / C12): it tilts what the detector hears toward the high frequencies, so bass "
                "drives less of the reduction. 0 is the flat, factory setting. It does not filter the audio.",
                48),
      modeRow (*p.apvts.getParameter (ParamID::compressLimit), "Mode", { "Compress", "Limit" },
               "COMPRESS: the gentler LA-2A curve, with a soft knee and a low ratio. LIMIT: the steeper limiting "
               "characteristic, which holds peaks down harder."),
      meterRow (*p.apvts.getParameter (ParamID::meter), "Meter", { "GR", "+4", "+10" },
                "What the needle shows; it never changes the sound. GR: gain reduction, resting at 0 and falling to the left. "
                "+4 / +10: the output level, with 0 VU at +4 or +10 dBu. The hood always shows the gain reduction."),
      stereoRow (*p.apvts.getParameter (ParamID::stereoLink), "Stereo", { "Indep", "Link" },
                 "LINK: both channels share one detector, so the image holds steady and both are reduced by the same "
                 "amount. INDEP: each channel follows its own level."),
      bypassRow (*p.apvts.getParameter (ParamID::bypass), "Bypass", { "Off", "On" },
                 "ON passes the signal through unprocessed, with no compression and none of the tube or transformer "
                 "colour. OFF is the processed signal.")
{
    const std::pair<juce::Component*, const char*> controls[] {
        { &gain, ParamID::gain }, { &peakReduction, ParamID::peakReduction }, { &emphasis, ParamID::emphasis },
        { &modeRow, ParamID::compressLimit }, { &meterRow, ParamID::meter }, { &stereoRow, ParamID::stereoLink },
        { &bypassRow, ParamID::bypass }
    };
    for (const auto& [component, id] : controls)
    {
        component->setComponentID (id);
        addAndMakeVisible (component);
    }
    addAndMakeVisible (vu);

    // The two rows share a label column in each of their two groups, so the options line up.
    const int firstColumn = juce::jmax (modeRow.labelTextWidth(), stereoRow.labelTextWidth()) + kLabelGap;
    const int secondColumn = juce::jmax (meterRow.labelTextWidth(), bypassRow.labelTextWidth()) + kLabelGap;
    modeRow.setLabelColumn (firstColumn);
    stereoRow.setLabelColumn (firstColumn);
    meterRow.setLabelColumn (secondColumn);
    bypassRow.setLabelColumn (secondColumn);

    showHelpFor (nullptr);
    updateMeters();
    setSize (kWidth, kHeight);
}

void EditorView::updateMeters()
{
    const auto meters = processor.getMeters();

    // The readout rises at once and falls at about 20 dB/s, so it can be read.
    shownGrDb = juce::jmax (meters.gainReductionDb, shownGrDb - 0.7f);
    vu.setReading (meters.needle, shownGrDb);

    const float in = juce::jmax (meters.inputDb[0], meters.inputDb[1]), out = juce::jmax (meters.outputDb[0], meters.outputDb[1]);
    if (juce::String (in, 1) != juce::String (inputDb, 1) || juce::String (out, 1) != juce::String (outputDb, 1))
    {
        inputDb = in;
        outputDb = out;
        repaint (levelArea);
    }
}

void EditorView::showHelpFor (juce::Component* component)
{
    juce::String title = kDefaultHelpTitle, text = kDefaultHelp;

    // The help belongs to the nearest ancestor that has any (a knob's slider has none itself).
    for (auto* c = component; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (c->getProperties().contains (helpId))
        {
            title = c->getProperties()[helpTitleId].toString();
            text = c->getProperties()[helpId].toString();
            break;
        }
    }

    if (title != helpTitle || text != helpText)
    {
        helpTitle = title;
        helpText = text;
        repaint (helpArea);
    }
}

void EditorView::showHelpForParameter (const juce::String& paramId)
{
    for (auto* child : getChildren())
        if (child->getComponentID() == paramId)
            showHelpFor (child);
}

void EditorView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Title: the model, and quieter, what it is.
    const auto title = Theme::titleFont (15.0f), subtitle = Theme::titleFont (11.0f);
    const float baseline = 33.0f;
    g.setColour (Theme::pale);
    g.setFont (title);
    g.drawSingleLineText ("LAAA", 24, (int) baseline);
    g.setColour (Theme::label);
    g.setFont (subtitle);
    g.drawSingleLineText ("OPTICAL LEVELING AMPLIFIER", 24 + Theme::widthOf (title, "LAAA") + 12, (int) baseline);

    // The levels, small and quiet under the meter: what goes in and what comes out.
    auto levels = levelArea;
    const auto drawLevel = [&] (const juce::String& name, float db, int width)
    {
        auto cell = levels.removeFromLeft (width);
        g.setColour (Theme::label);
        g.setFont (Theme::labelFont());
        g.drawText (name, cell.removeFromLeft (Theme::widthOf (Theme::labelFont(), name) + 8), juce::Justification::centredLeft, false);
        const bool silent = db <= -59.9f;
        g.setFont (silent ? Theme::bodyFont (14.0f) : Theme::valueFont (11.0f));
        g.drawText ((silent ? infinity() : (db < 0.0f ? minusSign() + juce::String (-db, 1) : juce::String (db, 1))) + " dBFS",
                    cell, juce::Justification::centredLeft, false);
    };
    drawLevel ("IN", inputDb, levelArea.getWidth() / 2);
    drawLevel ("OUT", outputDb, levelArea.getWidth() / 2);

    // Help, bottom left: what the control under the mouse does.
    auto help = helpArea;
    g.setColour (Theme::pale);
    g.setFont (Theme::labelFont());
    g.drawText (helpTitle, help.removeFromTop (16), juce::Justification::topLeft, false);
    g.setColour (Theme::label);
    g.setFont (Theme::bodyFont());
    g.drawFittedText (helpText, help, juce::Justification::topLeft, 3, 1.0f);
}

void EditorView::resized()
{
    constexpr int margin = 24, header = 50, knobCell = 120, faceWidth = 240, faceHeight = 112;
    constexpr int laneGap = 24, laneToKnobs = 4;

    auto area = getLocalBounds().reduced (margin, 0);
    area.removeFromTop (header);

    // Bottom: what the control under the mouse does.
    area.removeFromBottom (10);
    helpArea = area.removeFromBottom (54);

    // Two stacked rows of choices above the knobs they govern, centred over the knob bank. The
    // second group of each row starts at the same x.
    const int firstWidth = juce::jmax (modeRow.preferredWidth(), stereoRow.preferredWidth());
    const int secondWidth = juce::jmax (meterRow.preferredWidth(), bypassRow.preferredWidth());
    const int laneWidth = firstWidth + laneGap + secondWidth;
    const int laneX = area.getX() + (3 * knobCell - laneWidth) / 2;
    const auto place = [&] (OptionRow& first, OptionRow& second, int y)
    {
        first.setBounds (laneX, y, first.preferredWidth(), kRowHeight);
        second.setBounds (laneX + firstWidth + laneGap, y, second.preferredWidth(), kRowHeight);
    };
    place (modeRow, meterRow, area.getY());
    place (stereoRow, bypassRow, area.getY() + kRowHeight + kRowGap);
    area.removeFromTop (2 * kRowHeight + kRowGap + laneToKnobs);

    auto knobArea = area.removeFromLeft (3 * knobCell);
    const int rowHeight = gain.preferredHeight();
    knobArea.setHeight (rowHeight);
    for (auto* knob : { &gain, &peakReduction, &emphasis })
        knob->setBounds (knobArea.removeFromLeft (knobCell).withSizeKeepingCentre (knobCell, knob->preferredHeight()));

    // The meter takes the rest of the row, with the levels under it.
    auto meterColumn = area.withHeight (rowHeight + 20);
    meterColumn = meterColumn.removeFromRight (faceWidth);
    vu.setBounds (meterColumn.removeFromTop (rowHeight).withSizeKeepingCentre (faceWidth, faceHeight));
    levelArea = meterColumn.removeFromTop (20).withSizeKeepingCentre (faceWidth, 16);
}

//==============================================================================
La2aEditor::La2aEditor (La2aProcessor& p)
    : AudioProcessorEditor (p), owner (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // Scales as a whole, 0.75x to 2x, keeping the design's aspect ratio.
    constexpr int w = EditorView::kWidth, h = EditorView::kHeight;
    setResizable (true, true);
    setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
    getConstrainer()->setFixedAspectRatio ((double) w / (double) h);

    const double scale = juce::jlimit (0.75, 2.0, (double) p.apvts.state.getProperty (editorScaleId, 1.0));
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    startTimerHz (30);
}

La2aEditor::~La2aEditor()
{
    setLookAndFeel (nullptr);
}

void La2aEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void La2aEditor::resized()
{
    const float scale = (float) getWidth() / (float) EditorView::kWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, EditorView::kWidth, EditorView::kHeight);

    // Remember a size the user chose (a window on screen), not the ones a host passes through
    // while opening or closing it.
    if (isShowing() && getPeer() != nullptr)
        owner.apvts.state.setProperty (editorScaleId, (double) scale, nullptr);
}

void La2aEditor::timerCallback()
{
    view.updateMeters();

    auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    view.showHelpFor (under != nullptr && view.isParentOf (under) ? under : nullptr);
}
