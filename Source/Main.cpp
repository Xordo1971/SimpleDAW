#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "Splash.h"

class SimpleDAWApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        splash = std::make_unique<SplashWindow> ("v" + getApplicationVersion());

        // Se deja que la animación arranque y luego se inicializa el audio y los plugins
        // (la ventana principal se crea oculta y aparece cuando se desvanece la pantalla de arranque)
        juce::Timer::callAfterDelay (700, [this]
        {
            if (splash == nullptr)
                return;
            mainWindow = std::make_unique<MainWindow> (getApplicationName(), false);
            splash->setStatus ("Listo");
            splash->finish ([this] { if (mainWindow != nullptr) mainWindow->showAndFocus(); },
                            [this]
                            {
                                splash = nullptr;
                                if (mainWindow != nullptr)
                                    if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                                    {
                                        content->checkForRecovery();
                                        content->showWhatsNewIfUpdated();
                                        const auto captures = juce::SystemStats::getEnvironmentVariable ("SIMPLEDAW_CAPTURA", {});
                                        if (captures.isNotEmpty())
                                        {
                                            juce::Component::SafePointer<MainComponent> c (content);
                                            juce::Timer::callAfterDelay (1200, [c, captures]
                                            {
                                                if (c != nullptr) c->saveDiagnosticCaptures (juce::File (captures));
                                            });
                                        }
                                        content->checkForUpdates (false);
                                    }
                            });
        });
    }

    void shutdown() override
    {
        splash = nullptr;
        mainWindow = nullptr;
    }
    void systemRequestedQuit() override
    {
        // Pregunta si hay cambios sin guardar antes de salir
        if (mainWindow != nullptr)
            if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                content->requestQuit();
                return;
            }
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, bool showNow)
            : DocumentWindow (name, juce::Colour (0xff1b1d22), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);

           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (content);   // menús en la barra superior de macOS
           #else
            setMenuBar (content);                           // menús dentro de la ventana en Windows
           #endif
            setResizable (true, true);
            setResizeLimits (1340, 600, 10000, 10000);
            // Tamaño inicial amplio (1810 x 1035), sin pasarse de la pantalla
            int w = getWidth(), h = getHeight();
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                w = juce::jmin (w, display->userArea.getWidth() - 20);
                h = juce::jmin (h, display->userArea.getHeight() - 40);
            }
            centreWithSize (juce::jmax (1340, w), juce::jmax (600, h));
            if (showNow)
                showAndFocus();
        }

        void showAndFocus()
        {
            setVisible (true);
            toFront (true);
            if (auto* content = getContentComponent())
                content->grabKeyboardFocus();   // para que funcionen Espacio y R desde el principio
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SplashWindow> splash;
};

START_JUCE_APPLICATION (SimpleDAWApplication)
