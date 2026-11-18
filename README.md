# fountain

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `fttest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context on a synthetic clock, on this Mac's GPU at two rasters, and reads the water back off
> the pixels. A steady jet tops out at **v²/2g** to **0.07 px**; an arc falls along the
> closed-form parabola to **0.2 px**; a shot rises as **Vt − gt²/2** to **0.08 px** and a tilted
> one lands on the frame **2V sin θ/g** says. Beads stand **one Rayleigh wavelength (9.0144 a)**
> apart on the picture, to **0.01 px** over the span, and the column ends at the closed-form
> breakup age to **0.08 px**. With **Lead**, the water peaks on the host's beat to **0.24
> frames**; without it, an onset's water tops out **105 frames late**, its flight time, to
> **0.2 frames**. The Over is the clip times the water to **0.0001 codes** and black exactly
> where there's no water. **18** deliberately wrong models and **17** one-character mutants of
> the shipped code are all caught. How a nozzle is disturbed, the mist and the air are
> **chosen**, not measured: see [Status](#status).

A musical fountain for [Resolume](https://resolume.com) Arena and Avenue, as two FFGL plugins:
**SW Fountain**, a source that is a fountain at night lit by its own lamps, and **SW Fountain
Over**, an effect that projects your clip onto the same fountain's water.

![A fountain at night, seen face-on: seven thin vertical jets of different heights, the
tallest breaking into a line of beads at the top, four arcs crossing in pairs at the front,
and behind them a wide curtain of mist lit green at the left through blue to pink at the
right, its lower edge torn into dark holes, all mirrored dimly in the basin](docs/hero.png)

<sub>One frame of the source, rendered by `fttest`, not captured from Resolume: the defaults
at 120 BPM with the harness's kicks in the Audio buffer, 5.5 s in (`--kicks --frames 330`).</sub>

## The one idea

**The music opens valves; the water is a ballistic object; the picture is wherever the water
is.**

Nothing is drawn to the beat. A beat or an onset opens a solenoid valve, which takes its time
to open. A band of Resolume's audio input sets a pump's head, which takes its time to follow.
Water leaves each nozzle at v = s√(2gH) (s the valve's opening, H the head) and flies:

- **an intact column** flies drag-free, in closed form, until Rayleigh's capillary
  instability has grown from the nozzle's disturbance to the column's own radius. Then it
  pinches into **beads**, one per fastest-growing wavelength;
- **the beads** feel the air: quadratic drag against the wind, with a raindrop's terminal
  speed;
- **the mist bar** sprays a curtain of fine droplets that the wind carries and the gusts tear,
  and the curtain is a field of optical depth.

The source lights all of it with lamps on a black sky. The Over puts a projector where the
camera is, so the clip lands undistorted on whatever water it hits and shows **only where
there is water**.

## What falls out

- **Water is late by its flight time.** A jet fired on an onset tops out valve lag + shot +
  v/g after it. Nothing in the plugin delays it; the water takes that long to get up there.
  A 15 m jet is 1.75 s late.
- **Lead puts it back on the beat, for beats only.** The beat grid is predictable, so a valve
  can open early by the model's own command-to-peak time (the valve, the shot, the flight,
  and the beads' rise against drag after they pinch off) and the jet peaks on the beat. An
  onset can't be anticipated: onset-driven water is always late.
- **Louder is higher, as v²/2g.** The pump's head follows the band's level, and the drag-free
  apex is the head itself.
- **Tall jets break into beads at the top.** Breakup happens at a fixed age, so a jet whose
  flight is shorter than that stays glassy and lands intact, and a taller one turns to beads
  above the height it reached by then. The beads slow, and the wind takes them.
- **Beads are spaced by Rayleigh's wavelength**, 9.0144 times the nozzle's radius, and on an
  arc their horizontal spacing stays λ cos θ all the way down.
- **The picture on the mist tears where the spray thins and drifts with the wind.** A thin
  curtain sends back 1 − e^(−τ) of the clip, which is little.

## Controls

| Group | |
| --- | --- |
| **Fountain** | Layout (Show, Row, Arcs, Fan, Screen), Pressure (a shooter's head at full drive, 0.5–20 m), Response (how much of the head the music sets), Audio (Resolume's audio input), Fire On (Steady, Beat, Half Beat, 2 Beats, Bar, Onset), Fire (a shot now), Shot (how long the valve opens, 40 ms–1 s), Lead (0–1.5 × the command-to-peak time; 1 peaks on the beat), Sensitivity (the onset detector). |
| **Water** | Nozzle (radius, 1–12 mm), Breakup (the nozzle's disturbance, ln(a/δ) from 20, calm, to 2, rough), Valve (the solenoid's lag, 0–300 ms), Pump (the pumps' lag, 0.05–3 s). |
| **Air** | Wind (±10 m/s), Gust, Screen (the mist bar's flow). |
| **View** | View (the frame's height, 1–40 m), Lamps (source: White, Warm, Rainbow, Duo), Exposure (source, ±2 stops), Mix (Over). |

**Pressure is in metres**: it's the height a shooter reaches at full drive, drag-free.

The defaults are the Show layout (seven shooters in a crown, two pairs of crossing arcs, the
mist bar behind) at 6 m, half of it from the music, shooters fired on the beat for 150 ms and
led onto it, through a 60 ms valve and a 0.6 s pump, from 4 mm nozzles disturbed to ln 9, in
still air with Gust 0.35, a frame 10 m high, Rainbow lamps. The shooters follow the Low band,
the arcs the Mid; the mist bar runs steady. The Over's defaults are the same fountain at Mix 1.

## Status

**v0.1.0, local only, 16 November 2026. Never loaded into Resolume.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). Every check below runs at **two rasters**, 1280×720 and
320×180, and ten of them again on **Apple's software renderer** at 320×180.

| check | result |
| --- | --- |
| `--constants` | Rayleigh's fastest x = ka **0.6970189**, its growth rate, λ/a **9.014369** and the bead r/a **1.890887**, re-derived in the harness with a different Bessel routine, equal the plugin's to 1e-11; his printed x² = .4858 and his nine-row table reproduce to his four figures (worst 3.2e-5); σ at 20 °C from IAPWS (72.736 mN/m), ρ from Tanaka et al. (998.2068 kg/m³) |
| `--apex` | a steady vertical jet, drag off, at heads of 2, 4.5 and 7 m: the column's top at the head to **0.07 px** (tolerance 1 px) |
| `--range` | steady arcs at 60° and 70°: every row of the descending branch on the closed-form parabola, worst **0.20 px** (tolerance 1 px) |
| `--flight` | a shot with no valve lag: 77 frames of its rise as Vt − gt²/2, worst **0.08 px**; a 40 ms shot at 60° first reaches the basin on the frame 2V sin θ/g says (frame 178 of 178) |
| `--drag` | a 1 mm jet that breaks up early tops out at the closed-form apex under quadratic drag to **0.31 px**, 213 px below the drag-free height at 720p |
| `--rk4` | 908 beads in wind and gusts against the harness's own RK4 at 1/64 of the plugin's step: worst **7.7e-10 m** (tolerance 0.1 mm) |
| `--breakup` | a 6 mm arc at 50°: beads **25.030 px** apart against λ cos θ = 25.031 px at 720p (6.257 against 6.258 at 320×180); the column ends at the closed-form breakup age to **0.08 px**; an arc that lands sooner has no beads |
| `--lead` | host beats at 120 BPM, the defaults' lags, drag and breakup: a parabola through the column's top peaks on the beat to **0.24 frames**, and at 12 m fired once a bar, where the column pinches off 0.24 s before its apex, to **0.21 frames** (tolerance 1 frame) |
| `--late` | Fire On Onset, the harness's kicks: the first kick, 0.25 s in, is heard on its own frame; a 15 m shot tops out at kick + V/g to **0.20 frames**, 105 frames late, Lead or no Lead |
| `--louder` | Response 0.6, drag off: full-scale bins put the column's top at H, silence at (1 − Response) H, to **0.13 px** |
| `--lag` | an 80 ms solenoid: the column's top every frame on the envelope of closed-form parcels, worst **0.06 px** (tolerance 1.5 px) |
| `--screen` | the Over is the clip times M in linear light, M read off a white clip, worst **0.0001 codes**; M is zero exactly where the source shows no water (0 pixels disagree) |
| `--alpha` | the Over's alpha is mix(clip alpha, 1, Mix) to **0 codes**; at Mix 0 the clip comes through bit for bit (0 of 3 686 400 values) |
| `--wind` | a puff from the mist bar drifts **35.82 px** in a second against the wind's 36.00 (8.96 against 9.00 at 320×180) |
| `--state` | resized mid-run, the last frame is **0 values** different from a run that was always at the new size |
| `--clock` | a host clock at 4.99e8 ms renders **0 values** different from one starting at 0; a clock left to settle its own unit, rendered faster than real time, settles on seconds or milliseconds correctly; Fire pressed on a frame where the clock did not move (the first, or one drawn twice) is **1 shot**, at the start of the next frame that has time |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; bad cues and settings refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **18** deliberately wrong models, **18** caught |
| mutation | **17** one-character mutants of the shipped GLSL and C++, **17** caught |
| `tools/sweep.py` | all **18** controls of the source and **17** of the Over measurably change the picture |
| shaders | all 8 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reads `SW Fountain` / `FT01` / `source` and `SW Fountain Over` / `FT02` / `effect` and renders through both |

Render cost, GPU time by `GL_TIME_ELAPSED`, median of 120 frames after 240 of warm-up, from
verify.sh's run on 16 November 2026 with other builds sharing the machine: at the defaults
the source took **0.99 ms** at 720p, **2.06 ms** at 1080p and **7.86 ms** at 4K (one run); the
Over **1.02** and **2.19 ms** at 720p and 1080p; the Screen layout at full flow and full gust
**1.27** and **2.36 ms**. Up to about 4 000 beads and spray parcels a frame. macOS only.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform; no sound has reached it.
  `oxbow` (the fleet's test host) reads both bundles and renders through them, which is all.
- **Windows** has not been built yet: the repo's CI builds it on its first push.
- **Chosen, not measured:** how hard a nozzle is disturbed (Breakup), the valve and pump lags
  and the valve law (a half-open valve halves the speed), and all of the mist: its drop size,
  settling speed, the updraft that stands in for entrainment, its spread and evaporation, the
  gusts' spectrum, the lamps.
- **Rayleigh's regime only.** Real jets at fountain speeds break up sooner and messier,
  because of the air; viscosity, satellite drops, splashes and wind on the intact column are
  left out.
- The fountain is seen face-on and has no depth.
- The defaults were chosen over six of Resolume's bundled clips through `--pipe`, looked at as
  stills, not in a show.
- No user guide, no presets and no OpenFX port.

## Browser demo

[fountain-demo.stoatworks-labs.com](https://fountain-demo.stoatworks-labs.com/) runs both
plugins in your browser: their own C++, the plugin class with its whole simulation, clock and
audio analyser, compiled unmodified to WebAssembly and drawing with their own GLSL in WebGL2.
The page is the host. Its tempo stands in for Resolume's, a browser has no audio (so the Audio
buffer holds nothing, or `fttest`'s test kicks if you pick them), and SW Fountain Over runs on
generated clips or a picture of your own. A frame of the page matched `fttest`'s on the same
settings to one 8-bit code, both plugins, with the valves held open. It is a demo, not the
plugin, and the page lists what it leaves out.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Fountain.bundle` and `Fountain Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena and they're under Sources as
**SW Fountain** and under Effects as **SW Fountain Over**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`.

## Building and testing

```bash
tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
both bundles' architecture, plist and ad-hoc signature, a host's view of them through `oxbow`
(set `OXBOW=` to one), every check at two rasters and ten of them on the software renderer,
the pipe, the negative controls, the mutants, the sweep for dead controls and the render cost.
`CLAUDE.md` lists the checks one by one; `./build/fttest --all` runs them all.

Render a frame of your own:

```bash
./build/fttest --out /tmp/fountain.png --set "Layout=Fan" --set "Wind=0.7" --kicks
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
