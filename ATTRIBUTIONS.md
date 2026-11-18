# Attributions

Fountain is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling, the analyser and the two-plugin shape — Stoatworks ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, card, cue sheets, --pipe/--film (with SIGPIPE ignored and a closed stdout an exit of 1), --negative with its Perturb, --state and --offline (tools/fttest is fetest's infrastructure under new checks); tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh; CMakeLists.txt's two bundles from one OBJECT library, a host order per plugin with the About block last (source/Controls.cpp); the clock's vote on seconds or milliseconds (source/Fountain.cpp, by way of rosette and millpond); and source/Audio.{h,cpp}, ferrofluid's onset analyser (schlieren's, millpond's, rosette's, adapted from macroblock's) with its primed first frame, its Low and Mid bands abomerration's split. Fountain takes the onset's flux over the lowest quarter of the bins only, and adds a fallback to the clock's vote. All adapted.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's. source/GLState.h is radar's, which is vectrix's, from resolume-scopes.

### The clip as light — Stoatworks throw

<https://github.com/stoatworks-labs/throw>  
Licence: MIT  
Copyright: Stoatworks Labs

The Over's projector at the camera, so the clip lands undistorted on whatever it hits, and its light as the clip's colour times its alpha (the composite pass in source/Shaders.cpp).

### The DLL check and the release workflow's bring-up — Stoatworks xenon

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

tools/wincheck.ps1 (slitscan's and isohypse's before it), which loads each Windows DLL as a host does and reads back its name, id, type and parameters, and the release workflow's macos-15 runner, applocal copy off and --ffgl installer, as radar and xenon carry them. Adapted.

### The browser demo's build and kit — Stoatworks polyhedral

<https://github.com/stoatworks-labs/polyhedral>  
Licence: MIT  
Copyright: Stoatworks Labs

demo/tools/build-wasm.sh, which compiles the plugin's own C++ to WebAssembly unmodified, is polyhedral's pattern; the page around it is the fleet's shared demo kit (demo/vendor/). Adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The instability of jets — Lord Rayleigh, Proc. London Math. Soc. s1-10 (1878) 4

<https://doi.org/10.1112/plms/s1-10.1.4>

The growth rate of a disturbance on an inviscid column, q² = (σ/ρa³)(1 − x²) x I₁(x)/I₀(x), its fastest wavelength 9.0144 a and the bead one wavelength makes (source/Water.cpp). Read in the scanned original: fttest --constants re-derives his x² = .4858 (his (17)), his λ = 4.508 diameters (his (18)) and the table under (18) to his four figures, and --breakup measures the beads' spacing on the picture. Plateau's stability limit, a wavelength of 2πa, is the negative control's wrong answer.

### Surface tension of water — IAPWS R1-76(2014)

σ(T) = B τ^μ (1 + b τ), in source/Water.h at 20 °C: 72.736 mN/m, against the release's table's 72.74.

### Density of water — M. Tanaka, G. Girard, R. Davis, A. Peuto and N. Bignell, Metrologia 38 (2001) 301

Their equation (1), in source/Water.h at 20 °C: 998.2067 kg/m³. Checked through a published implementation of the formula, not the paper itself.

### The fall speed of raindrops — D. Atlas, R. C. Srivastava and R. S. Sekhon, Rev. Geophys. 11 (1973) 1, fitting R. Gunn and G. D. Kinzer, J. Meteor. 6 (1949) 243

v_t = 9.65 − 10.3 e^(−0.6 D) m/s for D from 0.5 to 5 mm, the beads' terminal speed and so their drag constant (source/Water.cpp). Checked through two reproductions (DISDRODB's fall_velocity module; arXiv:1707.05225, which gives the range), not the 1973 paper itself.

### The extinction paradox — H. C. van de Hulst, Light Scattering by Small Particles (1957)

A drop much larger than the wavelength removes twice its cross-section from a beam, so a curtain of spray holding volume V per area A in drops of radius r has optical depth 1.5 (V/A)/r (source/Water.h). Checked through two secondary sources (arXiv:2209.11073, arXiv:0807.2795).

### A dancing fountain's lag — J. Šatov, Development of experimental self-choreographed dancing fountain, MSc thesis, Tallinn University of Technology (2019), §2.4.1

A model jet answered its pump's command in under half a second, and sending the music to the controller ahead of time is proposed: the reason Lead exists. No number is taken from it.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The musical fountain

Jets, arcs and a mist screen, lit from below at night and fired to music by a show controller, whose water is always late by its flight time. Implemented from the physics of jets above; nothing is copied from anyone's show or software.

## Standards and published specifications

What the implementation is measured against.

- **3rd CGPM (1901), Declaration 2** — standard gravity 9.80665 m/s², in source/Water.h.
- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised so it lights the water as light, then encoded again.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
