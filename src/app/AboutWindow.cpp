#include "AboutWindow.h"

#include "Splash.h"

#include <PurrludeBranding.h>

namespace purrlude::about
{

namespace
{
    constexpr const char* author = "Clint Christopher Canada";
    constexpr const char* website = "https://github.com/clintcan/Purrlude";

    /** The acknowledgements, laid out once for the width (a read-only TextEditor took
        seconds to lay out this much wrapped text). Section headings are bold.
    */
    class Acknowledgements final : public juce::Component
    {
    public:
        Acknowledgements()
        {
            auto text = juce::String::createStringFromData (PurrludeBranding::ACKNOWLEDGEMENTS_txt,
                                                            PurrludeBranding::ACKNOWLEDGEMENTS_txtSize);
            auto lines = juce::StringArray::fromLines (text.trimEnd());

            for (int i = 0; i < lines.size(); ++i)
            {
                auto line = lines[i];
                // A section heading: in capitals, with a blank line after it ("JUCE 8" is a name).
                auto heading = line.isNotEmpty() && line == line.toUpperCase() && lines[i + 1].isEmpty();
                attributed.append (line + (i + 1 < lines.size() ? "\n" : ""),
                              juce::FontOptions (heading ? 13.5f : 13.0f, heading ? juce::Font::bold : juce::Font::plain),
                              juce::Colours::white.withAlpha (heading ? 0.95f : 0.8f));
            }

            attributed.setWordWrap (juce::AttributedString::byWord);
        }

        /** Lays the text out for a width, and takes the height it needs. */
        void layOut (int width)
        {
            layout.createLayout (attributed, (float) (width - 2 * margin));
            setSize (width, (int) std::ceil (layout.getHeight()) + 2 * margin);
        }

        void paint (juce::Graphics& g) override
        {
            layout.draw (g, getLocalBounds().reduced (margin).toFloat());
        }

    private:
        static constexpr int margin = 10;
        juce::AttributedString attributed;
        juce::TextLayout layout;
    };

    class Content final : public juce::Component
    {
    public:
        explicit Content (const juce::String& version)
            : logo (Splash::getLogo())
        {
            auto label = [this] (juce::Label& l, const juce::String& text, float size, bool bold, float alpha)
            {
                l.setText (text, juce::dontSendNotification);
                l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
                l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (alpha));
                l.setJustificationType (juce::Justification::topLeft);
                l.setBorderSize ({});
                l.setMinimumHorizontalScale (1.0f);   // wrap rather than squeeze
                addAndMakeVisible (l);
            };

            label (title, "Purrlude", 28.0f, true, 1.0f);
            label (versionLabel, "Version " + version, 14.0f, false, 0.7f);
            label (madeBy, juce::String ("Made by ") + author, 15.0f, false, 0.95f);
            label (copyright, juce::String::fromUTF8 ("\xc2\xa9 2026 ") + author, 13.0f, false, 0.7f);
            label (licence, "Free software under the GNU Affero General Public License v3. "
                            "It comes with no warranty.", 13.0f, false, 0.7f);
            label (thanksTitle, "Acknowledgements", 15.0f, true, 0.95f);

            link.setButtonText ("github.com/clintcan/Purrlude");
            link.setURL (juce::URL (website));
            link.setFont (juce::FontOptions (14.0f), false, juce::Justification::left);
            link.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff56b4e9));   // sky blue
            addAndMakeVisible (link);

            thanksView.setViewedComponent (&thanks, false);
            thanksView.setScrollBarsShown (true, false);
            thanksView.setScrollBarThickness (10);
            addAndMakeVisible (thanksView);

            licenseFiles.setTooltip ("Show the full license texts that come with Purrlude");
            licenseFiles.onClick = [] { getLicensesFolder().revealToUser(); };
            licenseFiles.setEnabled (getLicensesFolder().isDirectory());
            addAndMakeVisible (licenseFiles);

            closeButton.onClick = [this]
            {
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
            };
            addAndMakeVisible (closeButton);

            setSize (600, 640);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Splash::getBackgroundColour());
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::centred);

            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRect (thanksView.getBounds());
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (20);
            auto top = r.removeFromTop (180);
            logoArea = top.removeFromLeft (180);
            top.removeFromLeft (20);
            top.removeFromTop (14);

            title.setBounds (top.removeFromTop (36));
            versionLabel.setBounds (top.removeFromTop (24));
            madeBy.setBounds (top.removeFromTop (24));
            copyright.setBounds (top.removeFromTop (20));
            licence.setBounds (top.removeFromTop (36));
            link.setBounds (top.removeFromTop (22).removeFromLeft (260));

            auto buttons = r.removeFromBottom (32);
            closeButton.setBounds (buttons.removeFromRight (90));
            licenseFiles.setBounds (buttons.removeFromLeft (150));
            r.removeFromBottom (12);

            r.removeFromTop (12);
            thanksTitle.setBounds (r.removeFromTop (24));
            thanksView.setBounds (r);
            thanks.layOut (r.getWidth() - thanksView.getScrollBarThickness());
        }

    private:
        juce::Image logo;
        juce::Rectangle<int> logoArea;
        juce::Label title, versionLabel, madeBy, copyright, licence, thanksTitle;
        juce::HyperlinkButton link;
        Acknowledgements thanks;   // before the viewport that shows it
        juce::Viewport thanksView;
        juce::TextButton licenseFiles { "License Files..." }, closeButton { "Close" };
    };

    juce::Component::SafePointer<juce::DialogWindow> window;
}

void show (const juce::String& version)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions launch;
    launch.content.setOwned (new Content (version));
    launch.dialogTitle = "About Purrlude";
    launch.dialogBackgroundColour = Splash::getBackgroundColour();
    launch.escapeKeyTriggersCloseButton = true;
    launch.useNativeTitleBar = true;
    launch.resizable = false;
    window = launch.launchAsync();
}

void close()
{
    delete window.getComponent();
}

juce::File getLicensesFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/licenses");
   #else
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("licenses");
   #endif
}

} // namespace purrlude::about
