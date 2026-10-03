# Changelog

## 0.2.1 — 2026-10-03

Fixes and one feature from the first outside test (thanks to the Windows tester).

### New
- **Connections** (every look): draw arcs between check-ins — thin glowing lines or lit tubes like the
  Light Tubes look — from your pin to everyone, chained in check-in order, or each to its nearest neighbour.
  Thickness, arc height, glow, pulses and colour are all live, and they can meet the pins at the head
  (where the name floats) or at the base. Off by default.
- The source can show its own globe icon in the Sources list instead of the stock Browser icon. OBS only
  allows this from 32.2.0, and only in builds compiled against those headers; the downloadable packages are
  still built against OBS 31 headers, so they keep the Browser icon for now.

### Changed
- Look-specific settings groups now sit right under the look picker, above the shared Names / Pins /
  Trails groups, so a look's own options are the first thing you see.

### Fixed
- Changing **FPS** (or width/height) no longer throws away colours and other live changes: the page is
  reloaded with every current setting and the live values are pushed again once it is back.
- Star Chart: the backdrop disc (the sea) follows the Ocean colour, not the Land colour.
- Connections in the canvas looks (Star Chart, Vector Scope, Field Notebook, ASCII, Unfolding Projections):
  arcs end cleanly at the horizon instead of drawing saw teeth and stray fragments along the limb, and fade
  out instead of vanishing when both pins turn behind the globe.

## 0.2.0 — 2026-10-01

A big step up: nine new looks (37 in total), every setting working in every look,
new name and trail options, and a stability pass for long streams.

### New looks
- **Vector Scope**: the globe drawn by an oscilloscope beam, with phosphor afterglow; names written stroke by stroke.
- **Field Notebook**: a hand-drawn ink sketch with "boiling" lines, hatched land and handwritten names.
- **Stained Glass**: jewel glass panes and lead lines; each check-in grows a new pane; the streamer gets a rose window.
- **Paper Cut**: continents as stacked cut-paper layers with soft shadows; check-ins pop up as paper islands.
- **1-Bit**: two-ink dithered pixels in the style of Return of the Obra Dinn.
- **Star Chart**: an antique star atlas; viewers are stars joined into constellations, arriving as shooting stars.
- **Thermal Vision**: an infrared camera; check-ins bloom white-hot and crowds warm their region.
- **Liquid Chrome**: mirror-metal continents that ripple when someone checks in.
- **Synthwave**: a neon wireframe planet with a striped retro sun showing through the sea.

### New settings
- **Name height above surface**, **Name direction** (straight out from the globe, or straight up) and **Name font** (the look's own, or one of nine fonts for every look).
- **Crowded names**: when names pile up in one area, a few show at a time and the rest take turns; your name always shows and new check-ins jump the queue.
- **Pin trails**: new styles (embers, electric, datastream); sparkle and smoke rebuilt; trails follow the pin's real path; "auto" picks the style that suits each look.
- **Ocean animation** for every look (swell, current, glint, dots), with speed, density, size, brightness and colour.
- Option groups for Light Tubes and Storm Shell, and for each new look.
- **Randomize** and **Reset** buttons on every settings group, plus Randomize/Reset everything.
- Looks with their own colour menu (phosphor, palette, metal, inks) always use it; pick "Use Continent colours" to use the Land colours instead.

### Fixes
- Every option now works in every look, live (many looks previously ignored pin colours, sizes, trails, rings or land colours).
- Pin trails stream opposite the pin's motion in every look.
- One Channel field (the duplicate check-in link field is gone; an old value moves over automatically).
- The hurricane under the streamer's pin is clearly visible over land and sea.
- Voxel looks take land colours (Neon and Chunky ignored them).
- Unfolding Projections works inside the plugin.

### Stability
- Hidden scenes stop rendering (the source kept drawing at full speed after its first time on program).
- "Reload page" no longer risks a crash, and really reloads.
- If Triode's server or Google Fonts are down or slow, the globe still appears and reconnects on its own.
- Server data and all settings are validated; pins are capped (Triode's maxPins, plus a 300-pin safety cap).
- Trail particles share a budget; Username Continents keeps memory flat; several leaks fixed.
- Tested with multi-hour compressed soaks, 400-viewer raids, outages, resizes, sleep/wake and GPU resets.

### Notes
- Settings you changed in 0.1.0 are kept. If a look's colours seem stuck, check whether the Land group's "use the look's own colour" boxes are unticked.
- macOS packages are still not notarized (right-click → Open).

## 0.1.0 — 2026-09-30

First test release: 28 looks, settings in the source's properties, live updates.
