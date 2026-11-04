KICKFORGE v3.0 - UPDATE AND QUICK GUIDE
=======================================

UPDATE (about 20 minutes, browser only)
---------------------------------------
1. Unzip this file.
2. Open your GitHub repo > "Add file" > "Upload files".
3. Drag these two things from the unzipped folder into the browser:
     - the folder  "Source"
     - the file    "CMakeLists.txt"
   (Do NOT upload the hidden ".github" folder; your workflow stays as it is.)
   Files with the same name are replaced, new ones are added.
   Optional tidy-up afterwards: delete Source/Params.h and Source/Panels.h
   in the repo, they belong to v2 and are no longer used.
4. "Commit changes". The build starts by itself.
5. Actions tab > newest run > "Artifacts" > download KickForge-Windows.
6. CLOSE YOUR DAW, copy KickForge.vst3 over the old one in
   C:\Program Files\Common Files\VST3\  and rescan plugins.

If the build turns red: download the "build-log" artifact (or copy the red
error text from the failed "Build" step) and send it to Claude.
Old v1/v2 projects and presets do not load in v3 (new parameter system).

QUICK GUIDE
-----------
Header      Presets: < name > browses patches, Save stores one, "Init" resets the
            whole patch. "..." next to Randomise = what randomising touches.
            Randomise = each layer stays in its own folder, Full random = whole
            main folder. Locked layers (Lock button) are never changed.
Layers      Click a layer to edit it. Per layer: Rnd (dice), M, S, Lock,
            Inv (flip phase), Key (pitch follows MIDI), Init (empty the layer),
            folder menu, volume. Drop a sample file onto a layer to load it.
Layer tab   Source: Sample or Oscillator (7 waves, use as sub). Drag the two
            blue markers on the waveform to set start / end. "Silence before /
            after" add silence. Pitch mode "Keep length" keeps the sample length
            when pitched. Low cut removes the sub of this layer, "Sub cut" on the
            master strip does it for all sample layers.
Modulation  Tab "Modulation". 12 drawable modulators (Envelope or LFO) + 4 macros.
            Draw with the mouse (right-click = shapes). DRAG a modulator chip onto
            ANY knob to route it. Right-click a modulated knob to change amounts or
            remove. "Add" shifts the value, "Scale" multiplies it.
            Layer tab > "Draw volume curve" does it for the layer volume.
Effects     Bottom rack (per layer, click "Master" for the master rack).
            "+" adds an effect (up to 16), drag chips to reorder, click the dot to
            switch on/off, right-click for remove / duplicate / change type.
            "FX presets" saves and loads whole chains.
            EQ: drag the nodes, mouse wheel = Q, double-click empty space adds a
            band, right-click a node = type and Stereo / Mid / Side.
Knobs       Hold SHIFT while dragging for fine control. Double-click resets.
            Type values: C4, F#2, 1.5k, 20 ms, 50 %.
            Note names: C3 = middle C = MIDI 60.
