#pragma once
// Pequeño medidor de recursos para la barra superior, al estilo de los vúmetros de LED:
//   CPU: carga del procesado de audio (si llega al 100 % habrá cortes y chasquidos)
//   RAM: memoria que está usando SimpleDAW
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

#if JUCE_MAC
 #include <mach/mach.h>
#elif JUCE_WINDOWS
 // Declaradas a mano para no meter <windows.h> (choca con nombres de JUCE)
 extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess();
 extern "C" __declspec(dllimport) int __stdcall K32GetProcessMemoryInfo (void*, void*, unsigned long);
#elif JUCE_LINUX
 #include <unistd.h>
#endif

namespace ProcessMemory
{
    // Memoria usada por este programa, en bytes (0 si no se sabe)
    inline juce::int64 usedBytes()
    {
       #if JUCE_MAC
        task_vm_info_data_t info {};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
            return (juce::int64) info.phys_footprint;   // lo mismo que muestra el Monitor de Actividad
       #elif JUCE_WINDOWS
        struct Counters
        {
            unsigned long cb, pageFaultCount;
            size_t peakWorkingSetSize, workingSetSize, quotaPeakPagedPoolUsage, quotaPagedPoolUsage,
                   quotaPeakNonPagedPoolUsage, quotaNonPagedPoolUsage, pagefileUsage, peakPagefileUsage;
        } c {};
        c.cb = sizeof (c);
        if (K32GetProcessMemoryInfo (GetCurrentProcess(), &c, sizeof (c)) != 0)
            return (juce::int64) c.workingSetSize;
       #elif JUCE_LINUX
        long pages = 0, resident = 0;
        if (auto* f = std::fopen ("/proc/self/statm", "r"))
        {
            if (std::fscanf (f, "%ld %ld", &pages, &resident) != 2) resident = 0;
            std::fclose (f);
        }
        return (juce::int64) resident * (juce::int64) sysconf (_SC_PAGESIZE);
       #endif
        return 0;
    }
}

class ResourceMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ResourceMeter (juce::AudioDeviceManager& dm) : deviceManager (dm)
    {
        totalMB = juce::jmax (1, juce::SystemStats::getMemorySizeInMegabytes());
        startTimerHz (8);
        update();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::fillInset (g, r, Theme::meterBg, 4.0f);
        auto inner = r.reduced (6.0f, 4.0f);
        const float rowH = inner.getHeight() * 0.5f;
        drawRow (g, inner.removeFromTop (rowH), "CPU", cpuShown, cpuPeak, juce::String (juce::roundToInt (cpuShown * 100.0f)) + " %");
        drawRow (g, inner, "RAM", ramFraction, -1.0f, ramText());
    }

private:
    static constexpr int numSegments = 14;

    void drawRow (juce::Graphics& g, juce::Rectangle<float> row, const juce::String& label, float value, float peak, const juce::String& text)
    {
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
        g.setColour (Theme::meterTextDim);
        g.drawText (label, row.removeFromLeft (26.0f), juce::Justification::centredLeft);
        g.setColour (Theme::meterText);
        g.drawText (text, row.removeFromRight (46.0f), juce::Justification::centredRight);
        row.removeFromRight (4.0f);

        // Segmentos de LED: verde, amarillo a partir del 60 %, rojo a partir del 85 %
        auto bar = row.withSizeKeepingCentre (row.getWidth(), juce::jmin (8.0f, row.getHeight() - 3.0f));
        const float segW = bar.getWidth() / (float) numSegments;
        const int lit = value <= 0.0f ? 0 : juce::jmax (1, juce::roundToInt (value * numSegments));
        const int peakSeg = peak < 0.005f ? -1 : juce::jlimit (0, numSegments - 1, (int) std::ceil (peak * numSegments) - 1);
        for (int i = 0; i < numSegments; ++i)
        {
            const float pos = (i + 0.5f) / (float) numSegments;
            const auto c = pos > 0.85f ? Theme::recordRed : pos > 0.6f ? Theme::soloYellow : juce::Colour (0xff4cd964);
            const bool on = i < lit || i == peakSeg;
            g.setColour (on ? c : c.withAlpha (0.13f));
            g.fillRoundedRectangle (bar.getX() + i * segW + 0.5f, bar.getY(), segW - 1.5f, bar.getHeight(), 1.0f);
        }
    }

    juce::String ramText() const
    {
        if (ramMB <= 0) return "--";
        return ramMB >= 1024 ? juce::String (ramMB / 1024.0, 1) + " GB" : juce::String (ramMB) + " MB";
    }

    void timerCallback() override { update(); }

    void update()
    {
        // CPU: sube al momento y baja despacio (como la aguja de un vúmetro); el pico se queda un rato
        const float cpu = (float) juce::jlimit (0.0, 1.0, deviceManager.getCpuUsage());
        cpuShown = cpu > cpuShown ? cpu : cpuShown * 0.85f + cpu * 0.15f;
        if (cpu >= cpuPeak) { cpuPeak = cpu; peakAge = 0; }
        else if (++peakAge > 16) cpuPeak = juce::jmax (cpuShown, cpuPeak - 0.02f);

        if (++ramCounter >= 4 || ramMB == 0)   // la memoria se mira dos veces por segundo
        {
            ramCounter = 0;
            ramMB = (int) (ProcessMemory::usedBytes() / (1024 * 1024));
            ramFraction = (float) ramMB / (float) totalMB;
        }

        setTooltip (tr8 ("CPU: carga del procesado de audio (pico ") + juce::String (juce::roundToInt (cpuPeak * 100.0f))
                    + tr8 (" %). Si llega al 100 % habrá cortes: sube el tamaño de búfer en Audio.\n")
                    + tr8 ("RAM: memoria que usa SimpleDAW (") + ramText() + tr8 (" de ")
                    + juce::String (totalMB / 1024.0, 1) + tr8 (" GB del equipo)."));
        repaint();
    }

    juce::AudioDeviceManager& deviceManager;
    float cpuShown = 0.0f, cpuPeak = 0.0f, ramFraction = 0.0f;
    int peakAge = 0, ramMB = 0, totalMB = 1, ramCounter = 0;
};
