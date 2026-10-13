#include "LoopsPanel.h"

namespace purrlude
{

namespace
{
    constexpr int margin = 12, rowHeight = 44, keyIdBase = 1;   // key ids: 1 + tonic (+ 12 for minor)

    int keyToId (accompaniment::Key key)                { return keyIdBase + key.tonic + (key.minor ? 12 : 0); }
    accompaniment::Key idToKey (int id)                  { return { (id - keyIdBase) % 12, id - keyIdBase >= 12 }; }
}

LoopsPanel::LoopsPanel (tracktion::Edit& e, LiveInstrument& l)
    : edit (e), live (l)
{
    heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    // Drums / Bass / Chords.
    for (auto* button : { &drumsButton, &bassButton, &chordsButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (1);
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    drumsButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    bassButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    chordsButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    drumsButton.onClick = [this] { if (drumsButton.getToggleState()) setMode (Mode::drums); };
    bassButton.onClick = [this] { if (bassButton.getToggleState()) setMode (Mode::bass); };
    chordsButton.onClick = [this] { if (chordsButton.getToggleState()) setMode (Mode::chords); };

    styleBox.setTooltip ("Style");
    styleBox.onChange = [this] { showStyle (styleBox.getText()); };
    addAndMakeVisible (styleBox);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);

    // Key and progression, for bass and chords.
    for (int minor = 0; minor < 2; ++minor)
    {
        if (minor == 1)
            keyBox.addSeparator();

        for (int tonic = 0; tonic < 12; ++tonic)
        {
            accompaniment::Key key { tonic, minor == 1 };
            keyBox.addItem (accompaniment::getKeyName (key), keyToId (key));
        }
    }

    keyBox.setTooltip ("The key the chords are in. It follows what you've played until you choose one.");
    keyBox.onChange = [this]
    {
        keyChosen = true;
        updateProgressions();
    };

    progressionBox.setTooltip ("The chord progression");
    progressionBox.onChange = [this] { updateBars(); updateChordNames(); };

    chordNames.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    chordNames.setJustificationType (juce::Justification::topLeft);
    chordNames.setMinimumHorizontalScale (1.0f);

    for (auto* c : std::initializer_list<juce::Component*> { &keyLabel, &progressionLabel, &keyBox, &progressionBox, &chordNames })
        addChildComponent (c);

    barsBox.setTooltip ("How long it plays for: it repeats to fill this many bars");
    addAndMakeVisible (barsLabel);
    addAndMakeVisible (barsBox);

    addButton.onClick = [this] { addSelected(); };
    addButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (addButton);

    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    // The drum loops' license (CC BY 4.0) requires these credits to be shown.
    auto credits = LoopLibrary::getAttributions().joinIntoString ("\n");
    auto urlStart = credits.indexOf ("https://");

    creditLabel.setText (urlStart >= 0 ? credits.substring (0, urlStart).trimEnd() : credits, juce::dontSendNotification);
    creditLabel.setFont (juce::FontOptions (12.0f));
    creditLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    creditLabel.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (creditLabel);

    if (urlStart >= 0)
    {
        auto url = credits.substring (urlStart).upToFirstOccurrenceOf ("\n", false, false).trim();
        creditLink.setButtonText (url.fromFirstOccurrenceOf ("https://", false, false));
        creditLink.setURL (juce::URL (url));
        creditLink.setFont (juce::FontOptions (12.0f), false, juce::Justification::centredLeft);
        creditLink.setWantsKeyboardFocus (false);
        addAndMakeVisible (creditLink);
    }

    drumsButton.setToggleState (true, juce::dontSendNotification);
    setMode (Mode::drums);
}

LoopsPanel::~LoopsPanel()
{
    list.setModel (nullptr);
}

//==============================================================================
void LoopsPanel::setMode (Mode newMode)
{
    mode = newMode;
    auto accompanying = mode != Mode::drums;

    for (auto* c : std::initializer_list<juce::Component*> { &keyLabel, &progressionLabel, &keyBox, &progressionBox, &chordNames })
        c->setVisible (accompanying);

    creditLabel.setVisible (! accompanying);
    creditLink.setVisible (! accompanying && creditLink.getURL().isWellFormed());

    hint.setText (accompanying ? "Double-click a pattern, or choose one and Add to Song: it goes on its own track at the playhead."
                               : "Double-click a loop, or choose one and Add to Song: it goes on the Drums track at the playhead.",
                  juce::dontSendNotification);

    if (accompanying)
        updateKey();

    updateStyles();
    updateBars();
    resized();
}

void LoopsPanel::updateStyles()
{
    auto styles = mode == Mode::drums ? LoopLibrary::getStyles()
                                      : accompaniment::getStyles (mode == Mode::bass ? "bass" : "chords");

    // Keep the style when switching between parts, if the other has it.
    auto previous = styleBox.getText();
    styleBox.clear (juce::dontSendNotification);
    styleBox.addItemList (styles, 1);

    auto index = styles.indexOf (previous);
    styleBox.setSelectedItemIndex (juce::jmax (0, index), juce::dontSendNotification);
    showStyle (styleBox.getText());
}

void LoopsPanel::showStyle (const juce::String& style)
{
    items.clear();

    if (mode == Mode::drums)
    {
        for (const auto& loop : LoopLibrary::getAll())
            if (loop.style == style)
                items.push_back ({ loop.name, loop.description, &loop, nullptr });
    }
    else
    {
        auto part = mode == Mode::bass ? "bass" : "chords";

        for (const auto& pattern : accompaniment::getPatterns())
            if (pattern.part == part && pattern.style == style)
                items.push_back ({ pattern.name, pattern.trackName + " · " + pattern.description, nullptr, &pattern });
    }

    list.updateContent();
    list.selectRow (0);
    list.repaint();
}

void LoopsPanel::updateKey()
{
    if (! keyChosen)
        keyBox.setSelectedId (keyToId (accompaniment::detectKey (edit)), juce::dontSendNotification);

    keyLabel.setText (keyChosen ? "Key" : "Key (song)", juce::dontSendNotification);
    updateProgressions();
}

void LoopsPanel::updateProgressions()
{
    // Progressions written for this kind of key; the same one stays chosen if it still fits.
    auto key = getKey();
    auto previous = progressionBox.getSelectedId();
    progressionBox.clear (juce::dontSendNotification);

    const auto& progressions = accompaniment::getProgressions();

    for (size_t i = 0; i < progressions.size(); ++i)
        if (progressions[i].minor == key.minor)
            progressionBox.addItem (progressions[i].name + "  (" + progressions[i].description + ")", (int) i + 1);

    if (progressionBox.indexOfItemId (previous) >= 0)
        progressionBox.setSelectedId (previous, juce::dontSendNotification);
    else
        progressionBox.setSelectedItemIndex (0, juce::dontSendNotification);

    keyLabel.setText (keyChosen ? "Key" : "Key (song)", juce::dontSendNotification);
    updateBars();
    updateChordNames();
}

void LoopsPanel::updateBars()
{
    // Drum loops come in any number of bars; accompaniment in whole passes of the progression.
    auto previous = barsBox.getSelectedId();
    barsBox.clear (juce::dontSendNotification);

    if (mode == Mode::drums)
    {
        for (int bars : { 4, 8, 16, 32 })
            barsBox.addItem (juce::String (bars) + " bars", bars);
    }
    else if (auto* progression = getProgression())
    {
        auto length = (int) progression->bars.size();

        for (int passes : { 1, 2, 4 })
            barsBox.addItem (juce::String (length * passes) + " bars", length * passes);
    }

    if (barsBox.indexOfItemId (previous) >= 0)
        barsBox.setSelectedId (previous, juce::dontSendNotification);
    else
        barsBox.setSelectedItemIndex (juce::jmin (1, barsBox.getNumItems() - 1), juce::dontSendNotification);
}

void LoopsPanel::updateChordNames()
{
    juce::StringArray names;

    if (auto* progression = getProgression())
        for (const auto& c : accompaniment::realise (*progression, getKey()))
            names.add (c.chord.getName (getKey()));

    chordNames.setText (names.joinIntoString ("  "), juce::dontSendNotification);
}

void LoopsPanel::visibilityChanged()
{
    // The song may have changed key since the panel was last shown.
    if (isVisible() && mode != Mode::drums)
        updateKey();
}

//==============================================================================
const LoopsPanel::Item* LoopsPanel::getSelectedItem() const
{
    auto row = list.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) items.size()) ? &items[(size_t) row] : nullptr;
}

const accompaniment::Progression* LoopsPanel::getProgression() const
{
    auto index = progressionBox.getSelectedId() - 1;
    const auto& progressions = accompaniment::getProgressions();
    return juce::isPositiveAndBelow (index, (int) progressions.size()) ? &progressions[(size_t) index] : nullptr;
}

accompaniment::Key LoopsPanel::getKey() const
{
    return keyBox.getSelectedId() > 0 ? idToKey (keyBox.getSelectedId()) : accompaniment::Key {};
}

void LoopsPanel::selectedRowsChanged (int)
{
    addButton.setEnabled (getSelectedItem() != nullptr);
}

void LoopsPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    addSelected();
}

void LoopsPanel::addSelected()
{
    auto* item = getSelectedItem();

    if (item == nullptr)
        return;

    // At the start of the bar the playhead is in.
    auto bar = juce::jmax (0, edit.tempoSequence.toBarsAndBeats (edit.getTransport().getPosition()).bars);
    auto bars = barsBox.getSelectedId();
    tracktion::MidiClip* clip = nullptr;

    if (item->loop != nullptr)
        clip = LoopLibrary::insertLoop (edit, live, *item->loop, bar, bars);
    else if (auto* progression = getProgression(); item->pattern != nullptr && progression != nullptr)
        clip = accompaniment::insert (edit, live, *item->pattern, *progression, getKey(), bar, bars);

    if (clip != nullptr)
        hint.setText ("Added " + item->name + " at bar " + juce::String (bar + 1) + ". Cmd+Z to undo.", juce::dontSendNotification);
}

//==============================================================================
void LoopsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    const auto& item = items[(size_t) row];
    auto r = juce::Rectangle<int> (width, height).reduced (2, 1);

    if (selected)
    {
        g.setColour (findColour (juce::TextButton::buttonOnColourId).withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    r.reduce (8, 4);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (item.name, r.removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (item.description, r, juce::Justification::centredLeft);
}

void LoopsPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.06f));

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void LoopsPanel::resized()
{
    auto r = getLocalBounds().reduced (margin);

    heading.setBounds (r.removeFromTop (28));
    r.removeFromTop (4);

    auto modes = r.removeFromTop (28);
    auto third = modes.getWidth() / 3;
    drumsButton.setBounds (modes.removeFromLeft (third));
    bassButton.setBounds (modes.removeFromLeft (third));
    chordsButton.setBounds (modes);
    r.removeFromTop (8);

    styleBox.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);

    // From the bottom: credits (drums), hint, add controls, then key and chords (bass, chords).
    if (mode == Mode::drums)
    {
        creditLink.setBounds (r.removeFromBottom (creditLink.isVisible() ? 20 : 0));
        creditLabel.setBounds (r.removeFromBottom (64));
        r.removeFromBottom (8);
    }

    hint.setBounds (r.removeFromBottom (40));

    auto addRow = r.removeFromBottom (30);
    barsLabel.setBounds (addRow.removeFromLeft (56));
    barsBox.setBounds (addRow.removeFromLeft (100));
    addRow.removeFromLeft (8);
    addButton.setBounds (addRow);
    r.removeFromBottom (8);

    if (mode != Mode::drums)
    {
        chordNames.setBounds (r.removeFromBottom (24));

        auto progressionRow = r.removeFromBottom (28);
        progressionLabel.setBounds (progressionRow.removeFromLeft (80));
        progressionBox.setBounds (progressionRow);
        r.removeFromBottom (4);

        auto keyRow = r.removeFromBottom (28);
        keyLabel.setBounds (keyRow.removeFromLeft (80));
        keyBox.setBounds (keyRow);
        r.removeFromBottom (8);
    }

    list.setBounds (r);
}

} // namespace purrlude
