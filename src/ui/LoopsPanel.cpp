#include "LoopsPanel.h"

namespace purrlude
{

namespace
{
    constexpr int margin = 12, rowHeight = 44;
    const juce::String defaultHint ("Double-click a loop, or choose one and Add to Song: it goes on the Drums track at the playhead.");
}

LoopsPanel::LoopsPanel (tracktion::Edit& e, LiveInstrument& l)
    : edit (e), live (l)
{
    heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    styleBox.addItemList (LoopLibrary::getStyles(), 1);
    styleBox.setTooltip ("Style");
    styleBox.onChange = [this] { showStyle (styleBox.getText()); };
    addAndMakeVisible (styleBox);

    loopList.setRowHeight (rowHeight);
    loopList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    loopList.setWantsKeyboardFocus (false);
    addAndMakeVisible (loopList);

    for (int bars : { 4, 8, 16, 32 })
        barsBox.addItem (juce::String (bars) + " bars", bars);

    barsBox.setSelectedId (8, juce::dontSendNotification);
    barsBox.setTooltip ("How long the loop plays for: it repeats to fill this many bars");
    addAndMakeVisible (barsLabel);
    addAndMakeVisible (barsBox);

    addButton.onClick = [this] { addSelectedLoop(); };
    addButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (addButton);

    hint.setText (defaultHint, juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    // The license (CC BY 4.0) requires these credits to be shown.
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

    if (styleBox.getNumItems() > 0)
        styleBox.setSelectedItemIndex (0);
}

LoopsPanel::~LoopsPanel()
{
    loopList.setModel (nullptr);
}

//==============================================================================
void LoopsPanel::showStyle (const juce::String& style)
{
    shownLoops.clear();

    for (const auto& loop : LoopLibrary::getAll())
        if (loop.style == style)
            shownLoops.push_back (&loop);

    loopList.updateContent();
    loopList.selectRow (0);
    loopList.repaint();
}

const Loop* LoopsPanel::getSelectedLoop() const
{
    auto row = loopList.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) shownLoops.size()) ? shownLoops[(size_t) row] : nullptr;
}

void LoopsPanel::selectedRowsChanged (int)
{
    addButton.setEnabled (getSelectedLoop() != nullptr);
}

void LoopsPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    loopList.selectRow (row);
    addSelectedLoop();
}

void LoopsPanel::addSelectedLoop()
{
    auto* loop = getSelectedLoop();

    if (loop == nullptr)
        return;

    // At the start of the bar the playhead is in.
    auto bar = juce::jmax (0, edit.tempoSequence.toBarsAndBeats (edit.getTransport().getPosition()).bars);

    if (LoopLibrary::insertLoop (edit, live, *loop, bar, barsBox.getSelectedId()) != nullptr)
        hint.setText ("Added " + loop->name + " at bar " + juce::String (bar + 1) + ". Cmd+Z to undo.", juce::dontSendNotification);
}

//==============================================================================
void LoopsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shownLoops.size()))
        return;

    const auto& loop = *shownLoops[(size_t) row];
    auto r = juce::Rectangle<int> (width, height).reduced (2, 1);

    if (selected)
    {
        g.setColour (findColour (juce::TextButton::buttonOnColourId).withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    r.reduce (8, 4);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (loop.name, r.removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (loop.description, r, juce::Justification::centredLeft);
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
    styleBox.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);

    // Credits at the bottom, then the add controls above them; the list takes the rest.
    creditLink.setBounds (r.removeFromBottom (creditLink.isVisible() ? 20 : 0));
    creditLabel.setBounds (r.removeFromBottom (64));
    r.removeFromBottom (8);
    hint.setBounds (r.removeFromBottom (40));

    auto addRow = r.removeFromBottom (30);
    barsLabel.setBounds (addRow.removeFromLeft (56));
    barsBox.setBounds (addRow.removeFromLeft (100));
    addRow.removeFromLeft (8);
    addButton.setBounds (addRow);
    r.removeFromBottom (8);

    loopList.setBounds (r);
}

} // namespace purrlude
