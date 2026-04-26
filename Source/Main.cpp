#include "ShurikenHeaders.h"
#include "UI/MainComponent.h"
#include "UI/LookAndFeel.h"

//==============================================================================
class BoShurikenApplication final : public juce::JUCEApplication
{
public:
    BoShurikenApplication() = default;

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise(const juce::String& commandLine) override
    {
        // ── Parse command line ────────────────────────────────────────────────
        // Usage:  Bo-Shuriken [options] [audiofile]
        //
        // Options:
        //   -h, --help        Print usage and exit
        //   -v, --version     Print version and exit
        //
        // Any non-option argument is treated as an audio file to load.

        auto tokens = juce::StringArray::fromTokens(commandLine, true);

        for (const auto& t : tokens)
        {
            if (t == "-h" || t == "--help")
            {
                juce::Logger::writeToLog(
                    "Usage: Bo-Shuriken [options] [audiofile]\n"
                    "\n"
                    "Options:\n"
                    "  -h, --help       Show this help\n"
                    "  -v, --version    Show version\n"
                    "\n"
                    "Arguments:\n"
                    "  audiofile        Audio file to load on startup\n"
                    "                   Supported: WAV, AIFF, FLAC, OGG, MP3, ...\n");
                // Print to stdout as well
                std::cout
                    << "Usage: Bo-Shuriken [options] [audiofile]\n\n"
                    << "Options:\n"
                    << "  -h, --help       Show this help\n"
                    << "  -v, --version    Show version\n\n"
                    << "Arguments:\n"
                    << "  audiofile        Audio file to load on startup\n";
                quit();
                return;
            }

            if (t == "-v" || t == "--version")
            {
                std::cout << getApplicationName() << " " << getApplicationVersion() << "\n";
                quit();
                return;
            }

            // Non-option: treat as file path
            if (!t.startsWith("-"))
            {
                fileToLoad = juce::File::getCurrentWorkingDirectory().getChildFile(t);
                if (!fileToLoad.existsAsFile())
                    fileToLoad = juce::File(t);   // try as absolute path
            }
        }

        // ── Create window ─────────────────────────────────────────────────────
        lookAndFeel = std::make_unique<BoShurikenLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow>("Bo-Shuriken Beat Slicer");

        // Load file after the message loop starts so the UI is fully ready
        if (fileToLoad.existsAsFile())
        {
            juce::MessageManager::callAsync([this]
            {
                if (auto* mc = dynamic_cast<MainComponent*>(
                        mainWindow->getContentComponent()))
                    mc->loadFile(fileToLoad);
            });
        }
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override { quit(); }

    // Called when a second instance is launched — load the file it was given
    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        auto tokens = juce::StringArray::fromTokens(commandLine, true);
        for (const auto& t : tokens)
        {
            if (t.startsWith("-")) continue;
            juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(t);
            if (!f.existsAsFile()) f = juce::File(t);
            if (f.existsAsFile())
            {
                if (auto* mc = dynamic_cast<MainComponent*>(
                        mainWindow->getContentComponent()))
                    mc->loadFile(f);
                return;
            }
        }
    }

    //==========================================================================
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff1a1a2e),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
            setResizeLimits(800, 500, 3840, 2160);
            centreWithSize(getWidth(), getHeight());
#endif
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<BoShurikenLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow>          mainWindow;
    juce::File                           fileToLoad;
};

//==============================================================================
START_JUCE_APPLICATION(BoShurikenApplication)
