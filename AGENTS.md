# AGENTS.md: Fountain

Read this before changing the parcels and valve edges, the breakup, the predictor behind
Lead, the mist and its plume, the clock or the onset detector. `CLAUDE.md` has the commands;
the spec is `~/Projects/resolume/specs/SPEC-fountain.md` (its As-built section records where
this repo departs from it).

## The idea

**The music opens valves; the water is a ballistic object; the picture is wherever the water
is.** A beat or an onset opens a shooter's solenoid valve through its lag; a band level sets a
pump's head through its lag; water leaves at v = s√(2gH) and flies. An intact column is closed
form; at the age where Rayleigh's instability has grown from the nozzle's disturbance to the
column's radius it pinches into beads one wavelength of material apart, and the beads feel
quadratic drag against the air. The mist bar sprays parcels that relax to the air and are
splatted as Gaussians of optical depth. The source lights the optical depth with lamps; the
Over multiplies the clip by 1 − e^(−τ).

Audio drives the mechanism (valves and pumps), never a parameter wobble. Resolume's FFT buffer
and the host's beats are the only inputs; earshot (the shared capture library being built in
parallel) is not used.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Fountain` `FT01` source and `SW Fountain Over` `FT02` effect (exactly 16
  bytes; oxbow reads it back whole), one `FountainPlugin` class registered twice, each plugin
  with its own dense host order (`HostOrder( isEffect )`), the About block last. Lamps and
  Exposure are the source's, Mix the Over's.
- **All physics on the CPU in double, in metres and seconds**, on the plugin's own relative
  clock. The GPU draws instanced quads (column segments, beads, spray splats) into an RGBA32F
  buffer of lamp-weighted optical depth, then one composite pass. Every coverage is computed
  in the fragment shader as a box filter (a pixel against a strip, a disc, a normalised
  Gaussian widened to σ² + ¼ px²), never by the rasteriser's antialiasing, so a 1 px tolerance
  is a property of the filter and survives another rasteriser.
- **Parcels, not an integrator, for the intact column.** Each jet emits on a fixed 240 Hz
  launch grid plus one parcel at every valve edge, each in closed form. Edges in [t0, t1]
  inclusive, deduplicated by `emittedTo` (an edge exactly at a frame's start used to be missed,
  `e > t0`, and the shot rose 2 px late). With Valve at 0 the opening is a step: right limit at
  an open edge, left limit at a close edge, and a terminal parcel at the close so the shot's
  tail is where it should be.
- **Breakup.** t_b = ln(a/δ0)/q_max, the Breakup control being ln(a/δ0) (chosen: nothing in
  the literature fixes it for a fountain nozzle). Material launched one Rayleigh wavelength
  apart at the nozzle (a launch-time phase) pinches into one bead of radius 1.890887 a.
  Rayleigh's maximum is found by **bisection on the analytic slope** f' = (1 − 3x²)r + (x −
  x³)(1 − r/x − r²), r = I1/I0, in `Water.cpp` and `tools/constants.py`: golden section stalls
  at √eps on a flat maximum and was 1.5e-8 out.
- **Drag.** K = g/v_t², v_t from Atlas, Srivastava & Sekhon's fit, D clamped to its 0.5–5 mm
  range (a Rayleigh bead from a 1.33 mm nozzle is already bigger than any raindrop). RK4 on
  substeps of at most 1/240 s; a bead born inside a step integrates only its remainder.
- **Lead** opens a valve early by `CommandToPeak`: the shot, the valve's opening over it, the
  drag-free flight to apex or, when the column pinches off first, t_b + (v_t/g) atan(v_b/v_t)
  for the bead's rise against drag. Beats only; an onset is never predicted. The host's bar
  phase is unwrapped into a bar position, synced to the host on its first frame.
- **The mist bar is a screen, so it runs steady** at Pressure × 1.6 m of head whatever the
  music. The spec had it following the High band; over Resolume's clips that made the Over's
  picture pulse in and out with the hats, which reads as a parameter wobble, not a fountain.
  Response applies to the shooters (Low band) and the arcs (Mid band) only.
- **The mist model (all chosen).** Drops 50 µm, settling at 0.30 m/s, relaxing to the air
  exactly (linear drag, the air held over the step); an updraft U(y) = U0(1 − y/H_s)₊ standing
  in for entrainment, H_s = 0.7 × a **plume head** that follows the mist pump through a 1 s lag
  (the plume's air keeps moving after the spray changes; without it the wind check's puff
  collapsed the moment the bar stopped); parcels σ0 0.20 m spreading at D_t 0.02 m²/s,
  evaporating with a 4 s e-fold; 0.12 L/s per metre of bar at Screen 1. σ0, D_t and the flow
  were 0.12 m, 0.05 m²/s and 0.4 L/s/m until the clips showed a screen too thick and too
  smooth to read as spray.
- **Gusts** are three travelling sinusoids (wavelengths 23, 9.7, 4.1 m; periods 11, 5.3,
  2.9 s), frozen turbulence carried by the mean wind, amplitude Gust × (2 + |Wind|) m/s. The
  spec's Gust × (1 + |Wind|) left a still night's screen untorn at any Gust.
- **The onset detector** is ferrofluid's (primed first frame, adaptive floor, 80 ms
  refractory) with its **flux taken over the lowest quarter of the bins only**, so kicks and
  snares fire the shooters and hi-hats do not. Over the full spectrum a busy hat fired about
  one frame in five at Sensitivity 0.5. A second-frame seeding of the floor was added for the
  same hats, then removed: once the flux moved to the low quarter, its mutant survived every
  check, and code nothing tests isn't shipped.
- **The clock.** ferrofluid's vote on seconds or milliseconds against the wall clock, plus a
  fallback: after 30 frames of advancing host time with no verdict, the step's size decides (a
  frame is under a second and over a millisecond). A host rendering far faster than real time
  never wins the vote; oxbow's selftest rendered 120 frames in a fifth of a second of wall
  time and the Over came out black. The sim clock is a sum of frame steps quantised to 10 ns
  (so a long run is reproducible across machines); it drifts from the host's by at most 5 ns
  a frame, which the checks allow for (1e-6 s on "fired at its frame's start").
- **The Over's alpha is mix(clip alpha, 1, Mix).** At Mix 1 the Over paints the whole frame,
  black where there is no water, because that is what the camera sees of a fountain at night
  and it keeps "the clip only where there is water" literally true of the output. Making the
  dry pixels transparent (alpha = W) would let the layer below show through the gaps, which a
  user can already get with a blend mode. At Mix 0 the clip comes through bit for bit, alpha
  included. The clip's own alpha is the projector's: light = rgb × a, so a transparent clip
  projects nothing.
- **Below the waterline the Over is black.** The basin would catch the projection too, but the
  model has no basin surface to put it on; the source draws a dark basin with a dim mirror of
  the water there.
- **Defaults, chosen over Resolume's own clips** (the Shop74 set: FogAndDust_3, NeonRoom2_32,
  IntoTheGlow_02, SpaceUniverse_04, Metalive 01, OrganicMotions_06, through `--over --pipe` at
  30 fps, a still at frame 150, with and without `--kicks`): Nozzle 4 mm (3 mm jets were a
  hairline at View 10), Breakup ln 9 (beads near the top of the default shooters rather than
  above the frame), Gust 0.35, the mist steady, the flows above. Response 0.5 and View 10 were
  settled before the clips. Every clip survives: the clip shows through the mist with soft
  edges, the jets are faint lines over dark clips (correctly: they are thin), nothing blows out
  or goes black. SpaceUniverse_04 has only 121 frames, so the script loops its input.
- **Test hooks.** `SimHooks` (gravity scale, speed law, wavelength, drag scale, Euler beads,
  angle offset, the predictor ignoring the valve, the valve held per frame, the mist deaf to
  the wind, a solo mask, the clock scale) and the plugin's `Set...ForTest` setters exist for the
  harness and its negative controls; nothing in the plugin's own operation calls them, and
  every default is the physics.
- **The harness's music** (`feedKicks`): a steady bass bed in the low quarter, a hi-hat of
  random level at the top, a kick every half second, soft and hard by turns, and with `--kicks`
  a ghost note a thirtieth of a soft kick between them that only a high Sensitivity hears
  (6 onsets in 3 s at Sensitivity 0 and 0.5, 10 at 1).
- **CI.** `.github/workflows/ci.yml` and `release.yml` are ferrofluid's, adapted, and have
  never run: there is no remote. GitHub Actions belongs on public repos only; if this repo is
  ever created private, delete them first.
- **Provisional hand copies**: `source/StoatworksAbout*.h` (zoetrope's first header, guide
  "") and `ATTRIBUTIONS.md`, whose repo URLs for ferrofluid, millpond, zoetrope and throw are
  assumed to be `stoatworks-labs/<name>`.

## The traps actually hit

- **Golden section on Rayleigh's flat maximum** stalls at √eps: x was 1.5e-8 out and
  `--constants` caught it. Bisection on the analytic slope.
- **The sim clock is a sum of quantised steps**, so at frame 30 it was 1e-7 s from the host's
  time and "fired at its frame's start" said NO at a 1e-9 tolerance. Tolerance 1e-6 s, with
  the reason in the check.
- **A valve edge exactly at a frame's start was not a launch time** (`e > t0`): the shot rose
  2 px late at 720p. Edges in [t0, t1], deduplicated.
- **The cold-start onset floor climbs from zero**, and over the full spectrum the harness's
  hi-hat fired shots from frame 1, burying the kick at 0.25 s. Low-quarter flux (above).
- **The pump was not settled at the first kick** (head 0.14 m short at 0.25 s), so `--late`
  fits its vertex to the second kick's shot, 4 s on.
- **The wind check's puff** was low, landed parcels, and its centroid was biased 1.1 px by the
  ones that landed: now 3 s of Screen 1 at Pressure 20, View 40, measured a second apart
  after the bar stops, allowing at most 1% of parcels to go; and the plume persists.
- **Lead was checked modulo the beat.** The mutant that subtracts the beads' rise moved the
  prediction by 0.467 s, 0.033 s short of a whole beat, and passed `--lead` at 2.07 frames by
  aliasing. The tall case now fires once a bar.
- **oxbow's selftest renders faster than real time**, so the clock vote never settled and the
  Over was black. The fallback above.
- **A segment's first end mirrored passed `--apex`**: a column's top is each segment's far
  end, and those were untouched. It's `--range`'s mutant now, which it fails by 29 px.
- **The sweep's first run**: Fire pressed at frame 40 had landed by frame 180 (now 150);
  Sensitivity was dead because the harness's kicks fire at any sensitivity (the ghost note);
  Lead and Sensitivity were too faint on the Over behind the full mist screen (swept on the
  Row layout with the mist off, Sensitivity at Response 0).
- **zsh has no PIPESTATUS**: run the `| head -c 1` check under bash (verify.sh is bash).
- **`--flight`'s 3%-weak-gravity negative passes the 320×180 rise** (0.69 px against 1 px);
  it fails at 720p and on the landing frame, so it is caught, but only at the finer raster.

## Verified, and assumed

Verified, by `tools/verify.sh` on 16 November 2026 (numbers in README's Status): every claim
in the table there, at 1280×720 and 320×180 on this Mac's GPU, ten of them again on Apple's
software renderer; the negative controls and mutants; the sweep; both bundles universal,
signed ad hoc, read and rendered by oxbow.

Assumed: Rayleigh's inviscid, air-free regime; the Atlas fit for beads bigger than its range
(clamped); a valve law linear in the opening; the mist model and gust spectrum; Q_ext = 2;
that Resolume's 64 FFT bins put kicks in the lowest quarter (nobody in the fleet has measured
where they sit in frequency); that Resolume sends SetTime and SetBeatInfo every frame as
ferrofluid assumes.

Not done: loading it into Resolume on any platform; real music; a Windows build; a user
guide, presets, an OpenFX port, a browser demo (not required this round).

## Would this hold on another rasteriser, at another raster?

One line per check.

- `--constants`: no GL; double arithmetic only.
- `--apex`: yes; the column's end is the fragment shader's box filter, 1 px is the filter's width.
- `--range`: yes; row centroids of a box-filtered strip are rasteriser-independent to well under 1 px (0.20 px worst at 320×180).
- `--flight`: yes for the rise (a filtered end); the landing frame is a threshold crossing between frames, 1 frame tolerance.
- `--drag`: yes; the top is a bead's filtered disc, 1.5 px allows its edge plus the filter.
- `--rk4`: no GL in the measurement (state against state); raster-free.
- `--breakup`: yes at both rasters because the check zooms (View 1 m) until a wavelength is over 6 px at 320×180; at a raster where it is under 3 px the blobs would merge.
- `--lead`: yes; a peak time fitted over 25 frames, so a pixel's quantisation enters as ~0.3 px over 25 samples.
- `--late`: as `--lead`; the "heard" part is state.
- `--louder`: yes, as `--apex`.
- `--lag`: yes, as `--apex`, 1.5 px for the envelope's corner.
- `--screen`: yes to 2 codes (two 8-bit roundings); the support comparison is exact on any renderer that writes the same τ to the same pixel, which the shared fragment code guarantees.
- `--alpha`: yes to 1 code; Mix 0's bit-exactness needs only a texelFetch and a copy.
- `--wind`: yes; a τ-weighted centroid over ~2 400 splats, 2 px.
- `--state`: bit-exact, same context only (Apple's software renderer differs across contexts: honeydew's trap); on another renderer the claim is the same frame, not the same bits.
- `--clock`: bit-exact, same context only, as `--state`; the unit check is state.

## The negative controls and mutants

`fttest --negative`, each wrong model required to fail (checks failed of those run):

| check | wrong model | failed |
| --- | --- | --- |
| constants | the running Sim on the long-wave wavelength | 1 |
| constants | Rayleigh's table recomputed from the long-wave formula | 1 |
| apex | exit speed √(gH) | 6 |
| range | every launch angle 3° off (the spec said sin 2θ → sin θ; a 3° error is the subtler test) | 4 |
| flight | gravity 3% weak | 3 |
| drag | K = 0 | 2 |
| rk4 | beads stepped by forward Euler | 2 |
| breakup | Plateau's 2πa for Rayleigh's wavelength | 2 |
| lead | the predictor forgets the valve's lag | 4 |
| lead | Lead 0 | 4 |
| late | gravity 3% weak | 2 |
| late | the onset detector never primed | 4 |
| louder | speed in proportion to the head | 2 |
| lag | the valve's lag held a frame, then one Euler step | 2 |
| screen | the water added to the clip | 2 |
| wind | the mist deaf to the wind | 2 |
| state | a new raster empties the fountain | 2 |
| clock | the host's clock read in single precision | 4 |

`--alpha` has no negative control; its mutant (alpha mixed toward 0) is caught.

`tools/mutate.sh`, 16 one-character mutants, each built arm64 and run at 320×180 under a
300 s alarm, all caught: a segment's first end mirrored (`--range`), the source's Beer–Lambert
sign (`--apex`), a bead mirrored (`--breakup`), a spray splat mirrored (`--wind`), the Over
lighting dry air (`--screen`), the Over's alpha toward 0 (`--alpha`), Torricelli with 3 for 2
(`--apex`), an arc flying backwards (`--range`), gt²/2 with 0.6 (`--flight`), the lag's
exponent h·τ (`--lag`), the beads' rise subtracted from the lead (`--lead`), the mist carried
against the wind (`--wind`), the drag constant g·v_t² (`--drag`), the breakup age ln × q
(`--breakup`), the clock fallback upside down (`--clock`), the cold start primed with 1
(`--late`).

## The sweep's honest list

`python3 tools/sweep.py --bare` (every per-control context dropped):

- **Sensitivity** is dead on both plugins: it needs music with a soft onset in it and Fire On
  Onset.
- **Fire** is barely alive on the Over (0.023): with Fire On Beat the press is one shot among
  many and the mist screen covers most of it.
- **Lead** is barely alive on the Over (0.019): it moves a jet a few frames, which the Over
  shows only where the jet is lit, a sliver against the full screen.

## Open questions

- Should the mist follow the music at all? It's steady now (above); a slow follower of the
  overall level, over seconds, might read as a fountain's show rather than a wobble.
- Is ln 9 a fair default disturbance? Real nozzles at fountain speeds break up sooner because
  of the air, which the model leaves out.
- Where do Resolume's 64 bins sit in frequency? The onset's "lowest quarter" and the bands
  assume kicks are in it.
- Should the Over light the basin? It's black below the waterline.
- Does Arena send SetTime in seconds or milliseconds on this Mac today, and does the fallback
  ever fire in a real host? It shouldn't: Arena renders in real time.
