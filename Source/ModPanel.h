#pragma once

#include "LayerPanels.h"

namespace ui
{
//==============================================================================
// A modulation source. Click to edit, drag onto any knob to route it.
class ModChip : public juce::Component
{
public:
    std::function<void (int)> onClick;
    bool selected = false;

    ModChip (KickForgeAudioProcessor& p, int s) : proc (p), src (s) {}
    int source() const { return src; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.5f);
        const auto col = sourceColour (src);
        const auto routes = proc.routesForSource (src);

        g.setColour (selected ? col.withAlpha (0.22f) : panel2);
        g.fillRoundedRectangle (b, 6.0f);

        if (selected)
        {
            g.setColour (col);
            g.drawRoundedRectangle (b, 6.0f, 1.3f);
        }

        float live = 0.0f;
        if (! routes.empty()) live = std::fabs (proc.store.routes[routes.front()].live.load());

        g.setColour (col.withAlpha (0.25f + 0.75f * kf::clamp01 (live)));
        g.fillEllipse (b.getX() + 7.0f, b.getCentreY() - 4.5f, 9.0f, 9.0f);

        g.setColour (selected ? textHi : textLo);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (sourceName (src), b.withTrimmedLeft (22.0f).toNearestInt(), juce::Justification::centredLeft);

        if (! routes.empty())
        {
            g.setColour (col);
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText (juce::String ((int) routes.size()), b.toNearestInt().withTrimmedRight (6), juce::Justification::centredRight);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = false;
        if (onClick) onClick (src);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        dragging = true;

        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
            dc->startDragging (juce::String ("mod:") + juce::String (src), this);
    }

private:
    KickForgeAudioProcessor& proc;
    int src;
    bool dragging = false;
};

//==============================================================================
// Draw the modulator shape freehand (envelope over time, or one LFO cycle)
class ShapeEditor : public juce::Component
{
public:
    explicit ShapeEditor (KickForgeAudioProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    void setMod (int m) { mod = m; repaint(); }

    void tick()
    {
        const float t = proc.getPlayheadSeconds();
        if (t >= 0.0f || lastT >= 0.0f) repaint();
        lastT = t;
    }

    void paint (juce::Graphics& g) override
    {
        const auto col = sourceColour (mod);
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel2.darker (0.35f));
        g.fillRoundedRectangle (b, 8.0f);
        area = b.reduced (6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());

        juce::Path line, fill;
        for (int i = 0; i < kf::kShapeN; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kf::kShapeN - 1);
            const float y = area.getBottom() - proc.getShapePoint (mod, i) * area.getHeight();

            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, area.getBottom()); fill.lineTo (x, y); }
            else { line.lineTo (x, y); fill.lineTo (x, y); }
        }

        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.30f), 0.0f, area.getY(), col.withAlpha (0.0f), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (col.brighter (0.4f));
        g.strokePath (line, juce::PathStrokeType (2.2f));

        const float t = proc.getPlayheadSeconds();
        if (t >= 0.0f)
        {
            const int type = (int) std::lround (proc.getReal (kf::modIdx (mod, kf::MP_TYPE)));
            const double delay = (double) proc.getReal (kf::modIdx (mod, kf::MP_DELAY)) * 0.001;
            double u = -1.0;

            if (type == 0)
            {
                u = ((double) t - delay) / std::max (0.005, (double) proc.getReal (kf::modIdx (mod, kf::MP_TIME)) * 0.001);
            }
            else if (type == 1)
            {
                const bool sync = proc.getReal (kf::modIdx (mod, kf::MP_SYNC)) > 0.5f;
                const double hz = sync ? proc.getBpm() / 60.0 / (double) kf::kDivBeats[juce::jlimit (0, 11, (int) std::lround (proc.getReal (kf::modIdx (mod, kf::MP_DIV))))]
                                       : (double) proc.getReal (kf::modIdx (mod, kf::MP_RATE));
                u = ((double) t - delay) * hz;
                u -= std::floor (u);
            }

            if (u >= 0.0)
            {
                const float px = area.getX() + (float) std::min (1.0, u) * area.getWidth();
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawLine (px, area.getY(), px, area.getBottom(), 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        lastIdx = -1;
        paintAt (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) paintAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override { lastIdx = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override { proc.shapePreset (mod, 6); repaint(); }

    void showMenu()
    {
        juce::PopupMenu m;
        static const char* names[9] = { "Sine", "Triangle", "Saw up", "Saw down", "Decay", "Square", "Flat", "Attack + decay", "Random steps" };
        for (int i = 0; i < 9; ++i) m.addItem (i + 1, names[i]);
        m.addSeparator();
        m.addItem (20, "Smooth the curve");
        m.addItem (21, "Invert");

        juce::Component::SafePointer<ShapeEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r <= 9) safe->proc.shapePreset (safe->mod, r - 1);
            else if (r == 20) safe->proc.smoothShape (safe->mod);
            else if (r == 21) safe->invert();
            safe->repaint();
        });
    }

    void invert()
    {
        for (int i = 0; i < kf::kShapeN; ++i) proc.setShapePoint (mod, i, 1.0f - proc.getShapePoint (mod, i));
    }

private:
    void paintAt (juce::Point<float> p)
    {
        if (area.getWidth() < 2.0f || area.getHeight() < 2.0f) return;

        const int idx = juce::jlimit (0, kf::kShapeN - 1, (int) std::lround ((p.x - area.getX()) / area.getWidth() * (float) (kf::kShapeN - 1)));
        const float v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / area.getHeight());

        if (lastIdx < 0 || idx == lastIdx)
        {
            proc.setShapePoint (mod, idx, v);
        }
        else
        {
            const int a = std::min (lastIdx, idx), b = std::max (lastIdx, idx);
            for (int i = a; i <= b; ++i)
                proc.setShapePoint (mod, i, lastVal + (v - lastVal) * ((float) (i - lastIdx) / (float) (idx - lastIdx)));
        }

        lastIdx = idx;
        lastVal = v;
        repaint();
    }

    KickForgeAudioProcessor& proc;
    int mod = 0, lastIdx = -1;
    float lastVal = 1.0f, lastT = -1.0f;
    juce::Rectangle<float> area;
};

//==============================================================================
class ModPanel : public juce::Component, public Tickable
{
public:
    explicit ModPanel (KickForgeAudioProcessor& p) : proc (p), shape (p)
    {
        for (int s = 0; s < kf::kSources; ++s)
        {
            auto c = std::make_unique<ModChip> (proc, s);
            c->onClick = [this] (int src) { selectSource (src); };
            addAndMakeVisible (*c);
            chips.push_back (std::move (c));
        }

        addAndMakeVisible (shape);

        shapesBtn.setButtonText ("Shapes");
        shapesBtn.onClick = [this] { shape.showMenu(); };
        addAndMakeVisible (shapesBtn);
        smoothBtn.setButtonText ("Smooth");
        smoothBtn.onClick = [this] { if (sel < kf::kMods) proc.smoothShape (sel); shape.repaint(); };
        addAndMakeVisible (smoothBtn);
        invertBtn.setButtonText ("Invert");
        invertBtn.onClick = [this] { if (sel < kf::kMods) shape.invert(); shape.repaint(); };
        addAndMakeVisible (invertBtn);

        info.setFont (juce::Font (12.0f, juce::Font::plain));
        info.setColour (juce::Label::textColourId, textLo);
        info.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (info);

        addAndMakeVisible (routesView);
        routesView.setScrollBarsShown (true, false);

        selectSource (0);
    }

    int selectedSource() const { return sel; }

    void selectSource (int s)
    {
        sel = s;
        for (auto& c : chips) { c->selected = c->source() == s; c->repaint(); }
        shape.setMod (std::min (s, kf::kMods - 1));
        rebuildParams();
        rebuildRoutes();
    }

    void tick() override
    {
        shape.tick();
        for (auto& c : chips) c->repaint();

        if (sel < kf::kMods && (int) std::lround (proc.getReal (kf::modIdx (sel, kf::MP_TYPE))) != lastType) rebuildParams();
        if (proc.routesForSource (sel) != lastRoutes) rebuildRoutes();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto left = r.removeFromLeft (190);
        for (int s = 0; s < kf::kSources; ++s)
            chips[(size_t) s]->setBounds (left.getX() + (s % 2) * 96, left.getY() + (s / 2) * 30, 92, 28);

        r.removeFromLeft (10);
        auto right = r.removeFromRight (340);
        r.removeFromRight (10);

        const bool isMod = sel < kf::kMods;
        auto top = r.removeFromTop (24);
        shapesBtn.setVisible (isMod); smoothBtn.setVisible (isMod); invertBtn.setVisible (isMod); shape.setVisible (isMod);
        shapesBtn.setBounds (top.removeFromLeft (76)); top.removeFromLeft (4);
        smoothBtn.setBounds (top.removeFromLeft (70)); top.removeFromLeft (4);
        invertBtn.setBounds (top.removeFromLeft (70));
        r.removeFromTop (4);
        shape.setBounds (r);
        info.setBounds (r.reduced (4));

        int x = right.getX(), y = right.getY(), rowH = 0;
        for (auto& it : items)
        {
            if (x + it.w > right.getRight() && x > right.getX()) { x = right.getX(); y += rowH + 2; rowH = 0; }
            it.c->setBounds (x, y, it.w, it.h);
            x += it.w + 2;
            rowH = std::max (rowH, it.h);
        }

        right.removeFromTop (std::max (0, y + rowH - right.getY() + 6));
        routesView.setBounds (right);
        if (auto* c = routesView.getViewedComponent())
            c->setSize (std::max (200, routesView.getMaximumVisibleWidth()), c->getHeight());
    }

private:
    struct Item { std::unique_ptr<juce::Component> c; int w, h; };

    void add (std::unique_ptr<juce::Component> c, int w, int h)
    {
        addAndMakeVisible (*c);
        items.push_back ({ std::move (c), w, h });
    }

    void rebuildParams()
    {
        items.clear();
        const auto col = sourceColour (sel);

        if (sel >= kf::kMods)
        {
            const int macro = sel - kf::kMods;
            add (std::make_unique<KnobComp> (proc.apvts, "macro" + juce::String (macro + 1), "Macro " + juce::String (macro + 1), col, KnobComp::Pct), 90, 100);
            info.setText ("Macro " + juce::String (macro + 1) + ": a knob you can automate in your DAW or map to a MIDI controller.\n"
                          "Drag its chip onto any knob to control that parameter with it.", juce::dontSendNotification);
            lastType = -1;
        }
        else
        {
            lastType = (int) std::lround (proc.getReal (kf::modIdx (sel, kf::MP_TYPE)));
            auto P = [this] (int p) { return kf::modIdx (sel, p); };

            add (std::make_unique<LabeledBox> ("Type", std::make_unique<PCombo> (proc, P (kf::MP_TYPE))), 130, 44);

            if (lastType == 0)
            {
                add (std::make_unique<PKnob> (proc, P (kf::MP_TIME), "Length", col), 66, 100);
            }
            else
            {
                add (std::make_unique<LabeledBox> ("", std::make_unique<PToggle> (proc, P (kf::MP_SYNC), "Tempo sync", col)), 100, 44);
                add (std::make_unique<PKnob> (proc, P (kf::MP_RATE), "Rate", col), 66, 100);
                add (std::make_unique<LabeledBox> ("Division", std::make_unique<PCombo> (proc, P (kf::MP_DIV))), 90, 44);
            }

            add (std::make_unique<PKnob> (proc, P (kf::MP_DELAY), "Delay", col), 66, 100);
            add (std::make_unique<PKnob> (proc, P (kf::MP_SMOOTH), "Smooth", col), 66, 100);
            add (std::make_unique<LabeledBox> ("", std::make_unique<PToggle> (proc, P (kf::MP_BIP), "Bipolar", col)), 80, 44);
            info.setText ("", juce::dontSendNotification);
        }

        resized();
    }

    void rebuildRoutes()
    {
        lastRoutes = proc.routesForSource (sel);
        juce::Component::SafePointer<ModPanel> safe (this);
        routesView.setViewedComponent (new RouteList (proc, lastRoutes, false, [safe]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->rebuildRoutes(); });
        }), true);
        resized();
    }

    KickForgeAudioProcessor& proc;
    ShapeEditor shape;
    std::vector<std::unique_ptr<ModChip>> chips;
    juce::TextButton shapesBtn, smoothBtn, invertBtn;
    juce::Label info;
    juce::Viewport routesView;
    std::vector<Item> items;
    std::vector<int> lastRoutes;
    int sel = 0, lastType = -1;
};

} // namespace ui
