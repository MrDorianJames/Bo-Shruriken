#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"
#include "Audio/SlicePlayer.h"
#include "UI/LookAndFeel.h"

/**
 * Slice list interaction model:
 *
 *  Single click     → select row/cell, no editing
 *  Double click     → for Name/Note/Start/End: open text input dialog
 *                     for Gain/Atk/Rel/Stretch/Semi/Cents/Rev: ignored (use Enter)
 *  Enter key        → enter "edit mode" on selected numeric cell
 *                     in edit mode: Up/Down increment/decrement value
 *                     Enter or Escape → exit edit mode
 *  ▶ column click   → play slice
 *  ⚙ column click   → context menu
 */
class SliceTableComponent : public juce::Component,
                            public juce::TableListBoxModel
{
public:
    enum Column { Export=1, Play, Gear, Name, MidiNote, MidiLearn, Start, End,
                  Gain, Attack, Release, Stretch, Semitones, Cents, Chromatic, Reverse };

    std::function<void()>    onDataChanged;
    std::function<void(int)> onSliceTriggered;
    std::function<void(int)> onRowSelected;
    std::function<void(int)> onMidiLearnArm;
    std::function<void()>    onMidiLearnCancel;
    /** row = slice index, turningOn = new chromatic state.
     *  When turning OFF, MainComponent reads the last chromatic MIDI note
     *  and sets that slice's pitchSemitones accordingly. */
    std::function<void(int row, bool turningOn)> onChromaticToggled;

    SliceTableComponent()
    {
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId,
                        juce::Colour(BoShurikenLookAndFeel::BG_PANEL));
        table.setRowHeight(24);
        table.setMultipleSelectionEnabled(false);

        // Use a nested KeyListener so we don't inherit it directly
        // (which would hide Component's virtual keyPressed/keyStateChanged)
        keyForwarder = std::make_unique<KeyForwarder>(*this);
        table.addKeyListener(keyForwarder.get());
        table.getViewport()->addKeyListener(keyForwarder.get());

        auto& hdr = table.getHeader();
        hdr.addColumn("",       Export,    22, 22, 22, juce::TableHeaderComponent::notResizableOrSortable);
        hdr.addColumn("",       Play,      24, 24, 24, juce::TableHeaderComponent::notResizableOrSortable);
        hdr.addColumn("",       Gear,      24, 24, 24, juce::TableHeaderComponent::notResizableOrSortable);
        hdr.addColumn("Name",   Name,     100, 55, 200);
        hdr.addColumn("Note",   MidiNote,  42, 30,  65);
        hdr.addColumn("",       MidiLearn, 22, 22,  22, juce::TableHeaderComponent::notResizableOrSortable);
        hdr.addColumn("Start",  Start,     76, 46, 120);
        hdr.addColumn("End",    End,       76, 46, 120);
        hdr.addColumn("Gain",   Gain,      72, 48, 115);
        hdr.addColumn("Atk",    Attack,    62, 40,  95);
        hdr.addColumn("Rel",    Release,   62, 40,  95);
        hdr.addColumn("Str",    Stretch,   66, 44, 100);
        hdr.addColumn("Semi",   Semitones, 62, 40,  95);
        hdr.addColumn("Cents",  Cents,     62, 40,  95);
        hdr.addColumn("",       Chromatic, 22, 22,  22, juce::TableHeaderComponent::notResizableOrSortable);
        hdr.addColumn("Rev",    Reverse,   26, 26,  32, juce::TableHeaderComponent::notResizableOrSortable);

        addAndMakeVisible(table);

        // Intercept clicks on the table header for the Export select-all checkbox
        headerMouseListener.owner = this;
        table.getHeader().addMouseListener(&headerMouseListener, false);
    }

    void setSampleBuffer(SampleBuffer* buf)
    {
        sampleData = buf; editMode = false;
        table.updateContent(); repaint();
    }

    void refresh() { table.updateContent(); repaint(); }

    void selectRow(int row)
    {
        table.selectRow(row, false, true);
        table.scrollToEnsureRowIsOnscreen(row);
    }

    void grabFocus() { table.grabKeyboardFocus(); }

    /** Call from timer to update MIDI learn visual state */
    void updateMidiLearnState(int armedIndex)
    {
        if (armedIndex != midiLearnArmedRow)
        {
            midiLearnArmedRow = armedIndex;
            table.repaint();
        }
    }

    /** Returns indices of slices with exportEnabled=true */
    std::vector<int> getExportEnabledIndices() const
    {
        std::vector<int> result;
        if (!sampleData) return result;
        for (int i = 0; i < (int)sampleData->slices.size(); ++i)
            if (sampleData->slices[(size_t)i].exportEnabled)
                result.push_back(i);
        return result;
    }

    void resized() override { table.setBounds(getLocalBounds()); }

    // ── TableListBoxModel ─────────────────────────────────────────────────────

    int getNumRows() override
    {
        return sampleData ? (int)sampleData->slices.size() : 0;
    }

    void paintRowBackground(juce::Graphics& g, int row, int /*w*/, int /*h*/,
                            bool selected) override
    {
        g.fillAll(selected
            ? juce::Colour(BoShurikenLookAndFeel::ACCENT).withAlpha(0.22f)
            : (row % 2 == 0 ? juce::Colour(BoShurikenLookAndFeel::BG_PANEL)
                            : juce::Colour(BoShurikenLookAndFeel::BG_RAISED)));
    }

    void paintCell(juce::Graphics& g, int row, int col,
                   int w, int h, bool selected) override
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        const auto& sl = sampleData->slices[(size_t)row];

        // ── Export checkbox ───────────────────────────────────────────────────
        if (col == Export)
        {
            const float cx = w * 0.5f, cy = h * 0.5f, r = 5.5f;
            // Box
            g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER));
            g.drawRoundedRectangle(cx-r, cy-r, r*2, r*2, 1.5f, 1.2f);
            // Fill if enabled
            if (sl.exportEnabled)
            {
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT).withAlpha(0.85f));
                g.fillRoundedRectangle(cx-r+1.5f, cy-r+1.5f, r*2-3, r*2-3, 1.0f);
                // Tick
                g.setColour(juce::Colours::white);
                juce::Path tick;
                tick.startNewSubPath(cx-2.5f, cy);
                tick.lineTo(cx-0.5f, cy+2.5f);
                tick.lineTo(cx+3.0f, cy-2.5f);
                g.strokePath(tick, juce::PathStrokeType(1.5f));
            }
            return;
        }

        // ── MIDI learn icon (small MIDI plug symbol) ──────────────────────────
        if (col == MidiLearn)
        {
            const bool armed   = (row == midiLearnArmedRow);
            const bool isCursor = selected && row == table.getSelectedRow()
                                  && col == selectedCol;
            const float cx = w*0.5f, cy = h*0.5f;
            if (isCursor)
            {
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.7f));
                g.drawRect(1, 1, w-2, h-2, 1);
            }
            g.setColour(armed ? juce::Colour(0xffff4444)
                              : juce::Colour(BoShurikenLookAndFeel::TEXT_DIM).withAlpha(0.7f));
            g.fillRoundedRectangle(cx-5.0f, cy-3.5f, 10.0f, 7.0f, 1.5f);
            g.setColour(juce::Colour(BoShurikenLookAndFeel::BG_DEEP));
            for (int p = 0; p < 5; ++p)
                g.fillEllipse(cx - 4.0f + p * 2.0f, cy - 1.0f, 1.2f, 2.0f);
            if (armed)
            {
                g.setColour(juce::Colour(0xffff4444).withAlpha(0.5f));
                g.drawRoundedRectangle(cx-6.5f, cy-5.0f, 13.0f, 10.0f, 2.0f, 1.0f);
            }
            return;
        }

        // ── Chromatic mode icon (musical note ♪) ──────────────────────────────
        if (col == Chromatic)
        {
            const bool isCursor = selected && row == table.getSelectedRow()
                                  && col == selectedCol;
            const float cx = w*0.5f, cy = h*0.5f;
            if (isCursor)
            {
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.7f));
                g.drawRect(1, 1, w-2, h-2, 1);
            }
            g.setColour(sl.chromaticMode
                ? juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.9f)
                : juce::Colour(BoShurikenLookAndFeel::TEXT_DIM).withAlpha(0.35f));
            g.fillEllipse(cx-4.5f, cy+1.0f, 6.0f, 4.5f);
            g.fillRect(cx+1.0f, cy-5.0f, 1.5f, 7.0f);
            juce::Path flag;
            flag.startNewSubPath(cx+2.5f, cy-5.0f);
            flag.quadraticTo(cx+6.0f, cy-3.0f, cx+3.5f, cy-1.0f);
            g.strokePath(flag, juce::PathStrokeType(1.2f));
            return;
        }
        // ── Play icon ─────────────────────────────────────────────────────────
        if (col == Play)
        {
            const float cx=w*0.5f, cy=h*0.5f, r=h*0.27f;
            juce::Path tri;
            tri.addTriangle(cx-r*0.7f,cy-r, cx-r*0.7f,cy+r, cx+r,cy);
            g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.85f));
            g.fillPath(tri);
            return;
        }

        // ── Gear icon ─────────────────────────────────────────────────────────
        if (col == Gear)
        {
            const float cx=w*0.5f, cy=h*0.5f, r=h*0.21f;
            g.setColour(juce::Colour(BoShurikenLookAndFeel::TEXT_DIM).withAlpha(0.75f));
            g.drawEllipse(cx-r, cy-r, r*2.0f, r*2.0f, 1.4f);
            for (int i=0; i<6; ++i)
            {
                float a = (float)i/6.0f * juce::MathConstants<float>::twoPi;
                g.drawLine(cx+(r-0.5f)*std::cos(a), cy+(r-0.5f)*std::sin(a),
                           cx+(r+3.0f)*std::cos(a), cy+(r+3.0f)*std::sin(a), 1.4f);
            }
            g.fillEllipse(cx-2.0f, cy-2.0f, 4.0f, 4.0f);
            return;
        }

        // ── Reverse arrow ─────────────────────────────────────────────────────
        if (col == Reverse)
        {
            // Draw cursor box if this is the active column
            if (selected && row == table.getSelectedRow() && col == selectedCol)
            {
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.7f));
                g.drawRect(1, 1, w - 2, h - 2, 1);
            }

            const float cx=w*0.5f, cy=h*0.5f, r=h*0.27f;
            juce::Path arrow;
            if (sl.reversed)
            {
                arrow.addArrow({cx+r, cy, cx-r, cy}, 1.5f, r*1.1f, r*0.7f);
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT).withAlpha(0.9f));
            }
            else
            {
                arrow.addArrow({cx-r, cy, cx+r, cy}, 1.5f, r*1.1f, r*0.7f);
                g.setColour(juce::Colour(BoShurikenLookAndFeel::TEXT_DIM).withAlpha(0.35f));
            }
            g.fillPath(arrow);
            return;
        }

        // ── Value columns ─────────────────────────────────────────────────────
        const bool isRowSelected = selected;
        const bool isCursorCell  = isRowSelected
                                && row == table.getSelectedRow()
                                && col == selectedCol;
        const bool isEditCell    = isCursorCell && editMode;

        // Row-selected background (set by JUCE for selected rows)
        // Draw an additional cell cursor outline so the user always knows
        // which column the keyboard focus is on.
        if (isCursorCell && !isEditCell)
        {
            // Cursor box: ice-blue outline, no fill
            g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.7f));
            g.drawRect(1, 1, w - 2, h - 2, 1);
        }
        else if (isEditCell)
        {
            // Edit mode: filled tint + brighter outline
            g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.18f));
            g.fillRect(0, 0, w, h);
            g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2));
            g.drawRect(1, 1, w - 2, h - 2, 1);
        }

        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.5f)));
        g.setColour(isEditCell      ? juce::Colour(BoShurikenLookAndFeel::ACCENT2)
                   : isRowSelected  ? juce::Colours::white
                                    : juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));

        juce::String text;
        switch (col)
        {
            case Name:      text = sl.name; break;
            case MidiNote:  text = juce::MidiMessage::getMidiNoteName(sl.midiNote, true, true, 3); break;
            case Start:     text = juce::String(sl.startSample);        break;
            case End:       text = juce::String(sl.endSample);          break;
            case Gain:      text = juce::String(sl.gain, 2) + "x";     break;
            case Attack:    text = juce::String(sl.gainRampIn,  3) + "s"; break;
            case Release:   text = juce::String(sl.gainRampOut, 3) + "s"; break;
            case Stretch:   text = juce::String(sl.stretch, 2) + "x";  break;
            case Semitones: text = (sl.pitchSemitones >= 0 ? "+" : "") + juce::String((int)sl.pitchSemitones) + "st"; break;
            case Cents:     text = (sl.pitchCents     >= 0 ? "+" : "") + juce::String((int)sl.pitchCents)     + "c";  break;
            default: break;
        }

        // Show contextual hint
        if (isEditCell)
            text += "  [\u2191\u2193]";          // ↑↓ in edit mode
        else if (isCursorCell && isNudgeable(col))
            text += "  [\u23ce]";                // ↵ to enter edit mode

        g.drawText(text, 4, 0, w-4, h, juce::Justification::centredLeft);

        // Column border
        g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER).withAlpha(0.3f));
        g.drawVerticalLine(w-1, 1.0f, (float)h-1.0f);
    }

    // No inline component editors — all editing is keyboard/dialog driven
    juce::Component* refreshComponentForCell(int, int, bool, juce::Component*) override
    {
        return nullptr;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────────

    void cellClicked(int row, int col, const juce::MouseEvent& e) override
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;

        editMode = false;

        if (col == Export)
        {
            sampleData->slices[(size_t)row].exportEnabled =
                !sampleData->slices[(size_t)row].exportEnabled;
            table.repaintRow(row);
            if (onDataChanged) onDataChanged();
            return;
        }

        if (col == Play)  { if (onSliceTriggered) onSliceTriggered(row); return; }

        if (col == Gear || e.mods.isRightButtonDown()) { showContextMenu(row, e); return; }

        if (col == MidiLearn)
        {
            if (midiLearnArmedRow == row)
            {
                // Cancel learn
                midiLearnArmedRow = -1;
                if (onMidiLearnCancel) onMidiLearnCancel();
            }
            else
            {
                // Arm this row
                midiLearnArmedRow = row;
                if (onMidiLearnArm) onMidiLearnArm(row);
            }
            table.repaintRow(row);
            if (midiLearnArmedRow >= 0 && midiLearnArmedRow != row)
                table.repaintRow(midiLearnArmedRow);
            return;
        }

        if (col == Chromatic)
        {
            const bool wasOn   = sampleData->slices[(size_t)row].chromaticMode;
            const bool turningOn = !wasOn;
            // Only one slice can be chromatic at a time
            for (auto& s : sampleData->slices) s.chromaticMode = false;
            if (turningOn) sampleData->slices[(size_t)row].chromaticMode = true;
            table.repaint();
            if (onChromaticToggled) onChromaticToggled(row, turningOn);
            if (onDataChanged) onDataChanged();
            return;
        }

        if (col == Reverse)
        {
            sampleData->slices[(size_t)row].reversed = !sampleData->slices[(size_t)row].reversed;
            table.repaintRow(row);
            if (onDataChanged) onDataChanged();
            return;
        }

        selectedCol = col;
        table.repaintRow(row);
        if (onRowSelected) onRowSelected(row);
    }

    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;

        // Only text fields open a dialog on double-click
        if (col == Name || col == MidiNote || col == Start || col == End)
        {
            editMode = false;
            openTextDialog(row, col);
        }
        // Numeric slider columns: double-click enters edit mode (same as Enter)
        else if (isNudgeable(col))
        {
            selectedCol = col;
            editMode    = true;
            table.repaintRow(row);
        }
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        // Fires when arrow keys or mouse change the selection.
        // Sync waveform highlight WITHOUT triggering playback.
        if (lastRowSelected >= 0 && onRowSelected)
            onRowSelected(lastRowSelected);
    }

    void deleteKeyPressed(int) override
    {
        if (!sampleData || editMode) return;
        auto sel = table.getSelectedRows();
        for (int i = (int)sampleData->slices.size()-1; i >= 0; --i)
            if (sel.contains(i))
                sampleData->slices.erase(sampleData->slices.begin()+i);
        table.updateContent();
        if (onDataChanged) onDataChanged();
    }

    // ── Key handling (called by nested KeyForwarder) ──────────────────────────

    bool handleKey(const juce::KeyPress& key)
    {
        if (!sampleData) return false;
        const int row = table.getSelectedRow();
        const int kc  = key.getKeyCode();

        // ── M key: arm MIDI learn for selected row ────────────────────────────
        if ((kc == 'm' || kc == 'M') && !editMode)
        {
            if (row >= 0 && row < (int)sampleData->slices.size())
            {
                if (midiLearnArmedRow == row)
                {
                    midiLearnArmedRow = -1;
                    if (onMidiLearnCancel) onMidiLearnCancel();
                }
                else
                {
                    midiLearnArmedRow = row;
                    if (onMidiLearnArm) onMidiLearnArm(row);
                }
                table.repaintRow(row);
                return true;
            }
        }

        // ── C key: toggle chromatic mode for selected row ─────────────────────
        if ((kc == 'c' || kc == 'C') && !editMode)
        {
            if (row >= 0 && row < (int)sampleData->slices.size())
            {
                const bool wasOn   = sampleData->slices[(size_t)row].chromaticMode;
                const bool turningOn = !wasOn;
                for (auto& s : sampleData->slices) s.chromaticMode = false;
                if (turningOn) sampleData->slices[(size_t)row].chromaticMode = true;
                table.repaint();
                if (onChromaticToggled) onChromaticToggled(row, turningOn);
                if (onDataChanged) onDataChanged();
                return true;
            }
        }

        // ── Enter: toggle edit mode, Reverse, MidiLearn, or Chromatic ─────────
        if (kc == juce::KeyPress::returnKey)
        {
            if (row >= 0 && row < (int)sampleData->slices.size())
            {
                if (selectedCol == Reverse)
                {
                    sampleData->slices[(size_t)row].reversed =
                        !sampleData->slices[(size_t)row].reversed;
                    table.repaintRow(row);
                    if (onDataChanged) onDataChanged();
                    return true;
                }
                if (selectedCol == MidiLearn)
                {
                    if (midiLearnArmedRow == row)
                    {
                        midiLearnArmedRow = -1;
                        if (onMidiLearnCancel) onMidiLearnCancel();
                    }
                    else
                    {
                        midiLearnArmedRow = row;
                        if (onMidiLearnArm) onMidiLearnArm(row);
                    }
                    table.repaintRow(row);
                    return true;
                }
                if (selectedCol == Chromatic)
                {
                    const bool wasOn   = sampleData->slices[(size_t)row].chromaticMode;
                    const bool turningOn = !wasOn;
                    for (auto& s : sampleData->slices) s.chromaticMode = false;
                    if (turningOn) sampleData->slices[(size_t)row].chromaticMode = true;
                    table.repaint();
                    if (onChromaticToggled) onChromaticToggled(row, turningOn);
                    if (onDataChanged) onDataChanged();
                    return true;
                }
                if (isNudgeable(selectedCol))
                {
                    editMode = !editMode;
                    table.repaintRow(row);
                    return true;
                }
            }
            return false;
        }

        // ── Escape: exit edit mode ────────────────────────────────────────────
        if (kc == juce::KeyPress::escapeKey && editMode)
        {
            editMode = false;
            if (row >= 0) table.repaintRow(row);
            return true;
        }

        // ── In edit mode: Up/Down nudge value ────────────────────────────────
        if (editMode && row >= 0 && row < (int)sampleData->slices.size()
            && isNudgeable(selectedCol))
        {
            const bool up   = kc == juce::KeyPress::upKey;
            const bool down = kc == juce::KeyPress::downKey;
            if (up || down)
            {
                nudgeValue(row, selectedCol, up ? 1 : -1,
                           key.getModifiers().isShiftDown() ? 10.0 : 1.0);
                return true;
            }
        }

        // ── Left/Right: move between columns ─────────────────────────────────
        if (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey)
        {
            editMode = false;
            if (row >= 0) table.repaintRow(row);

            // Full set of navigable columns including icon-action columns
            static const int cols[] = { Name, MidiNote, MidiLearn, Start, End,
                                        Gain, Attack, Release,
                                        Stretch, Semitones, Cents,
                                        Chromatic, Reverse };
            static const int numCols = (int)(sizeof(cols)/sizeof(cols[0]));

            int cur = 0;
            for (int i = 0; i < numCols; ++i)
                if (cols[i] == selectedCol) { cur = i; break; }

            if (kc == juce::KeyPress::rightKey)
                cur = juce::jmin(numCols - 1, cur + 1);
            else
                cur = juce::jmax(0, cur - 1);

            selectedCol = cols[cur];
            if (row >= 0) table.repaintRow(row);
            return true;
        }

        return false;
    }

private:
    // Nested forwarder avoids hiding Component's virtual key methods
    struct KeyForwarder : public juce::KeyListener
    {
        explicit KeyForwarder(SliceTableComponent& o) : owner(o) {}
        bool keyPressed(const juce::KeyPress& k, juce::Component*) override { return owner.handleKey(k); }
        bool keyStateChanged(bool, juce::Component*) override { return false; }
        SliceTableComponent& owner;
    };
    std::unique_ptr<KeyForwarder> keyForwarder;

    // Header click listener to toggle all export checkboxes
    struct HeaderMouseListener : public juce::MouseListener
    {
        SliceTableComponent* owner = nullptr;

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (!owner || !owner->sampleData) return;
            // Check if click is in the Export column (first column, ~22px wide)
            if (e.x < 22)
            {
                // Toggle all
                bool anyEnabled = false;
                for (const auto& s : owner->sampleData->slices)
                    if (s.exportEnabled) { anyEnabled = true; break; }
                for (auto& s : owner->sampleData->slices)
                    s.exportEnabled = !anyEnabled;
                owner->table.repaint();
                if (owner->onDataChanged) owner->onDataChanged();
            }
        }
    };
    HeaderMouseListener headerMouseListener;

    // ── Value nudging ─────────────────────────────────────────────────────────

    static bool isNudgeable(int col)
    {
        return col == MidiNote || col == Start || col == End ||
               col == Gain || col == Attack || col == Release ||
               col == Stretch || col == Semitones || col == Cents;
    }

    void nudgeValue(int row, int col, int direction, double multiplier)
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        auto& sl = sampleData->slices[(size_t)row];
        const double d = (double)direction * multiplier;

        switch (col)
        {
            case MidiNote:
                sl.midiNote = juce::jlimit(0, 127, sl.midiNote + (int)d);
                break;
            case Start:
            {
                int v = juce::jlimit(0, sl.endSample - 1,
                                     sl.startSample + (int)(d * 100.0));
                sl.startSample = v;
                break;
            }
            case End:
            {
                int v = juce::jlimit(sl.startSample + 1,
                                     sampleData->totalSamples,
                                     sl.endSample + (int)(d * 100.0));
                sl.endSample = v;
                break;
            }
            case Gain:
                sl.gain = (float)juce::jlimit(0.0, 4.0, (double)sl.gain + d * 0.05);
                break;
            case Attack:
                sl.gainRampIn = (float)juce::jmax(0.0, (double)sl.gainRampIn + d * 0.005);
                break;
            case Release:
                sl.gainRampOut = (float)juce::jmax(0.0, (double)sl.gainRampOut + d * 0.005);
                break;
            case Stretch:
                sl.stretch = (float)juce::jlimit(0.1, 10.0, (double)sl.stretch + d * 0.05);
                break;
            case Semitones:
                sl.pitchSemitones = (float)juce::jlimit(-24.0, 24.0,
                                    (double)sl.pitchSemitones + d);
                break;
            case Cents:
                sl.pitchCents = (float)juce::jlimit(-100.0, 100.0,
                                (double)sl.pitchCents + d);
                break;
            default: break;
        }

        table.repaintRow(row);
        if (onDataChanged) onDataChanged();
    }

    // ── Text input dialogs (Name, Note, Start, End) ───────────────────────────

    void openTextDialog(int row, int col)
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        const auto& sl = sampleData->slices[(size_t)row];

        juce::String title, current;
        switch (col)
        {
            case Name:    title="Rename Slice";  current=sl.name;                    break;
            case MidiNote:title="Set MIDI Note"; current=juce::String(sl.midiNote); break;
            case Start:   title="Set Start Sample"; current=juce::String(sl.startSample); break;
            case End:     title="Set End Sample";   current=juce::String(sl.endSample);   break;
            default: return;
        }

        auto* aw = new juce::AlertWindow(title, "", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor("v", current, col == Name ? "Name:" : "Value:");
        aw->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        aw->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, aw, row, col](int result)
            {
                if (result == 1 && sampleData && row < (int)sampleData->slices.size())
                {
                    auto& sl2 = sampleData->slices[(size_t)row];
                    const juce::String val = aw->getTextEditorContents("v");
                    switch (col)
                    {
                        case Name:    sl2.name = val; break;
                        case MidiNote:sl2.midiNote = juce::jlimit(0,127, val.getIntValue()); break;
                        case Start:   { int v=val.getIntValue(); if(v>=0&&v<sl2.endSample) sl2.startSample=v; } break;
                        case End:     { int v=val.getIntValue(); if(v>sl2.startSample&&sampleData&&v<=sampleData->totalSamples) sl2.endSample=v; } break;
                        default: break;
                    }
                    table.repaintRow(row);
                    if (onDataChanged) onDataChanged();
                }
                delete aw;
            }), true);
    }

    // ── Context menu ──────────────────────────────────────────────────────────

    void showContextMenu(int row, const juce::MouseEvent& e)
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        const auto& sl = sampleData->slices[(size_t)row];

        juce::PopupMenu menu;
        menu.addItem(1, "Play \"" + sl.name + "\"");
        menu.addSeparator();
        menu.addItem(6, "Normalise to 0 dBFS");
        menu.addItem(7, "Normalise to -3 dBFS");
        menu.addSeparator();
        menu.addItem(2, "Export as WAV...");
        menu.addItem(3, "Export as AIFF...");
        menu.addSeparator();
        menu.addItem(4, "Rename...");
        menu.addItem(5, "Delete slice");

        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(table)
                .withTargetScreenArea({e.getScreenX(), e.getScreenY(), 1, 1}),
            [this, row](int result)
            {
                if (!sampleData || row >= (int)sampleData->slices.size()) return;
                switch (result)
                {
                    case 1: if (onSliceTriggered) onSliceTriggered(row); break;
                    case 2: exportSingleSlice(row, false); break;
                    case 3: exportSingleSlice(row, true);  break;
                    case 4: openTextDialog(row, Name);     break;
                    case 5:
                        sampleData->slices.erase(sampleData->slices.begin()+row);
                        table.updateContent();
                        if (onDataChanged) onDataChanged();
                        break;
                    case 6: normaliseSlice(row, 1.0f);   break;
                    case 7: normaliseSlice(row, 0.708f); break;
                    default: break;
                }
            });
    }

    // ── Normalise ─────────────────────────────────────────────────────────────

    void normaliseSlice(int row, float targetPeak)
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        const auto& sl = sampleData->slices[(size_t)row];
        const int len = sl.endSample - sl.startSample;
        if (len <= 0) return;

        float peak = 0.0f;
        for (int ch = 0; ch < sampleData->numChannels; ++ch)
        {
            const float* p = sampleData->buffer.getReadPointer(ch, sl.startSample);
            for (int i = 0; i < len; ++i) peak = std::max(peak, std::abs(p[i]));
        }
        if (peak < 1e-6f) return;

        const float gain = targetPeak / peak;
        for (int ch = 0; ch < sampleData->numChannels; ++ch)
            sampleData->buffer.applyGain(ch, sl.startSample, len, gain);
        sampleData->slices[(size_t)row].gain = 1.0f;
        table.updateContent();
        if (onDataChanged) onDataChanged();
    }

    // ── Export ────────────────────────────────────────────────────────────────

    void exportSingleSlice(int row, bool asAiff)
    {
        if (!sampleData || row >= (int)sampleData->slices.size()) return;
        const SlicePoint sl  = sampleData->slices[(size_t)row];
        const juce::AudioBuffer<float>& srcBuf = sampleData->buffer;
        const double sr      = sampleData->sampleRate;
        const juce::String stem = sampleData->sourceFile.getFileNameWithoutExtension();
        const int total      = (int)sampleData->slices.size();
        const int padW       = (total >= 100) ? 3 : (total >= 10) ? 2 : 1;
        const juce::String num = juce::String(row+1).paddedLeft('0', padW);
        const juce::String ext = asAiff ? "aif" : "wav";

        // Pre-render with all processing applied (stretch, pitch, gain, envelopes)
        // This runs on the message thread — safe for Rubber Band.
        juce::AudioBuffer<float> rendered = renderSliceForExport(sl, srcBuf, sr);
        if (rendered.getNumSamples() == 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Export Failed", "Could not render slice \"" + sl.name + "\".");
            return;
        }

        fileChooser = std::make_unique<juce::FileChooser>(
            "Export " + num,
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                .getChildFile(stem + "_" + num + "." + ext),
            "*." + ext);

        const int numCh = rendered.getNumChannels();   // capture before move

        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
            [rendered = std::move(rendered), sr, numCh, asAiff]
            (const juce::FileChooser& fc) mutable
            {
                auto dest = fc.getResult();
                if (dest.getFullPathName().isEmpty()) return;
                JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
                auto* s = new juce::FileOutputStream(dest);
                if (!s->openedOk()) { delete s; return; }
                juce::AudioFormatWriter* w = nullptr;
                if (asAiff) { juce::AiffAudioFormat f; w = f.createWriterFor(s, sr, (unsigned)numCh, 24, {}, 0); }
                else        { juce::WavAudioFormat  f; w = f.createWriterFor(s, sr, (unsigned)numCh, 24, {}, 0); }
                if (!w) { delete s; return; }
                w->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples());
                delete w;
                JUCE_END_IGNORE_WARNINGS_GCC_LIKE
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Exported", dest.getFullPathName());
            });
    }

    // ── Members ───────────────────────────────────────────────────────────────
    juce::TableListBox                 table { "Slices", nullptr };
    SampleBuffer*                      sampleData  = nullptr;
    int                                selectedCol = Name;
    bool                               editMode    = false;
    int                                midiLearnArmedRow = -1;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceTableComponent)
};
