#pragma once

#include <juce_graphics/juce_graphics.h>
#include <thread>

namespace samplar {

/**
 * FontWarmer — forces DirectWrite (Windows) / CoreText (macOS) to populate their
 * font caches BEFORE the first editor is constructed.
 *
 * Root cause of this helper:
 *   JUCE's font lookup is slow on the first lookup of each unique (family, size,
 *   style) combination. On Windows this manifests as a 0.5-8 s delay the first
 *   time an editor opens because every Label / drawText triggers a DirectWrite
 *   lookup on the message thread. See
 *   https://forum.juce.com/t/plugin-editor-window-slow-to-open-since-juce-6-1-possibly/48152
 *
 * Strategy: on a dedicated background thread we instantiate the fonts SAMPLAR uses
 * and ask each to compute string widths. That forces the DirectWrite cache to
 * be populated. When the editor opens, first-paint hits warm cache → near-zero
 * font cost.
 *
 * Usage: FontWarmer::warmInBackground() from App::initialise /
 * SAMPLARAudioProcessor ctor. Fire-and-forget; thread is detached.
 */
class FontWarmer
{
public:
    /** Kick off font warming on a detached background thread. Safe to call once
        at app/processor start. No-op if already running. */
    static void warmInBackground()
    {
        std::thread([]()
        {
            // Probe string — covers ASCII letters/digits/punct which is enough
            // to populate DirectWrite's glyph atlas for most UI strings.
            static const juce::String probe { "AaBbCcDd 0123456789 !?,.-" };

            // (family, size, style) combinations SAMPLAR actually uses.
            struct Combo { const char* family; float size; int style; };
            static constexpr Combo combos[] = {
                { nullptr, 10.0f, juce::Font::plain },
                { nullptr, 12.0f, juce::Font::plain },
                { nullptr, 12.0f, juce::Font::bold  },
                { nullptr, 14.0f, juce::Font::plain },
                { nullptr, 14.0f, juce::Font::bold  },
                { nullptr, 16.0f, juce::Font::plain },
                { nullptr, 16.0f, juce::Font::bold  },
                { nullptr, 18.0f, juce::Font::bold  },
                { nullptr, 22.0f, juce::Font::bold  },
            };

            for (const auto& c : combos)
            {
                juce::Font f { c.size, c.style };
                // Computing the string width forces the backend to resolve the
                // typeface + shape the glyphs.
                (void) f.getStringWidthFloat(probe);
            }

            juce::Logger::writeToLog("[FontWarmer] Pre-warm complete ("
                + juce::String(static_cast<int>(sizeof(combos) / sizeof(combos[0])))
                + " font combos)");
        }).detach();
    }
};

} // namespace samplar
