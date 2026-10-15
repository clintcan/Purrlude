#include "ControlChooser.h"

namespace purrlude
{

namespace
{
    class Chooser final : public juce::Component,
                          private juce::ListBoxModel
    {
    public:
        Chooser (const automation::PluginControls& p, std::function<void (const automation::Target&)> callback)
            : plugin (p), onChosen (std::move (callback))
        {
            title.setText ("Automate a control of " + plugin.name, juce::dontSendNotification);
            title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            addAndMakeVisible (title);

            search.setTextToShowWhenEmpty ("Search " + juce::String ((int) plugin.controls.size()) + " controls",
                                           juce::Colours::grey);
            search.setFont (juce::FontOptions (14.0f));
            search.onTextChange = [this] { filter(); };
            search.onReturnKey = [this] { choose (list.getSelectedRow()); };
            addAndMakeVisible (search);

            list.setModel (this);
            list.setRowHeight (22);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
            addAndMakeVisible (list);

            filter();
            setSize (360, 420);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            title.setBounds (r.removeFromTop (24));
            search.setBounds (r.removeFromTop (28));
            r.removeFromTop (6);
            list.setBounds (r);
        }

        void visibilityChanged() override
        {
            if (isShowing())
                search.grabKeyboardFocus();
        }

    private:
        void filter()
        {
            auto words = juce::StringArray::fromTokens (search.getText(), true);
            shown.clear();

            for (size_t i = 0; i < plugin.controls.size(); ++i)
            {
                auto matches = true;

                for (auto& word : words)
                    matches = matches && plugin.controls[i].shortName.containsIgnoreCase (word);

                if (matches)
                    shown.push_back (i);
            }

            list.updateContent();
            list.selectRow (0);
            list.repaint();
        }

        void choose (int row)
        {
            if (! juce::isPositiveAndBelow (row, (int) shown.size()))
                return;

            auto target = plugin.controls[shown[(size_t) row]];
            auto callback = onChosen;

            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();

            callback (target);
        }

        int getNumRows() override       { return (int) shown.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, (int) shown.size()))
                return;

            if (selected)
                g.fillAll (juce::Colour (0xff0072b2));   // the app's blue

            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (plugin.controls[shown[(size_t) row]].shortName, 8, 0, width - 16, height, juce::Justification::centredLeft);
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override   { choose (row); }
        void returnKeyPressed (int row) override                                   { choose (row); }

        automation::PluginControls plugin;
        std::function<void (const automation::Target&)> onChosen;
        std::vector<size_t> shown;   ///< Indexes into plugin.controls that match the search

        juce::Label title;
        juce::TextEditor search;
        juce::ListBox list;
    };
}

void showControlChooser (juce::Rectangle<int> screenArea, const automation::PluginControls& plugin,
                         std::function<void (const automation::Target&)> onChosen)
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<Chooser> (plugin, std::move (onChosen)), screenArea, nullptr);
}

} // namespace purrlude
