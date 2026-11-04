#pragma once

#include "UiKit.h"
#include <memory>
#include <vector>

// Controls bound to the parameter store: knobs, toggles, combos, modulation routing editors.
namespace ui
{
//==============================================================================
class RouteRow : public juce::Component
{
public:
    RouteRow (KickForgeAudioProcessor& p, int r, bool showSource, std::function<void()> changed)
        : proc (p), route (r), onChange (std::move (changed))
    {
        auto& rt = proc.store.routes[r];
        const int src = rt.src.load(), tg = rt.target.load();

        label.setText (showSource ? sourceName (src) : juce::String (kf::targetName (proc.store, tg)), juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, showSource ? sourceColour (src) : textHi);
        label.setFont (juce::Font (12.0f, juce::Font::plain));
        addAndMakeVisible (label);

        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
        amount.setRange (-1.0, 1.0, 0.01);
        amount.setValue (rt.amount.load(), juce::dontSendNotification);
        amount.setDoubleClickReturnValue (true, 0.5);
        amount.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        amount.accent = sourceColour (src);
        amount.onValueChange = [this] { proc.store.routes[route].amount.store ((float) amount.getValue()); ++proc.store.version; };
        addAndMakeVisible (amount);

        mode.setButtonText (rt.mode.load() == 0 ? "Add" : "Scale");
        mode.onClick = [this]
        {
            auto& m = proc.store.routes[route].mode;
            m.store (1 - m.load());
            mode.setButtonText (m.load() == 0 ? "Add" : "Scale");
            ++proc.store.version;
        };
        addAndMakeVisible (mode);

        remove.setButtonText ("x");
        remove.onClick = [this] { proc.removeRoute (route); if (onChange) onChange(); };
        addAndMakeVisible (remove);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 2);
        remove.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        mode.setBounds (r.removeFromRight (50));
        r.removeFromRight (4);
        label.setBounds (r.removeFromLeft (150));
        amount.setBounds (r);
    }

private:
    KickForgeAudioProcessor& proc;
    int route;
    std::function<void()> onChange;
    juce::Label label;
    KnobSlider amount;
    juce::TextButton mode, remove;
};

class RouteList : public juce::Component
{
public:
    RouteList (KickForgeAudioProcessor& p, const std::vector<int>& routes, bool showSource, std::function<void()> changed)
    {
        for (int r : routes)
        {
            rows.push_back (std::make_unique<RouteRow> (p, r, showSource, changed));
            addAndMakeVisible (*rows.back());
        }

        setSize (430, std::max (1, (int) rows.size()) * 28 + 8);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        for (auto& row : rows) row->setBounds (r.removeFromTop (28));
    }

private:
    std::vector<std::unique_ptr<RouteRow>> rows;
};

class NameInput : public juce::Component
{
public:
    NameInput (const juce::String& hint, std::function<void (const juce::String&)> onOk) : cb (std::move (onOk))
    {
        ed.setTextToShowWhenEmpty (hint, textLo);
        addAndMakeVisible (ed);
        ok.setButtonText ("Save");
        ok.onClick = [this]
        {
            if (cb && ed.getText().trim().isNotEmpty()) cb (ed.getText());
            if (auto* c = findParentComponentOfClass<juce::CallOutBox>()) c->dismiss();
        };
        addAndMakeVisible (ok);
        ed.onReturnKey = [this] { ok.triggerClick(); };
        setSize (280, 42);
    }

    void parentHierarchyChanged() override { ed.grabKeyboardFocus(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        ok.setBounds (r.removeFromRight (60));
        r.removeFromRight (6);
        ed.setBounds (r);
    }

private:
    std::function<void (const juce::String&)> cb;
    juce::TextEditor ed;
    juce::TextButton ok;
};

//==============================================================================
// Knob (or horizontal slider) bound to one store parameter. Accepts dropped modulators.
class PKnob : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    std::function<void()> onRouted;

    PKnob (KickForgeAudioProcessor& p, int paramIdx, const juce::String& label, juce::Colour accent,
           bool linear = false, int dropIdx = -1)
        : proc (p), idx (paramIdx), target (dropIdx >= 0 ? dropIdx : paramIdx), isLinear (linear)
    {
        if (const auto* i0 = proc.info (idx)) pi = *i0;
        else pi = kf::PInfo { "?", 0.0f, 1.0f, 0.0f, 1.0f, kf::F_PLAIN, nullptr };

        knob.accent = accent;

        if (linear)
        {
            knob.setSliderStyle (juce::Slider::LinearHorizontal);
            knob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 18);
        }

        knob.setRange (0.0, 1.0, 0.0);
        knob.setValue (proc.getN (idx), juce::dontSendNotification);
        knob.setDoubleClickReturnValue (true, proc.defaultN (idx));
        if (pi.mn < 0.0f && pi.mx > 0.0f) knob.zeroPos = kf::toNorm (pi, 0.0f);

        const kf::PInfo info = pi;
        knob.textFromValueFunction = [info] (double v) { return fmtValue (info, kf::toReal (info, (float) v)); };
        knob.valueFromTextFunction = [info] (const juce::String& t) { return (double) kf::toNorm (info, parseValue (info, t, info.def)); };
        knob.updateText();

        knob.onValueChange = [this] { proc.setN (idx, (float) knob.getValue()); };

        juce::Component::SafePointer<PKnob> safe (this);
        knob.onRightClick = [safe] { if (safe != nullptr) safe->openRoutes(); };
        addAndMakeVisible (knob);

        if (! linear)
        {
            name.setText (label, juce::dontSendNotification);
            name.setJustificationType (juce::Justification::centred);
            name.setFont (juce::Font (11.0f, juce::Font::bold));
            name.setColour (juce::Label::textColourId, textLo);
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);
        }

        startTimerHz (20);
    }

    ~PKnob() override { stopTimer(); }

    void resized() override
    {
        auto r = getLocalBounds();
        if (! isLinear) name.setBounds (r.removeFromBottom (14));
        knob.setBounds (r);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("mod:"); }
    void itemDragEnter (const SourceDetails&) override { knob.dropGlow = true; knob.poke(); }
    void itemDragExit (const SourceDetails&) override { knob.dropGlow = false; knob.poke(); }

    void itemDropped (const SourceDetails& d) override
    {
        knob.dropGlow = false;
        knob.poke();

        const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
        const bool scale = target < kf::kSlotBase && (target - kf::kLayerBase) % kf::kLayerStride == kf::LP_AMP;
        proc.addRoute (src, target, scale ? 1 : 0, scale ? 1.0f : 0.5f);
        refreshRings();
        if (onRouted) onRouted();
    }

    KnobSlider knob;

private:
    void openRoutes()
    {
        const auto rs = proc.routesForTarget (target);
        if (rs.empty()) return;

        juce::Component::SafePointer<PKnob> safe (this);
        auto content = std::make_unique<RouteList> (proc, rs, true, [safe] { if (safe != nullptr) safe->refreshRings(); });
        juce::CallOutBox::launchAsynchronously (std::move (content), knob.getScreenBounds(), nullptr);
    }

    void refreshRings()
    {
        const auto rs = proc.routesForTarget (target);
        std::vector<KnobSlider::ModRing> rings;
        const float base = proc.getN (target);
        float eff = base;

        for (int r : rs)
        {
            const auto& rt = proc.store.routes[r];
            const float a = rt.amount.load(), live = rt.live.load();

            if (rt.mode.load() == 0)
            {
                rings.push_back ({ base, kf::clamp01 (base + a), sourceColour (rt.src.load()) });
                eff += a * live;
            }
            else
            {
                const float lo = a >= 0.0f ? 1.0f - a : 1.0f, hi = a >= 0.0f ? 1.0f : 1.0f + a;
                rings.push_back ({ base * lo, base * hi, sourceColour (rt.src.load()) });
                eff *= (a >= 0.0f ? 1.0f - a + a * live : 1.0f + a * live);
            }
        }

        knob.rings = rings;
        knob.modValue = rs.empty() ? -1.0f : kf::clamp01 (eff);
        if (! rs.empty() || hadRings) knob.repaint();
        hadRings = ! rs.empty();
    }

    void timerCallback() override
    {
        if (! isShowing()) return;

        const float n = proc.getN (idx);
        if (std::fabs ((float) knob.getValue() - n) > 1.0e-6f && ! knob.isMouseButtonDown())
            knob.setValue (n, juce::dontSendNotification);

        refreshRings();
    }

    KickForgeAudioProcessor& proc;
    int idx, target;
    bool isLinear, hadRings = false;
    kf::PInfo pi;
    juce::Label name;
};

//==============================================================================
class PToggle : public juce::TextButton, private juce::Timer
{
public:
    PToggle (KickForgeAudioProcessor& p, int idx, const juce::String& text, juce::Colour accent) : proc (p), pidx (idx)
    {
        setButtonText (text);
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setToggleState (proc.getN (pidx) > 0.5f, juce::dontSendNotification);
        onClick = [this] { proc.setN (pidx, getToggleState() ? 1.0f : 0.0f); };
        startTimerHz (15);
    }

    ~PToggle() override { stopTimer(); }

private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        const bool on = proc.getN (pidx) > 0.5f;
        if (on != getToggleState()) setToggleState (on, juce::dontSendNotification);
    }

    KickForgeAudioProcessor& proc;
    int pidx;
};

class PCombo : public juce::ComboBox, private juce::Timer
{
public:
    PCombo (KickForgeAudioProcessor& p, int idx) : proc (p), pidx (idx)
    {
        if (const auto* pi = proc.info (idx))
        {
            juce::StringArray c;
            c.addTokens (pi->choices != nullptr ? pi->choices : "", "|", "");
            addItemList (c, 1);
            count = c.size();
        }

        setSelectedId (sel(), juce::dontSendNotification);
        onChange = [this] { proc.setN (pidx, count > 1 ? (float) (getSelectedId() - 1) / (float) (count - 1) : 0.0f); };
        startTimerHz (15);
    }

    ~PCombo() override { stopTimer(); }

private:
    int sel() const
    {
        return juce::jlimit (1, std::max (1, count), (int) std::lround (proc.getN (pidx) * (float) std::max (1, count - 1)) + 1);
    }

    void timerCallback() override
    {
        if (isShowing() && ! isPopupActive() && getSelectedId() != sel())
            setSelectedId (sel(), juce::dontSendNotification);
    }

    KickForgeAudioProcessor& proc;
    int pidx, count = 1;
};

} // namespace ui
