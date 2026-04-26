#pragma once
#include "ShurikenHeaders.h"

/**
 * Reads the desktop accent colour from the active DE using multiple methods:
 *
 *  1. xdg-desktop-portal  — org.freedesktop.appearance / accent-color
 *     Works on KDE Plasma 6+, GNOME 47+, Cosmic, and anything that
 *     implements the Settings portal (the cross-desktop standard).
 *
 *  2. KDE kdeglobals       — [Colors:Button] FocusColor or AccentColor
 *     Direct file read, works on Plasma 5+ even without portal support.
 *
 *  3. GNOME gsettings      — org.gnome.desktop.interface accent-color
 *     GNOME 47+ named colour (teal, blue, green, etc.) mapped to RGB.
 *
 *  4. GTK theme colour     — gtk-color-scheme / @theme_selected_bg_color
 *     Last resort: parse the active GTK theme for selection colour.
 *
 *  Returns a default orange-ish colour if nothing is found.
 */
struct AccentColour
{
    static juce::Colour get()
    {
        // Try each source in priority order
        juce::Colour c;

        if (tryXdgPortal(c))    return c;
        if (tryKdeGlobals(c))   return c;
        if (tryGnomeGSettings(c)) return c;

        // Default fallback — matches our existing ACCENT colour
        return juce::Colour(0xffE8872A);
    }

private:
    // ── 1. xdg-desktop-portal ────────────────────────────────────────────────
    static bool tryXdgPortal(juce::Colour& out)
    {
        // Call: gdbus call --session
        //   --dest org.freedesktop.portal.Desktop
        //   --object-path /org/freedesktop/portal/desktop
        //   --method org.freedesktop.portal.Settings.Read
        //   "org.freedesktop.appearance" "accent-color"
        //
        // Returns: (<<(0.2, 0.5, 1.0)>,)  -- nested variant with 3 doubles

        juce::ChildProcess proc;
        juce::StringArray args;
        args.add("gdbus");
        args.add("call");
        args.add("--session");
        args.add("--dest");
        args.add("org.freedesktop.portal.Desktop");
        args.add("--object-path");
        args.add("/org/freedesktop/portal/desktop");
        args.add("--method");
        args.add("org.freedesktop.portal.Settings.Read");
        args.add("org.freedesktop.appearance");
        args.add("accent-color");

        if (!proc.start(args)) return false;
        proc.waitForProcessToFinish(2000);
        juce::String output = proc.readAllProcessOutput().trim();

        // Parse: (<<(0.123456, 0.456789, 0.789012)>,)
        // or:    (<(0.123456, 0.456789, 0.789012)>,)
        // Extract three doubles
        juce::StringArray tokens;
        output = output.retainCharacters("0123456789., \n");
        output = output.trim();

        // Split on comma and space/newline
        for (auto& part : juce::StringArray::fromTokens(output, ", \n\t", ""))
        {
            auto t = part.trim();
            if (t.containsChar('.') || t.matchesWildcard("0", false))
                tokens.add(t);
        }

        if (tokens.size() >= 3)
        {
            double r = tokens[0].getDoubleValue();
            double g = tokens[1].getDoubleValue();
            double b = tokens[2].getDoubleValue();

            // Spec says out-of-range means unset
            if (r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0
                && (r + g + b) > 0.01)   // avoid black
            {
                out = juce::Colour::fromFloatRGBA((float)r, (float)g, (float)b, 1.0f);
                return true;
            }
        }
        return false;
    }

    // ── 2. KDE kdeglobals ────────────────────────────────────────────────────
    static bool tryKdeGlobals(juce::Colour& out)
    {
        // KDE Plasma 6 stores accent in [General] AccentColor=r,g,b
        // Older Plasma stores selection color in [Colors:Selection] BackgroundNormal=r,g,b

        static const char* const paths[] = {
            "~/.config/kdeglobals",
            nullptr
        };

        for (int i = 0; paths[i]; ++i)
        {
            juce::File f(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                         .getChildFile(".config/kdeglobals"));
            if (!f.existsAsFile()) continue;

            juce::StringArray lines;
            f.readLines(lines);

            // Look for AccentColor=r,g,b  (Plasma 6)
            for (const auto& line : lines)
            {
                if (line.startsWith("AccentColor="))
                {
                    auto val = line.fromFirstOccurrenceOf("=", false, false).trim();
                    auto parts = juce::StringArray::fromTokens(val, ",", "");
                    if (parts.size() == 3)
                    {
                        int r = parts[0].getIntValue();
                        int g = parts[1].getIntValue();
                        int b = parts[2].getIntValue();
                        if (r+g+b > 0)
                        {
                            out = juce::Colour((uint8_t)r, (uint8_t)g, (uint8_t)b);
                            return true;
                        }
                    }
                }
            }

            // Fallback: [Colors:Selection] BackgroundNormal (the selection highlight)
            bool inSelectionGroup = false;
            for (const auto& line : lines)
            {
                if (line.trim() == "[Colors:Selection]")      { inSelectionGroup = true; continue; }
                if (line.startsWith("[") && inSelectionGroup) { inSelectionGroup = false; }
                if (inSelectionGroup && line.startsWith("BackgroundNormal="))
                {
                    auto val = line.fromFirstOccurrenceOf("=", false, false).trim();
                    auto parts = juce::StringArray::fromTokens(val, ",", "");
                    if (parts.size() == 3)
                    {
                        int r = parts[0].getIntValue();
                        int g = parts[1].getIntValue();
                        int b = parts[2].getIntValue();
                        if (r+g+b > 0)
                        {
                            out = juce::Colour((uint8_t)r, (uint8_t)g, (uint8_t)b);
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // ── 3. GNOME gsettings ───────────────────────────────────────────────────
    static bool tryGnomeGSettings(juce::Colour& out)
    {
        // GNOME 47+ has org.gnome.desktop.interface accent-color
        // Returns a named string: 'teal', 'blue', 'green', etc.
        juce::ChildProcess proc;
        juce::StringArray args { "gsettings", "get",
                                  "org.gnome.desktop.interface", "accent-color" };

        if (!proc.start(args)) return false;
        proc.waitForProcessToFinish(1000);
        juce::String val = proc.readAllProcessOutput()
                              .trim()
                              .unquoted()
                              .toLowerCase();

        // Named colours from GNOME's palette
        if (val == "blue"   || val == "'blue'")   { out = juce::Colour(0xff3584e4); return true; }
        if (val == "teal"   || val == "'teal'")   { out = juce::Colour(0xff2190a4); return true; }
        if (val == "green"  || val == "'green'")  { out = juce::Colour(0xff3a944a); return true; }
        if (val == "yellow" || val == "'yellow'") { out = juce::Colour(0xffc88800); return true; }
        if (val == "orange" || val == "'orange'") { out = juce::Colour(0xffe66100); return true; }
        if (val == "red"    || val == "'red'")    { out = juce::Colour(0xffe62d42); return true; }
        if (val == "pink"   || val == "'pink'")   { out = juce::Colour(0xffd56199); return true; }
        if (val == "purple" || val == "'purple'") { out = juce::Colour(0xff9141ac); return true; }
        if (val == "slate"  || val == "'slate'")  { out = juce::Colour(0xff6f8396); return true; }

        return false;
    }
};
