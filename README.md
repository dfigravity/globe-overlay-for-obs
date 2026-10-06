# Globe Overlay for OBS

An OBS Studio source that shows a live, spinning globe of where your viewers
are. Viewers check in from Twitch chat (through [Triode's check-in
service](https://checkin.triodeofficial.com)) and a pin with their name appears
at their location. Thirty-seven looks to choose from, every visual setting
exposed in the source's properties, no server to run, nothing to host.

> Status: **0.2.0, test release.** It works, it has been used on stream,
> and it may still have rough edges. See [CHANGELOG.md](CHANGELOG.md) for what's new. Please report problems in Issues with your OBS
> log attached (Help → Log Files → Upload Current Log File).

## What you need

- **OBS Studio 31 or newer** (32.x recommended), 64-bit, with its built-in
  Browser Source (present in the official builds for Windows, macOS and Linux).
- **A Triode check-in channel** for your Twitch channel, so your viewers'
  `!checkin` messages become pins. Without one the source still works but only
  shows other people's globes (paste their check-in link).
- **An internet connection while streaming**: the source connects to Triode's
  server for check-ins and loads its fonts from Google Fonts (if either is
  unreachable the globe still shows, and reconnects on its own).

Nothing else. No Homebrew, no Node, no local web server.

## Install

Download the latest build from the **Releases** page.

**Windows**

Easiest: run `globe-overlay-for-obs-<version>-windows-x64-installer.exe` from
the Releases page (quit OBS first). It installs to
`C:\ProgramData\obs-studio\plugins\globe-overlay-for-obs\` and needs no admin rights.

Manual, from the `.zip`:
1. Quit OBS.
2. Unzip. You get one folder, `globe-overlay-for-obs`, with `bin` and `data` inside.
3. Copy that **whole folder** to `C:\ProgramData\obs-studio\plugins\`, so you end up with
   ```
   C:\ProgramData\obs-studio\plugins\globe-overlay-for-obs\bin\64bit\globe-overlay-for-obs.dll
   C:\ProgramData\obs-studio\plugins\globe-overlay-for-obs\data\...
   ```
   `ProgramData` is hidden: paste the path into File Explorer's address bar.
   Create the `plugins` folder if it isn't there.
4. Start OBS.

Do **not** put the folder inside `C:\Program Files\obs-studio\obs-plugins\64bit\`.
That older location expects the DLL to sit there by itself, and OBS will not
look inside a subfolder. (If you must use it: put `globe-overlay-for-obs.dll`
directly in `obs-plugins\64bit\` and the contents of `data` in
`C:\Program Files\obs-studio\data\obs-plugins\globe-overlay-for-obs\`.)

**macOS** (Intel and Apple Silicon)
1. Quit OBS.
2. Right-click the `.pkg` and choose **Open** (this test build is not notarized,
   so a plain double-click is blocked; if macOS still refuses, System Settings
   → Privacy & Security → *Open Anyway*).
3. Follow the installer; it installs into `~/Library/Application Support/obs-studio/plugins`.
4. Start OBS.

**Linux (Ubuntu 24.04 and similar)**
`sudo apt install ./globe-overlay-for-obs-*-x86_64-linux-gnu.deb`, then start OBS.
Flatpak OBS is not supported by this package.

## Set up

1. Sources → **+** → **Globe Overlay (check-in globe)**.
2. **Look**: pick one of the 37 (Ink on Paper is the default).
3. **Channel**: paste your Triode check-in page link
   (`https://checkin.triodeofficial.com/YourChannel`) or just your channel name.
4. Width/height default to 1920×1080 and 60 FPS.
5. Everything else lives in the groups below: Names (size, height, direction,
   font), Crowded names, Pins, Pin trails, Rings under your pin, the look's own
   group (e.g. Star chart, Thermal vision), Land, Ocean and Colours. Sliders and
   colours apply live, and every group has 🎲 Randomize and ↺ Reset buttons.
   Looks with their own colour menu use it; choose "Use Continent colours" there
   to use the Land group's colours instead.

Your own check-in appears as the gold pin. Right-click the source → *Interact*
if you want to drive the look with the mouse.

## The looks

Wind Trails (7 variants, including Ink on Paper), Light Tubes (6), Particle
Earth, Gooey Earth, Storm Shell, Voxel Earth (9), ASCII Terminal, Username
Continents, Unfolding Projections, Vector Scope, Field Notebook, Stained Glass,
Paper Cut, 1-Bit, Star Chart, Thermal Vision, Liquid Chrome and Synthwave.
Oceans are see-through (only a faint animation shows), so the globe sits over
your camera or game.

## Uninstall

Delete the plugin folder (Windows: run the uninstaller from Settings → Apps, or delete `C:\ProgramData\obs-studio\plugins\globe-overlay-for-obs`;
macOS: `~/Library/Application Support/obs-studio/plugins/globe-overlay-for-obs.plugin`;
Linux: `sudo apt remove globe-overlay-for-obs`) and restart OBS.

## How it works

The source hosts OBS's own Browser Source internally and loads one of the
bundled single-file looks (`data/looks/*.html`, WebGL2). The property sheet is
generated from a settings schema shared with the pages, and live changes are
pushed into the page without a reload. See [DEVELOPING.md](DEVELOPING.md) for
building from source.

## Credits and licence

Plugin code: GPL-2.0-or-later, built on
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
Looks use [three.js](https://threejs.org) (MIT), [d3](https://d3js.org) (ISC),
Natural Earth land data (public domain) and Google Fonts families under the
SIL Open Font License (Inter, JetBrains Mono, Inconsolata, Caveat, Silkscreen,
Nunito, Bebas Neue, Permanent Marker, Playfair Display, Orbitron). Check-ins are provided by Triode's
check-in service, which is a separate product with its own terms (https://triodeofficial.com/).
