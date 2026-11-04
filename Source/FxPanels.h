#pragma once

#include "Bind.h"

namespace ui
{
inline juce::String scopeLabel (int scope) { return scope < kNumSlots ? "Layer " + juce::String (scope + 1) : juce::String ("Master"); }

struct Tickable { virtual ~Tickable() = default; virtual void tick() = 0; };

//==============================================================================
class LabeledBox : public juce::Component
{
public:
    LabeledBox (const juce::String& text, std::unique_ptr<juce::Component> c) : ctl (std::move (c))
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, textLo);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
        addAndMakeVisible (*ctl);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        ctl->setBounds (r.removeFromTop (26));
    }

private:
    juce::Label label;
    std::unique_ptr<juce::Component> ctl;
};

//==============================================================================
class DistCurve : public juce::Component
{
public:
    DistCurve (KickForgeAudioProcessor& p, int s, int sl, juce::Colour c) : proc (p), scope (s), slot (sl), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel.darker (0.2f));
        g.fillRoundedRectangle (b, 6.0f);

        kf::DistParams d;
        d.type = juce::jlimit (0, 6, (int) std::lround (proc.getReal (kf::slotIdx (scope, slot, 0))));
        d.drive = proc.getReal (kf::slotIdx (scope, slot, 1));
        d.shape = proc.getReal (kf::slotIdx (scope, slot, 2));
        d.stages = juce::jlimit (1, 4, (int) std::lround (proc.getReal (kf::slotIdx (scope, slot, 5))));

        const float half = std::min (b.getWidth(), b.getHeight()) * 0.5f - 6.0f;
        const auto c = b.getCentre();

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) c.y, b.getX(), b.getRight());
        g.drawVerticalLine ((int) c.x, b.getY(), b.getBottom());

        juce::Path path;
        const int steps = std::max (8, (int) (half * 2.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float in = -1.0f + 2.0f * (float) i / (float) steps;
            const float out = juce::jlimit (-1.2f, 1.2f, kf::Distortion::curveAt (in, d));
            const float x = c.x + in * half, y = c.y - out / 1.2f * half;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        g.setColour (accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

private:
    KickForgeAudioProcessor& proc;
    int scope, slot;
    juce::Colour accent;
};

//==============================================================================
// Pro-Q style EQ: drag the nodes, mouse wheel = Q, double-click empty space adds a band,
// right-click a node for type / stereo-mid-side. Kick spectrum is drawn behind.
class EqDisplay : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    int selected = 0;

    EqDisplay (KickForgeAudioProcessor& p, int s, int sl, juce::Colour c) : proc (p), scope (s), slot (sl), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel.darker (0.25f));
        g.fillRoundedRectangle (b, 6.0f);
        area = b.reduced (6.0f, 8.0f);

        static const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        g.setFont (10.0f);
        for (float f : freqs)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) xOf (f), area.getY(), area.getBottom());
            g.setColour (textLo);
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) xOf (f) + 2, (int) area.getBottom() - 11, 30, 10, juce::Justification::left);
        }

        for (int db = -12; db <= 12; db += 6)
        {
            g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.12f : 0.05f));
            g.drawHorizontalLine ((int) yOf ((float) db), area.getX(), area.getRight());
            g.setColour (textLo);
            g.drawText (juce::String (db), (int) area.getX() + 2, (int) yOf ((float) db) - 10, 24, 10, juce::Justification::left);
        }

        // kick spectrum behind the curve
        const auto& pv = proc.getPreview();
        const auto& spec = scope < kNumSlots ? pv.specLayer[(size_t) scope] : pv.specSum;
        if (spec.size() > 2)
        {
            juce::Path sp;
            sp.startNewSubPath (area.getX(), area.getBottom());
            for (size_t i = 0; i < spec.size(); ++i)
                sp.lineTo (area.getX() + (float) i / (float) (spec.size() - 1) * area.getWidth(),
                           area.getBottom() - juce::jlimit (0.0f, 1.0f, (spec[i] + 90.0f) / 90.0f) * area.getHeight());
            sp.lineTo (area.getRight(), area.getBottom());
            sp.closeSubPath();
            g.setColour (textHi.withAlpha (0.08f));
            g.fillPath (sp);
        }

        const auto eq = readEq();
        const bool on = proc.getN (kf::slotIdx (scope, slot, kf::kOnK)) > 0.5f;
        juce::Path curve;

        for (int x = 0; x <= (int) area.getWidth(); x += 2)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) x / area.getWidth());
            const float y = yOf (juce::jlimit (-18.0f, 18.0f, kf::EqPro::responseDb (eq, f, 44100.0)));
            if (x == 0) curve.startNewSubPath (area.getX(), y); else curve.lineTo (area.getX() + (float) x, y);
        }

        g.setColour ((on ? accent : textLo).withAlpha (0.9f));
        g.strokePath (curve, juce::PathStrokeType (2.2f));

        for (int id = 0; id < 6; ++id)
        {
            const auto pt = nodePos (id);
            const bool active = id >= 4 ? (id == 4 ? eq.lcF > 20.5f : eq.hcF < 19900.0f) : kf::EqPro::bandActive (eq.b[id]);
            const auto col = id >= 4 ? textHi : bandColour (id);
            const float r = id == selected ? 8.0f : 6.0f;

            g.setColour (col.withAlpha (active ? 0.28f : 0.10f));
            g.fillEllipse (pt.x - r - 3.0f, pt.y - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f);
            g.setColour (col.withAlpha (active ? 1.0f : 0.45f));
            g.fillEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
            g.setColour (bg);
            g.setFont (juce::Font (9.5f, juce::Font::bold));
            g.drawText (id < 4 ? juce::String (id + 1) : (id == 4 ? "L" : "H"), (int) (pt.x - r), (int) (pt.y - r), (int) (r * 2.0f), (int) (r * 2.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hit (e.position);

        if (drag >= 0)
        {
            selected = drag;
            if (onSelect) onSelect (drag);

            if (e.mods.isPopupMenu() && drag < 4) { showBandMenu (drag); drag = -1; }
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0 || area.getWidth() < 4.0f) return;

        const float f = juce::jlimit (20.0f, 20000.0f, fOf (e.position.x));

        if (drag < 4)
        {
            const int base = 4 + drag * 5;
            setP (base + 1, f);
            if ((int) std::lround (getP (base)) != 3) setP (base + 2, juce::jlimit (-18.0f, 18.0f, yToDb (e.position.y)));
        }
        else if (drag == 4) setP (0, juce::jlimit (20.0f, 500.0f, f));
        else setP (2, juce::jlimit (2000.0f, 20000.0f, f));

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);

        if (h >= 0 && h < 4) { setP (4 + h * 5 + 2, 0.0f); repaint(); return; }
        if (h >= 4) { setP (h == 4 ? 0 : 2, h == 4 ? 20.0f : 20000.0f); repaint(); return; }

        const auto eq = readEq();
        for (int b = 0; b < 4; ++b)
            if (! kf::EqPro::bandActive (eq.b[b]))
            {
                const int base = 4 + b * 5;
                setP (base, 0.0f);
                setP (base + 1, juce::jlimit (20.0f, 20000.0f, fOf (e.position.x)));
                setP (base + 2, juce::jlimit (-18.0f, 18.0f, yToDb (e.position.y)));
                selected = b;
                if (onSelect) onSelect (b);
                break;
            }

        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int h = hit (e.position);

        if (h >= 0 && h < 4)
        {
            const int k = 4 + h * 5 + 3;
            setP (k, juce::jlimit (0.3f, 8.0f, getP (k) * std::exp (w.deltaY * 1.5f)));
            repaint();
        }
        else if (h >= 4)
        {
            const int k = h == 4 ? 1 : 3;
            setP (k, (float) juce::jlimit (0, 2, (int) std::lround (getP (k)) + (w.deltaY > 0 ? 1 : -1)));
            repaint();
        }
    }

private:
    static juce::Colour bandColour (int b)
    {
        static const juce::uint32 c[4] = { 0xffff6b6b, 0xff4dd0e1, 0xffffd54f, 0xffa5d6a7 };
        return juce::Colour (c[juce::jlimit (0, 3, b)]);
    }

    float getP (int k) const { return proc.getReal (kf::slotIdx (scope, slot, k)); }
    void setP (int k, float v) { proc.setReal (kf::slotIdx (scope, slot, k), v); }

    kf::EqProParams readEq() const
    {
        float p[24];
        for (int k = 0; k < 24; ++k) p[k] = getP (k);
        return kf::FxEq::read (p);
    }

    float xOf (float f) const { return area.getX() + std::log10 (f / 20.0f) / 3.0f * area.getWidth(); }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, (x - area.getX()) / std::max (1.0f, area.getWidth())); }
    float yOf (float db) const { return area.getCentreY() - db / 18.0f * (area.getHeight() * 0.5f); }
    float yToDb (float y) const { return -(y - area.getCentreY()) / (area.getHeight() * 0.5f) * 18.0f; }

    juce::Point<float> nodePos (int id) const
    {
        if (id == 4) return { xOf (std::max (20.0f, getP (0))), yOf (0.0f) };
        if (id == 5) return { xOf (std::min (20000.0f, getP (2))), yOf (0.0f) };
        const int base = 4 + id * 5;
        const bool notch = (int) std::lround (getP (base)) == 3;
        return { xOf (getP (base + 1)), yOf (notch ? 0.0f : getP (base + 2)) };
    }

    int hit (juce::Point<float> p) const
    {
        int best = -1;
        float bd = 12.0f;
        for (int id = 5; id >= 0; --id)
        {
            const float d = nodePos (id).getDistanceFrom (p);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    }

    void showBandMenu (int b)
    {
        juce::PopupMenu m;
        m.addItem (1, "Bell"); m.addItem (2, "Low shelf"); m.addItem (3, "High shelf"); m.addItem (4, "Notch");
        m.addSeparator();
        m.addItem (11, "Stereo"); m.addItem (12, "Mid only"); m.addItem (13, "Side only");
        m.addSeparator();
        m.addItem (20, "Reset band");

        juce::Component::SafePointer<EqDisplay> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, b] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            const int base = 4 + b * 5;
            if (r <= 4) safe->setP (base, (float) (r - 1));
            else if (r >= 11 && r <= 13) safe->setP (base + 4, (float) (r - 11));
            else if (r == 20) { safe->setP (base + 2, 0.0f); safe->setP (base + 3, 1.0f); safe->setP (base + 4, 0.0f); }
            safe->repaint();
        });
    }

    KickForgeAudioProcessor& proc;
    int scope, slot, drag = -1;
    juce::Colour accent;
    juce::Rectangle<float> area;
};

//==============================================================================
class FxPanel : public juce::Component, public Tickable
{
public:
    FxPanel (KickForgeAudioProcessor& p, int scopeIdx, int slotId, int type, juce::Colour accent)
        : proc (p), scope (scopeIdx)
    {
        const auto& ti = kf::fxInfo (type);

        title.setText (scopeLabel (scope) + "  -  " + ti.name, juce::dontSendNotification);
        title.setFont (juce::Font (14.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (title);

        power = std::make_unique<PToggle> (proc, kf::slotIdx (scope, slotId, kf::kOnK), "On", accent);
        addAndMakeVisible (*power);

        if (type == kf::FX_DIST)
            add (std::make_unique<DistCurve> (proc, scope, slotId, accent), 140, 100);

        for (int k = 0; k < ti.n; ++k)
        {
            const auto& pi = ti.p[k];
            const int idx = kf::slotIdx (scope, slotId, k);

            if (pi.fmt == kf::F_CHOICE)
                add (std::make_unique<LabeledBox> (pi.name, std::make_unique<PCombo> (proc, idx)), 130, 44);
            else if (pi.fmt == kf::F_BOOL)
                add (std::make_unique<LabeledBox> ("", std::make_unique<PToggle> (proc, idx, pi.name, accent)), 100, 44);
            else
                add (std::make_unique<PKnob> (proc, idx, pi.name, accent), 74, 100);
        }
    }

    void tick() override { for (auto& it : items) if (dynamic_cast<DistCurve*> (it.c.get()) != nullptr) it.c->repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto head = r.removeFromTop (26);
        power->setBounds (head.removeFromRight (52));
        title.setBounds (head);
        r.removeFromTop (8);

        int x = r.getX(), y = r.getY(), rowH = 0;
        for (auto& it : items)
        {
            if (x + it.w > r.getRight() && x > r.getX()) { x = r.getX(); y += rowH + 4; rowH = 0; }
            it.c->setBounds (x, y, it.w, it.h);
            x += it.w + 2;
            rowH = std::max (rowH, it.h);
        }
    }

private:
    struct Item { std::unique_ptr<juce::Component> c; int w, h; };

    void add (std::unique_ptr<juce::Component> c, int w, int h)
    {
        addAndMakeVisible (*c);
        items.push_back ({ std::move (c), w, h });
    }

    KickForgeAudioProcessor& proc;
    int scope;
    juce::Label title;
    std::unique_ptr<PToggle> power;
    std::vector<Item> items;
};

class EqPanel : public juce::Component, public Tickable
{
public:
    EqPanel (KickForgeAudioProcessor& p, int scopeIdx, int slotId, juce::Colour accentColour)
        : proc (p), scope (scopeIdx), slot (slotId), accent (accentColour), display (p, scopeIdx, slotId, accentColour)
    {
        title.setText (scopeLabel (scope) + "  -  EQ  (drag the nodes, wheel = Q, double-click adds a band)", juce::dontSendNotification);
        title.setFont (juce::Font (14.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (title);

        power = std::make_unique<PToggle> (proc, kf::slotIdx (scope, slot, kf::kOnK), "On", accent);
        addAndMakeVisible (*power);
        addAndMakeVisible (display);

        display.onSelect = [this] (int id) { selected = id; buildControls(); };
        buildControls();
    }

    void tick() override { display.repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto head = r.removeFromTop (26);
        power->setBounds (head.removeFromRight (52));
        title.setBounds (head);
        r.removeFromTop (4);
        display.setBounds (r.removeFromTop (std::max (90, r.getHeight() - 106)));
        r.removeFromTop (4);

        int x = r.getX();
        for (auto& it : items)
        {
            it.c->setBounds (x, r.getY(), it.w, std::min (it.h, r.getHeight()));
            x += it.w + 2;
        }
    }

private:
    struct Item { std::unique_ptr<juce::Component> c; int w, h; };

    void add (std::unique_ptr<juce::Component> c, int w, int h)
    {
        addAndMakeVisible (*c);
        items.push_back ({ std::move (c), w, h });
    }

    void buildControls()
    {
        items.clear();
        const int s = kf::slotIdx (scope, slot, 0);

        if (selected >= 4)
        {
            const bool low = selected == 4;
            add (std::make_unique<PKnob> (proc, s + (low ? 0 : 2), low ? "Low cut" : "High cut", accent), 74, 100);
            add (std::make_unique<LabeledBox> ("Slope", std::make_unique<PCombo> (proc, s + (low ? 1 : 3))), 130, 44);
        }
        else
        {
            const int base = s + 4 + selected * 5;
            const auto col = juce::Colour (std::array<juce::uint32, 4> { 0xffff6b6b, 0xff4dd0e1, 0xffffd54f, 0xffa5d6a7 }[(size_t) juce::jlimit (0, 3, selected)]);
            add (std::make_unique<LabeledBox> ("Band " + juce::String (selected + 1) + " type", std::make_unique<PCombo> (proc, base)), 140, 44);
            add (std::make_unique<PKnob> (proc, base + 1, "Freq", col), 74, 100);
            add (std::make_unique<PKnob> (proc, base + 2, "Gain", col), 74, 100);
            add (std::make_unique<PKnob> (proc, base + 3, "Q", col), 74, 100);
            add (std::make_unique<LabeledBox> ("Stereo / Mid / Side", std::make_unique<PCombo> (proc, base + 4)), 140, 44);
        }

        resized();
    }

    KickForgeAudioProcessor& proc;
    int scope, slot, selected = 0;
    juce::Colour accent;
    EqDisplay display;
    juce::Label title;
    std::unique_ptr<PToggle> power;
    std::vector<Item> items;
};

//==============================================================================
class FxChip : public juce::Component
{
public:
    int slotId, type;
    bool selected = false;
    std::function<void()> onSelect, onMenu;
    std::function<void (int)> onDrag;

    FxChip (KickForgeAudioProcessor& p, int s, int id, int t, juce::Colour c)
        : slotId (id), type (t), proc (p), scope (s), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = proc.getN (kf::slotIdx (scope, slotId, kf::kOnK)) > 0.5f;

        g.setColour (selected ? accent.withAlpha (0.2f) : panel2);
        g.fillRoundedRectangle (b, 6.0f);

        if (selected)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (b, 6.0f, 1.2f);
        }

        const float cx = b.getX() + 13.0f, cy = b.getCentreY();

        if (on)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse (cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        }

        g.setColour (on ? accent : textLo.withAlpha (0.45f));
        g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);

        g.setColour (selected ? textHi : (on ? textHi.withAlpha (0.85f) : textLo));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (kf::fxInfo (type).name, b.withTrimmedLeft (26.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }

        if (e.position.x < 26.0f)
        {
            const int idx = kf::slotIdx (scope, slotId, kf::kOnK);
            proc.setN (idx, proc.getN (idx) > 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }

        if (onSelect) onSelect();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && e.position.x >= 0.0f && e.getDistanceFromDragStart() > 6 && onDrag)
            onDrag (e.getEventRelativeTo (getParentComponent()).getPosition().x);
    }

private:
    KickForgeAudioProcessor& proc;
    int scope;
    juce::Colour accent;
};

// The effect rack of one layer (or the master): chips to select / toggle / reorder, panel of the selected effect
class FxRack : public juce::Component, private juce::Timer
{
public:
    FxRack (KickForgeAudioProcessor& p, int scopeIndex) : proc (p), scope (scopeIndex), accent (layerColour (scopeIndex))
    {
        addBtn.setButtonText ("+");
        addBtn.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addBtn);

        presetBtn.setButtonText ("FX presets");
        presetBtn.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetBtn);

        syncChips();
        startTimerHz (15);
    }

    ~FxRack() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

        if (chips.empty())
        {
            g.setColour (textLo);
            g.setFont (13.0f);
            g.drawText ("No effects yet. Press + to add one (distortion, EQ, reverb, delay, ...). They run top to bottom, drag to reorder.",
                        getLocalBounds().reduced (20, 50), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        auto row = r.removeFromTop (30);

        presetBtn.setBounds (row.removeFromRight (84));
        row.removeFromRight (6);
        addBtn.setBounds (row.removeFromRight (32));
        row.removeFromRight (6);

        chipsArea = row;
        layoutChips();

        r.removeFromTop (4);
        if (panel_ != nullptr) panel_->setBounds (r);
    }

private:
    void layoutChips()
    {
        if (chips.empty()) return;
        chipW = juce::jlimit (80, 150, chipsArea.getWidth() / (int) chips.size());
        int x = chipsArea.getX();
        for (auto& c : chips) { c->setBounds (x, chipsArea.getY(), chipW - 2, chipsArea.getHeight()); x += chipW; }
    }

    void syncChips()
    {
        const auto rk = proc.getRack (scope);
        lastVer = proc.rackVersion (scope);

        std::vector<std::unique_ptr<FxChip>> fresh;
        bool selValid = false, changed = rk.size() != chips.size();

        for (const auto& sv : rk)
        {
            if (sv.id == selSlot) selValid = true;

            auto it = std::find_if (chips.begin(), chips.end(), [&sv] (const std::unique_ptr<FxChip>& c) { return c != nullptr && c->slotId == sv.id && c->type == sv.type; });

            if (it != chips.end())
            {
                fresh.push_back (std::move (*it));
            }
            else
            {
                changed = true;
                auto c = std::make_unique<FxChip> (proc, scope, sv.id, sv.type, accent);
                const int id = sv.id;
                c->onSelect = [this, id] { select (id); };
                c->onMenu = [this, id] { showChipMenu (id); };
                c->onDrag = [this, id] (int x) { dragChip (id, x); };
                addAndMakeVisible (*c);
                fresh.push_back (std::move (c));
            }
        }

        chips = std::move (fresh);

        if (! selValid) { selSlot = chips.empty() ? -1 : chips.front()->slotId; changed = true; }
        for (auto& c : chips) c->selected = c->slotId == selSlot;

        if (changed || panel_ == nullptr) buildPanel();
        resized();
        repaint();
    }

    void select (int id)
    {
        selSlot = id;
        for (auto& c : chips) { c->selected = c->slotId == id; c->repaint(); }
        buildPanel();
        resized();
    }

    void buildPanel()
    {
        panel_.reset();
        int type = -1;
        for (const auto& sv : proc.getRack (scope)) if (sv.id == selSlot) type = sv.type;
        if (type < 0) return;

        if (type == kf::FX_EQ) panel_ = std::make_unique<EqPanel> (proc, scope, selSlot, accent);
        else panel_ = std::make_unique<FxPanel> (proc, scope, selSlot, type, accent);

        addAndMakeVisible (*panel_);
    }

    void dragChip (int id, int x)
    {
        int cur = -1;
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i]->slotId == id) cur = (int) i;
        if (cur < 0 || chips.empty()) return;

        const int target = juce::jlimit (0, (int) chips.size() - 1, (x - chipsArea.getX()) / std::max (1, chipW));
        if (target != cur)
        {
            proc.moveFx (scope, cur, target);
            syncChips();
        }
    }

    void showAddMenu()
    {
        juce::PopupMenu m;
        for (int t = 0; t < kf::FX_COUNT; ++t) m.addItem (t + 1, kf::fxInfo (t).name);

        juce::Component::SafePointer<FxRack> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            const int id = safe->proc.addFx (safe->scope, r - 1);
            safe->syncChips();
            if (id >= 0) safe->select (id);
        });
    }

    void showChipMenu (int id)
    {
        juce::PopupMenu m, types;
        m.addItem (1, "Remove");
        m.addItem (2, "Duplicate");
        m.addItem (3, "Move left");
        m.addItem (4, "Move right");
        for (int t = 0; t < kf::FX_COUNT; ++t) types.addItem (100 + t, kf::fxInfo (t).name);
        m.addSubMenu ("Change type", types);

        juce::Component::SafePointer<FxRack> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, id] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            auto& p = safe->proc;
            int pos = -1;
            const auto rk = p.getRack (safe->scope);
            for (size_t i = 0; i < rk.size(); ++i) if (rk[i].id == id) pos = (int) i;

            if (r == 1) p.removeFx (safe->scope, id);
            else if (r == 2) p.duplicateFx (safe->scope, id);
            else if (r == 3) p.moveFx (safe->scope, pos, pos - 1);
            else if (r == 4) p.moveFx (safe->scope, pos, pos + 1);
            else if (r >= 100) p.changeFxType (safe->scope, id, r - 100);
            safe->syncChips();
        });
    }

    void showPresetMenu()
    {
        const auto files = proc.listPresets (true);
        juce::PopupMenu m;
        m.addItem (1, "Save this chain...");
        m.addItem (2, "Clear all effects");
        m.addSeparator();
        for (size_t i = 0; i < files.size(); ++i) m.addItem (1000 + (int) i, files[i].getFileNameWithoutExtension());
        if (files.empty()) m.addItem (3, "(no saved chains yet)", false, false);

        juce::Component::SafePointer<FxRack> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe, files] (int r)
        {
            if (safe == nullptr || r <= 0) return;

            if (r == 1)
            {
                auto content = std::make_unique<NameInput> ("Chain name", [safe] (const juce::String& n) { if (safe != nullptr) safe->proc.saveFxChain (safe->scope, n); });
                juce::CallOutBox::launchAsynchronously (std::move (content), safe->presetBtn.getScreenBounds(), nullptr);
            }
            else if (r == 2) { safe->proc.clearRack (safe->scope); safe->syncChips(); }
            else if (r >= 1000 && (size_t) (r - 1000) < files.size()) { safe->proc.loadFxChain (safe->scope, files[(size_t) (r - 1000)]); safe->syncChips(); }
        });
    }

    void timerCallback() override
    {
        if (proc.rackVersion (scope) != lastVer) syncChips();
        for (auto& c : chips) c->repaint();
        if (auto* t = dynamic_cast<Tickable*> (panel_.get())) t->tick();
    }

    KickForgeAudioProcessor& proc;
    int scope;
    juce::Colour accent;
    int lastVer = -1, selSlot = -1, chipW = 100;
    juce::TextButton addBtn, presetBtn;
    juce::Rectangle<int> chipsArea;
    std::vector<std::unique_ptr<FxChip>> chips;
    std::unique_ptr<juce::Component> panel_;
};

} // namespace ui
