# fountain

A musical fountain for Resolume Arena/Avenue, as two FFGL plugins from one core: valves
opened by the beat or an onset, pumps that follow the music's level, ballistic jets that
break into Rayleigh's beads and feel the air, and a mist screen the clip shows on only where
there is water. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the water, lit by its lamps on a night sky | `FT01` | `SW Fountain` | `Fountain.bundle`, `com.stoatworks.ffgl.fountain` | source |
| the clip projected on the water | `FT02` | `SW Fountain Over` | `Fountain Over.bundle`, `com.stoatworks.ffgl.fountain.over` | effect |

`SW Fountain Over` is exactly 16 bytes, FFGL's name limit: never lengthen it.

Read `AGENTS.md` before touching the parcels and valve edges (`Sim::Advance`, `emit`), the
breakup (`Water.cpp`), the predictor behind Lead (`Sim::CommandToPeak`), the mist and its
plume, the clock, or the onset detector.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/fttest --out /tmp/f.png` (the source on its defaults at 120 BPM, 3 s in);
  the summary line gives nozzles, beads, spray parcels, shots, the breakup age and px a metre
- The Over: `./build/fttest --over --out /tmp/o.png` (the harness's card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/fttest --list` (`--over` for the effect's)
- Set anything by name: `./build/fttest --set "Layout=Fan" --set "Pressure=0.8"`
  (a standard control takes the host's 0..1; an option takes its name)
- Press something later: `./build/fttest --at 100 "Fire=1"`; music in the Audio buffer: `--kicks`
- Which water is drawn: `--draw N` (1 columns, 2 beads, 4 spray)
- Film: `./build/fttest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/fttest --over --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- Constants: `python3 tools/constants.py` prints Rayleigh's numbers that `source/Water.h` carries.

## Verify
- Everything: `tools/verify.sh` (GLSL reserved words, `glslc`, the demo's copies of the
  shaders and its .wasm's sources, the submodule pin, a fresh
  universal build with no warnings, both bundles through lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest` when `OXBOW=` points at one; every check at 1280x720 and
  320x180; ten again on Apple's software renderer; `--offline`; `--pipe` for both; the
  negative controls; 17 mutants; the sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`.
- Every check at once: `./build/fttest --all`. One at a time, each at both rasters (`--size WxH`: that raster alone):
  - **No GL**: `--constants` (Rayleigh's numbers re-derived with a different Bessel routine,
    his printed table, IAPWS σ, Tanaka ρ, g_n).
  - **Flight**: `--apex` (v²/2g at three heads), `--range` (an arc's descending branch on
    the closed-form parabola, row by row), `--flight` (a shot's top as Vt − gt²/2; a tilted
    shot's landing frame), `--drag` (the closed-form apex under quadratic drag),
    `--rk4` (every bead against the harness's own RK4 at 1/64 of the step), `--lag` (the
    80 ms solenoid's envelope).
  - **Breakup**: `--breakup` (beads one Rayleigh wavelength apart on the picture; the column
    ends at t_b; a short arc has none).
  - **Music**: `--lead` (the peak on the beat, at the defaults and at 12 m once a bar),
    `--late` (an onset's water tops out V/g after the kick; the cold start is heard),
    `--louder` (full scale gives H, silence (1 − Response) H).
  - **Picture**: `--screen` (the Over is clip × M, M zero exactly where the source shows no
    water), `--alpha` (mix(clip alpha, 1, Mix); Mix 0 bit-exact), `--wind` (a puff drifts at
    the wind's speed).
  - **The rest**: `--state` (a resize changes nothing but the drawing), `--clock` (a clock
    at 4.99e8 ms renders what one from 0 does; the unit settles offline; a Fire press on a
    frame with no time in it fires at the next one's start).
- The checks can fail: `./build/fttest --negative` (18 wrong models), `tools/mutate.sh`
  (17 mutants, arm64, 320x180, perl alarm at 300 s).
- No dead controls: `python3 tools/sweep.py` (both plugins; `--bare` drops the contexts).
- Render cost: `./build/fttest --bench` (720p, 1080p, one 4K run).
- `FTTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **All the physics is on the CPU in double**, in metres and seconds on the plugin's own
  relative clock; the GPU only rasterises segments, beads and Gaussian splats the CPU has
  placed into an RGBA32F optical-depth buffer, then composites. No time and no trig reach a
  shader, so the fountain is the same at any raster (`--state`).
- **An intact column has no integrator**: parcels on a 240 Hz launch grid plus one at every
  valve edge, each in closed form. Only beads (RK4, quadratic drag) and spray (exact linear
  drag toward the air) are stepped.
- **Pressure is in metres of head**: the drag-free apex of a shooter at full drive.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Fire acts on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `fountain_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- `.github/workflows/`: `ci.yml` (macOS `--offline`, the Windows build) on every push and
  PR; `release.yml` builds both platforms on a dispatch and publishes on a `v*` tag, the
  Windows DLLs checked by `tools/wincheck.ps1` (24 and 23 parameters). GitHub Actions only
  because the repo is public; if it is ever made private, delete them first.
- Public repo `stoatworks-labs/fountain` since 2026-11-18; the fleet checkout is
  `~/Projects/resolume/fountain` (worktrees for every edit). The spec is
  `~/Projects/resolume/specs/SPEC-fountain.md`.

## Browser demo
- `demo/` is served at https://fountain-demo.stoatworks-labs.com/ by the Worker in
  `wrangler.toml` (a route, plus a proxied AAAA `100::` record for `fountain-demo`: delete the
  record and the page goes dark while deploys stay green). `.github/workflows/deploy.yml`
  redeploys it on every push to main; by hand: `cf-run npx wrangler deploy`.
- **`demo/fountain-core.{js,wasm}` is the plugin's C++, compiled and committed.** After ANY
  change under `source/` (the generated `StoatworksAbout*.h` included), run
  `demo/tools/build-wasm.sh` (emscripten's `em++`) and commit both outputs and
  `demo/wasm/inputs.sha256`; `tools/verify.sh` fails until you do.
- `demo/shaders.js` is a copy: after changing a shader, `python3 demo/tools/check_shaders.py
  --write`. The same script checks the glue's copy of fttest's `feedKicks` and the .wasm's
  input hashes.
- `demo/vendor/` is the shared kit (`stoatworks-backend/resolume-demo`); never edit it.
- Keep the page's presets thin and steady: no full-frame or large-area flash faster than
  three a second (AGENTS.md has the measurement).

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/fountain/fountain.YYYY-MM-DD.log
    %LOCALAPPDATA%\fountain\logs\fountain.YYYY-MM-DD.log

`FOUNTAIN_LOG_DIR` overrides the folder.
