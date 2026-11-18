/**
 * Fountain — browser demo.
 *
 * A musical fountain at night (SW Fountain, FT01, a source) and the clip
 * projected onto the same fountain's water (SW Fountain Over, FT02, an
 * effect). The one idea, from AGENTS.md: the music opens valves; the water is
 * a ballistic object; the picture is wherever the water is. A beat opens a
 * shooter's solenoid valve through its lag, water leaves at s√(2gH) and flies
 * in closed form until Rayleigh's instability pinches it into beads, the beads
 * feel quadratic drag against the wind, and the mist bar's spray is splatted
 * as Gaussians of optical depth.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `fountain-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Fountain.cpp` — the plugin class, with
 * its constructor, its clock and seconds-or-milliseconds vote, the Audio
 * buffer's analyser, the beat and Lead, the instance buffers and every uniform
 * it sets — and everything it calls (Sim, Water, Layout, Controls, Audio,
 * PassBuffer, Shaders, Diag), with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader and FFGLFBO. All of the physics is that C++, in
 * double, on this page's thread. Its GL calls go to this page's WebGL2 context
 * through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what the two
 *     registration files do in a bundle), reads its parameter declarations
 *     back through the SDK's host getters — the panel below is built from
 *     them, not from a list typed here — and forwards SetFloatParameter,
 *     SetTime, SetBeatInfo, the Audio buffer and ProcessOpenGL, as
 *     tools/fttest's Rig does.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()` — the version line and the ES precision
 *     defaults, nothing else. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2.
 *   - **The host row**: the tempo (Resolume's BPM and bar phase, here the
 *     page's) and what is in the Audio buffer (nothing, or fttest's own test
 *     kicks, copied from the harness into glue.cpp and checked against it).
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ft_convert pairs each control with
 *     the function Fountain.cpp applies to it); the units printed after it, the
 *     tooltips and the presets are this page's words. The presets are written
 *     in metres and seconds and go to the host's 0..1 through the plugin's own
 *     inverse functions (ft_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page**, as radar's and polyhedral's demos do it: the
 * Plugin dropdown is a new instance at that constructor's defaults.
 * `?plugin=over` opens the effect.
 *
 * **Audio is not on the panel.** It is FF_TYPE_BUFFER, Resolume's 64-bin FFT,
 * which the kit has no control for and a browser cannot supply. The host row
 * chooses what the page writes into it: nothing (the default, exactly what a
 * layer with no audio routed gives the plugin) or the harness's kicks.
 *
 * **Fire** (FF_TYPE_EVENT) is a button that sends the press and the release,
 * 1 then 0, as the harness's Press() does.
 *
 * **The About block is not on the panel**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createFountain from './fountain-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex() and Assemble() by
// check_shaders.py), one text per stage of each of the four passes.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.find((s) => s.stage === stage && s.text === text);
  if (!ours) {
    shaderCheck.refused += 1;
    return '';
  }
  shaderCheck.matched.add(ours.name);
  return port(ours.text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createFountain({
    fountainShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/fountain.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

//===========================================================================
// The declarations, read from each plugin's constructor through the SDK.
//===========================================================================

function declarations(effect) {
  const h = core._ft_new(effect ? 1 : 0);
  const list = [];
  const n = core._ft_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ft_param_type(h, i);
    const d = { index: i, name: text(core._ft_param_name(h, i)), type, group: text(core._ft_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ft_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ft_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ft_param_element_name(h, i, e)));
        d.values.push(core._ft_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_BUFFER) {
      d.bins = core._ft_param_element_count(h, i);
      d.usage = core._ft_param_usage(h, i);
    }
    list.push(d);
  }
  const maxInputs = core._ft_max_inputs(h);
  // The inverse conversions, for the presets: this instance's, by host index.
  const from = (name, value) => {
    const d = list.find((x) => x.name === name);
    return d ? core._ft_param_from(h, d.index, value) : NaN;
  };
  const presetValues = PRESET_UNITS.map(([title, spec]) => [title, Object.fromEntries(
    Object.entries(spec).map(([name, value]) => [name, typeof value === 'object' ? from(name, value.units) : value]),
  )]);
  // Kept, never given GL: the read-outs ask it for Controls.cpp's conversions,
  // which need only its host-index table, before any renderer exists.
  return { list, maxInputs, presetValues, converter: h };
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (metres,
// seconds, ln) and is converted by the plugin's own inverse (ft_param_from);
// an option is its element's name; a bare number is the host's 0..1. They are
// the page's choices, mostly the README's and fttest's examples, and every one
// keeps the fountain's only fast events thin: jets fired at most twice a
// second, the mist screen steady.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: the Show at 6 m, shooters on the beat, led onto it', {}],
  ['A fan in a breeze (the README’s example)', { Layout: 'Fan', Wind: 0.7 }],
  ['Eleven shooters in a row, once a bar, wide nozzles', { Layout: 'Row', 'Fire On': 'Bar', Pressure: u(10), Nozzle: u(8e-3), View: u(14) }],
  ['Valves held open: steady jets that bead at the top', { 'Fire On': 'Steady', Pressure: u(9), View: u(14) }],
  ['Crossing arcs, steady', { Layout: 'Arcs' }],
  ['The mist screen in a gusty wind', { Layout: 'Screen', Screen: 1.0, Gust: 1.0, Wind: u(2) }],
  ['Big rough nozzles: beads early, the wind takes them', { Nozzle: u(9e-3), Breakup: u(4), Wind: u(-3), Gust: 0.6, Pressure: u(9), View: u(14) }],
  ['Lead off: the water peaks late, by its flight time', { Lead: u(0), 'Fire On': 'Bar', Pressure: u(12), View: u(16) }],
  ['Slow valves, long shots (Shot 0.6 s, Valve 0.25 s)', { Shot: u(0.6), Valve: u(0.25), 'Fire On': '2 Beats' }],
  ['Warm lamps, the mist bar off, close in (View 7 m)', { Lamps: 'Warm', Screen: 0, View: u(7) }],
];

const DECLARED = { source: declarations(false), over: declarations(true) };

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const sig = (n) => (Math.abs(n) >= 100 ? n.toFixed(0) : Math.abs(n) >= 10 ? n.toFixed(1) : Math.abs(n) >= 1 ? n.toFixed(2) : n.toFixed(3));
const seconds = (s) => (s >= 1 ? `${sig(s)} s` : `${sig(s * 1000)} ms`);
const UNITS = {
  Pressure: (x) => `${sig(x)} m`,
  Shot: seconds,
  Lead: (x) => `× ${fixed(x, 2)}`,
  Nozzle: (x) => `${sig(x * 1000)} mm`,
  Breakup: (x) => `ln ${fixed(x, 1)}`,
  Valve: seconds,
  Pump: seconds,
  Wind: (x) => `${x >= 0 ? '+' : '−'}${fixed(Math.abs(x), 1)} m/s`,
  View: (x) => `${sig(x)} m`,
  Exposure: (x) => `× ${fixed(x, 2)}`,
};

function readout(d, variant) {
  return (v) => {
    const x = core._ft_convert(DECLARED[variant].converter, d.index, Math.fround(v));
    if (Number.isNaN(x)) return fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Layout: 'Where the nozzles stand along the basin’s front edge. Show: seven shooters in a crown, two pairs of crossing arcs, the mist bar behind. Row: eleven equal shooters. Arcs: crossing arcs, no shooters. Fan: nine jets fanned from one point, the centre one a shooter. Screen: the mist bar alone, a shooter at each end. Changing it is a new fountain: the water already in the air goes.',
  Pressure: 'A shooter’s head at full drive, 0.5–20 m, geometrically: the height it reaches drag-free, since water leaves at √(2gH). Each nozzle has its share; the mist bar runs at 1.6 × this.',
  Response: 'How much of the head the music sets: the pumps run at (1 − Response + Response × the band’s level) × their head. The shooters follow the Low band, the arcs the Mid. With nothing in the Audio buffer (the host row) the level is 0, so this lowers every jet.',
  'Fire On': 'What opens the shooters’ valves. Steady: held open. Beat, Half Beat, 2 Beats, Bar: the host’s beat (here the page’s tempo, in the host row), each shot opening early by Lead. Onset: an onset the analyser hears in the Audio buffer — with nothing in it, never; with the host row’s test kicks, on each kick, late by the water’s flight.',
  Fire: 'A shot from every shooter now, on the press. FF_TYPE_EVENT: the button sends the press and the release. Pressed while paused, the shot waits for the clock: it fires at the start of the first frame with time in it, when you press Play (see the notes at the foot).',
  Shot: 'How long a shot holds the valve open, 40 ms to 1 s, geometrically.',
  Lead: 'Opens each valve early by Lead × the model’s own command-to-peak time (the valve’s lag, the shot, the flight, and the beads’ rise against drag), so at 1 a beat-fired jet peaks on the beat. Beats only: an onset cannot be anticipated.',
  Sensitivity: 'The onset detector’s margin over its adaptive floor, for Fire On Onset. It listens to the Audio buffer, so with nothing in it this changes nothing.',
  Nozzle: 'The nozzle’s radius, 1–12 mm, geometrically. Rayleigh’s beads are 1.89 × it, one wavelength (9.01 × it) of jet apart; a bigger nozzle breaks up later.',
  Breakup: 'How hard the nozzle disturbs the jet, as ln(a/δ): 20 calm (the column stays glassy for longer) to 2 rough. The column pinches into beads at the age ln(a/δ) / Rayleigh’s fastest growth rate.',
  Valve: 'The solenoid’s lag, 0–300 ms: a first-order time constant the opening follows. 0 is a step.',
  Pump: 'The pumps’ lag, 0.05–3 s, geometrically: how slowly each head follows its command.',
  Wind: 'The mean wind, −10 to +10 m/s. The beads feel it through quadratic drag, the spray through linear drag; the intact column does not.',
  Gust: 'Three travelling gusts frozen into the wind, amplitude Gust × (2 + |Wind|) m/s. They tear the mist screen.',
  Screen: 'The mist bar’s flow: how thick the screen of fine spray is. 0 turns it off.',
  View: 'The frame’s height, 1–40 m, geometrically. The waterline sits 8% up the frame.',
  Lamps: 'The lamps under each nozzle: White, Warm, Rainbow (round the wheel by position), Duo. SW Fountain only.',
  Exposure: 'The camera, ±2 stops (× 1/4 to × 4). SW Fountain only.',
  Mix: 'SW Fountain Over only: the projection against the clip. At 1 the frame is the clip times the water, black where there is no water; at 0 the clip comes through untouched.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed). Shared names whose declarations
// agree are one row; a name only one plugin declares carries `only`.
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
};

/** The kit reads these from the query string itself, and this page reads `plugin`, `bpm` and `kicks`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'plugin', 'bpm', 'kicks']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

const onPanel = (d) => d.group !== 'About' && KIT_TYPES[d.type] !== undefined;

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return 0;
    default: return d.default;
  }
}

const sameDeclaration = (a, b) => JSON.stringify({ ...a, index: 0 }) === JSON.stringify({ ...b, index: 0 });

const PARAMS = [];
{
  const over = DECLARED.over.list.filter(onPanel);
  const source = DECLARED.source.list.filter(onPanel);
  // The source's order, then anything only the Over declares, after the last
  // row of its own group (Mix, after View's).
  for (const s of source) {
    const d = over.find((x) => x.name === s.name);
    const p = { id: addressOf(s.name), name: s.name, type: KIT_TYPES[s.type], group: s.group, hint: HINTS[s.name], ff: { source: s, over: d && sameDeclaration(s, d) ? d : null } };
    if (!p.ff.over) p.only = 'source';
    if (d && !p.ff.over) throw new Error(`${s.name}: the two plugins declare it differently, and this page has no row for that`);
    PARAMS.push(p);
  }
  for (const d of over) {
    if (source.some((x) => x.name === d.name)) continue;
    const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: { source: null, over: d }, only: 'over' };
    let at = PARAMS.length;
    for (let i = PARAMS.length - 1; i >= 0; i -= 1) if (PARAMS[i].group === d.group) { at = i + 1; break; }
    PARAMS.splice(at, 0, p);
  }
  for (const p of PARAMS) {
    const d = p.ff.source ?? p.ff.over;
    if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
    if (d.type === FF_TYPE_STANDARD) p.display = (v) => readout(d, p.only ?? currentVariant())(v);
  }
}

/** Each constructor's defaults, by kit id; a row the plugin lacks keeps the other's. */
const DEFAULTS = { source: {}, over: {} };
for (const p of PARAMS) {
  for (const variant of ['source', 'over']) {
    const d = p.ff[variant] ?? p.ff.source ?? p.ff.over;
    DEFAULTS[variant][p.id] = kitDefault(d);
  }
}

/** A declaration's host index in a plugin, or -1. */
const indexIn = (p, variant) => p.ff[variant]?.index ?? -1;

/** The presets, as kit values: options to their element's slot, the rest as converted. */
function kitPresets(variant) {
  const out = {};
  for (const [title, spec] of DECLARED[variant].presetValues) {
    const values = {};
    for (const [name, value] of Object.entries(spec)) {
      const p = PARAMS.find((x) => x.name === name);
      if (!p) throw new Error(`preset “${title}”: no control called ${name}`);
      if (p.type === 'option') {
        const slot = p.elements.indexOf(value);
        if (slot < 0) throw new Error(`preset “${title}”: ${name} has no element ${value}`);
        values[p.id] = slot;
      } else {
        if (!Number.isFinite(value)) throw new Error(`preset “${title}”: ${name} has no conversion`);
        values[p.id] = value;
      }
    }
    out[title] = values;
  }
  return out;
}

const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
for (const p of PARAMS) p.default = DEFAULTS[initialVariant][p.id];

//===========================================================================
// The host's tempo and the Audio buffer. Resolume sends its BPM and bar phase
// every frame (SetBeatInfo) and fills the Audio buffer with its FFT; here the
// page is the host. The bar phase runs on the page's clock at the tempo in
// the host row, in 4/4, continuous across a tempo change, and stands still
// when paused.
//===========================================================================

const TEMPOS = [60, 90, 100, 120, 128, 140, 150, 174];
const AUDIO = [
  { id: 'none', name: 'Nothing (a browser has no audio)' },
  { id: 'kicks', name: 'fttest’s test kicks (not music)' },
];
const host = {
  bpm: TEMPOS.includes(Number(query.get('bpm'))) ? Number(query.get('bpm')) : 120,
  phase: 0,
  lastTime: null,
  audio: query.get('kicks') === '1' ? 'kicks' : 'none',
};

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;
function currentVariant() { return mounted?.state.variant ?? initialVariant; }

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    default: return Math.fround(v);
  }
}

function createRenderer(gl) {
  // Additive blending into the plugin's RGBA32F water buffer. The kit has
  // enabled EXT_color_buffer_float (needFloat) and EXT_float_blend
  // (needFloatBlend) on this context, or refused to start.
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);
  core._ft_prepare_log();

  let instance = 0;
  let variant = null;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let audioWritten = null;
  let presses = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(which, width, height) {
    if (instance) core._ft_delete(instance);
    instance = core._ft_new(which === 'over' ? 1 : 0);
    variant = which;
    pushed = new Map();
    frames = 0;
    audioWritten = null;
    host.lastTime = null;
    if (!core._ft_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`${which === 'over' ? 'SW Fountain Over' : 'SW Fountain'} could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const d = p.ff[variant];
      if (!d) continue;
      const value = hostValue(d, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ft_set_float(instance, d.index, value);
    }
  }

  return {
    render({ input, params, width, height, time, variant: wanted }) {
      core.GL.makeContextCurrent(handle);
      if (!instance || wanted !== variant) start(wanted ?? 'source', width, height);
      push(params);

      // The host's beat, every frame.
      if (host.lastTime !== null && time > host.lastTime) host.phase = (host.phase + (time - host.lastTime) * host.bpm / 240) % 1;
      host.lastTime = time;
      core._ft_set_beat(instance, host.bpm, host.phase);

      // The Audio buffer: nothing, or the harness's spectrum for the frame of
      // its 60 fps clock this moment falls in.
      if (host.audio === 'kicks') {
        core._ft_audio_kicks(instance, Math.floor(time * 60 + 1e-6));
        audioWritten = 'kicks';
      } else if (audioWritten !== 'none') {
        core._ft_audio_silence(instance);
        audioWritten = 'none';
      }

      const clip = variant === 'over' ? glName(input.texture) : 0;
      if (!core._ft_process(instance, time, clip, input.width, input.height)) {
        throw new GLError('The plugin’s ProcessOpenGL failed.');
      }
      // For anything checking the page from outside: frames drawn by this
      // instance, the shots the plugin has fired and its simulation clock.
      frames += 1;
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.shots = String(core._ft_shots(instance));
      gl.canvas.dataset.simTime = String(core._ft_sim_time(instance));
      gl.canvas.dataset.presses = String(presses);
    },
    /** FF_TYPE_EVENT: the press and the release, as the harness's Press() sends them. */
    press(name) {
      const p = PARAMS.find((x) => x.name === name);
      const d = p?.ff[variant];
      if (!instance || !d) return;
      core._ft_set_float(instance, d.index, 1);
      core._ft_set_float(instance, d.index, 0);
      presses += 1;
    },
    stats() {
      if (!instance) return null;
      return {
        variant,
        simTime: core._ft_sim_time(instance),
        nozzles: core._ft_nozzles(instance),
        beads: core._ft_beads(instance),
        spray: core._ft_spray(instance),
        shots: core._ft_shots(instance),
        breakup: core._ft_breakup_age(instance),
        ppm: core._ft_pixels_per_metre(instance),
        clock: core._ft_clock_scale(instance),
        onsets: core._ft_onsets(instance),
        low: core._ft_band_level(instance, 0),
        mid: core._ft_band_level(instance, 1),
        peak: core._ft_command_to_peak(instance, 0),
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._ft_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. fountain-core.wasm is source/Fountain.cpp — the plugin class: its constructor, clock and seconds-or-milliseconds vote, the Audio buffer’s analyser, the beat and Lead, the instance buffers and every uniform — and everything it calls (Sim, Water, Layout, Controls, Audio, PassBuffer, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). All the physics — pumps, valves, parcels, Rayleigh’s breakup, the beads’ RK4 under drag, the spray and the gusts — is that C++, in double. Only the two registration files are left out; the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and Assemble() join the pieces in, and the repository’s verify script fails if a character differs. At start-up the page also requires each of the eight stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points. The water is added into a 32-bit float buffer by blending, as in the plugin, which needs EXT_color_buffer_float and EXT_float_blend; a browser without either is refused rather than handed something smaller.',
  'No audio. The plugin declares Audio, an FF_TYPE_BUFFER that Resolume fills with its 64-bin FFT every frame; the kit has no control for one and a browser has no Resolume FFT, so it is not on the panel. The host row under the picture chooses what this page writes into it. Its default, nothing, is exactly a layer with no audio routed: every band level is 0, so the pumps run at (1 − Response) of their head (half, at the defaults), Fire On Onset never fires and Sensitivity changes nothing. Its other choice is fttest’s own test signal (--kicks, copied into the page’s glue from tools/fttest/main.cpp and held to it by the verify script): a bass bed, a hi-hat of random level, a kick every half second, soft and hard by turns, and a ghost note between that only a high Sensitivity hears. It is a synthetic spectrum, not music, written as the harness writes it for each frame of its 60 fps clock — here for the frame of that clock each page frame falls in, so at 120 Hz a spectrum is sent twice. Nothing here has heard a sound, and the plugin has not either.',
  'The tempo and bar phase are the page’s. Resolume sends its BPM and bar phase every frame; here the tempo is chosen in the host row (120 BPM by default, which is also what the plugin assumes when a host sends none) and the bar phase runs on the page’s clock, in 4/4, continuous across a tempo change, standing still when paused. At the defaults the shooters fire on every beat, twice a second at 120 BPM.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime. The plugin’s own vote against the wall clock settles the unit (seconds, here within four frames); until it does, the plugin runs on the wall clock, and the frame it settles on advances nothing — both the plugin’s own rules. Restart is a backward jump, which the plugin takes as no time passing, so the water carries on where it was; switching the Plugin dropdown is what starts a new fountain. Step advances the clock 1/60 s.',
  'Paused, a moved control redraws the frame without advancing the water. A Fire press while paused is held: a frame where no time has passed launches nothing, so the plugin keeps the press and fires it at the start of the next frame that has time, the first after Play. It does the same in a host that renders a frame without moving its clock.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. The physics does not depend on it — the intact column is closed form on a 240 Hz grid of launch times, beads and spray are stepped at most 1/240 s at a time, and a shot opens at the moment inside the frame its beat fell — but which instants are drawn does.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its exp, sin and pow are musl’s, not Apple’s, so the simulation’s doubles can differ from the plugin built for a Mac in the last digits; the spray’s PCG hashes are integer arithmetic and cannot. What reaches the GPU is rounded to float, as in the plugin, and then rasterised by this browser’s GPU, not the Mac’s. Checked once against the plugin’s harness on the same settings and a 60 fps clock, with the valves held open, a frame of this page agreed with the harness’s to one 8-bit code, for both plugins (one frame apart: the frame the plugin’s clock settles on advances nothing here, and the harness skips the vote).',
  'SW Fountain Over’s clip is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the plugin treats its input as straight colour times alpha (the projector’s light is rgb × a), so over a half-transparent edge the light would be counted twice. The page’s own clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them; an image of your own with transparency would show the double count at its edges. In Arena the effect gets the composition’s own frame; here its whole texture is used, unpadded.',
  'Fire is FF_TYPE_EVENT; the button sends the press and the release, 1 then 0, as the harness’s Press() does. The About block (a credit line and its buttons) is declared by the plugin and left off this panel. Audio, an FF_TYPE_BUFFER, is the host row’s, above.',
  'Switching the Plugin dropdown is a new instance at that constructor’s defaults, as in Resolume. The two declare the same controls with the same defaults but for Lamps and Exposure (the source’s) and Mix (the Over’s).',
  'Nothing here is measured. The plugin itself has loaded and rendered in Resolume Arena on Windows, on software rendering, and has never heard a sound in a host (its README’s Status says what was seen). Its harness, fttest, drives the same class headlessly on a synthetic clock and reads the water back off the pixels: a jet tops out at v²/2g to 0.07 px, an arc falls on its parabola to 0.2 px, beads stand one Rayleigh wavelength apart to 0.01 px, Lead peaks the water on the beat to 0.24 frames, and 18 wrong models and 17 one-character mutants are caught. That harness, not this page, is the reason to believe the fountain.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Fountain',
  pluginId: 'FT01 · FT02',
  kind: ['source', 'effect'],
  tagline:
    'A musical fountain for Resolume. The beat opens solenoid valves, which take their time to open; a pump’s head follows the music’s level; water leaves each nozzle at √(2gH) and flies, intact in closed form until Rayleigh’s instability pinches it into beads, the beads slowed by the air and carried by the wind, and a mist bar sprays a screen the gusts tear. SW Fountain lights it all with lamps on a night sky; SW Fountain Over projects your clip onto the same water, so the clip shows only where there is water.',
  repo: 'https://github.com/stoatworks-labs/fountain',

  blurb:
    'It is the plugin’s own C++ — the plugin class, its whole simulation in double, the audio analyser and the clock — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: the tempo is the page’s, a browser has no audio, so the Audio buffer holds nothing unless you choose the plugin harness’s test kicks, and SW Fountain Over runs on a generated clip.',

  // The water is added by blending into a 32-bit float buffer, as in the plugin.
  needFloat: true,
  needFloatBlend: true,
  // Both plugins write alpha 1 at their defaults (the Over: mix( clip alpha, 1, Mix )).
  showBackdrop: false,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Fountain (source)', hint: 'FT01, FF_SOURCE: the water, lit by its lamps on a night sky. Reads no clip.' },
      { id: 'over', name: 'SW Fountain Over (effect)', hint: 'FT02, FF_EFFECT: the clip, projected from the camera onto the same fountain’s water.' },
    ],
  },

  // For SW Fountain Over only, and all opaque: the kit's 'alpha' clip is left
  // out because the kit's premultiplied colour is not the straight colour the
  // plugin expects (see the notes at the foot).
  sources: ['scene', 'bars', 'grid', 'spot', 'ramp'],

  params: PARAMS,
  presets: kitPresets(initialVariant),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// Which controls belong to which plugin, Fire, the host row and the line
// under the picture.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin and the host row.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    q.delete('fire');
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (host.bpm !== 120) q.set('bpm', String(host.bpm));
    if (host.audio === 'kicks') q.set('kicks', '1');
    return q;
  };

  // Fire: a press, sent on the next frame. The kit's toggle is replaced below;
  // a ?fire=1 in a URL presses it once on load (for checking the page headless).
  if (params.get('fire') > 0.5) {
    params.set('fire', 0, { silent: true });
    queueMicrotask(() => renderer?.press('Fire'));
  }

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(which) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === which);
    const effect = which === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const which = mounted.state.variant;
    if (which !== shownVariant) {
      shownVariant = which;
      // A new instance: that constructor's defaults, for Defaults too.
      params.defaults = { ...DEFAULTS[which] };
      params.reset();
    }
    if (!embed) showForVariant(which);
  });

  if (!embed) {
    showForVariant(shownVariant);

    // FF_TYPE_EVENT: a button that sends the press and the release.
    for (const p of PARAMS) {
      if ((p.ff.source ?? p.ff.over).type !== FF_TYPE_EVENT) continue;
      const row = rowOf(p);
      const toggle = row?.querySelector('.prow__toggle');
      if (!toggle) continue;
      const press = document.createElement('button');
      press.type = 'button';
      press.className = 'prow__toggle';
      press.style.textAlign = 'center';
      press.textContent = p.name;
      press.title = p.hint ?? '';
      press.addEventListener('click', () => {
        renderer?.press(p.name);
        mounted.redraw();
      });
      toggle.replaceWith(press);
    }

    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-ins for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, select, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the select with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  select.style.maxWidth = '100%';
  select.style.minWidth = '0';
  wrap.append(span, select);
  return wrap;
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'fountain-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'Stand-ins for what Resolume would hand the plugin. None of these is a plugin control.';

  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  tempo.id = 'fountain-tempo';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.bpm = Number(tempo.value);
    mounted.redraw();
  });

  const audio = document.createElement('select');
  audio.className = 'transport__select';
  audio.id = 'fountain-audio';
  for (const a of AUDIO) audio.append(new Option(a.name, a.id));
  audio.value = host.audio;
  audio.addEventListener('change', () => {
    host.audio = audio.value;
    mounted.redraw();
  });

  row.append(
    tag,
    field('Tempo', tempo, 'Resolume’s BPM, sent with its bar phase every frame (SetBeatInfo). The bar phase runs at it, in 4/4, on the page’s clock.'),
    field('Audio buffer', audio, 'What Resolume would write into the plugin’s Audio buffer, its 64-bin FFT. A browser has none, so: nothing (a layer with no audio routed), or the plugin harness’s synthetic test kicks — a spectrum written by fttest --kicks, a kick every half second, not music.'),
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The line under the picture: the plugin's own state, through the accessors
// its harness reads. It reports; it measures nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status';
  line.id = 'fountain-status';
  line.setAttribute('aria-live', 'off');
  const log = document.createElement('p');
  log.className = 'stage__status';
  log.id = 'fountain-log';
  log.setAttribute('aria-live', 'off');
  log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
  log.style.fontSize = '12px';
  log.style.whiteSpace = 'pre-wrap';
  stage.append(line, log);

  const shotsPerSecond = { Steady: 0, Beat: 1, 'Half Beat': 2, '2 Beats': 0.5, Bar: 0.25, Onset: null };
  const fireOn = PARAMS.find((p) => p.name === 'Fire On');
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    const mode = fireOn ? fireOn.elements[Math.round(mounted.params.get(fireOn.id))] : '';
    const perBeat = shotsPerSecond[mode];
    const rate = perBeat === null
      ? (host.audio === 'kicks' ? 'the shooters fire on the onsets the analyser hears' : 'Onset with nothing in the Audio buffer: the shooters never fire')
      : perBeat === 0
        ? 'the shooters’ valves held open'
        : `the shooters fire ${perBeat * host.bpm / 60} times a second at ${host.bpm} BPM`;
    const unit = s.clock === 1 ? 'seconds' : s.clock === 0.001 ? 'milliseconds' : 'not settled yet';
    line.textContent = `${s.variant === 'over' ? 'SW Fountain Over' : 'SW Fountain'}: ${s.simTime.toFixed(2)} s of water`
      + ` · ${s.nozzles} nozzles, ${s.beads} beads, ${s.spray} spray parcels in the air`
      + ` · ${s.shots} shots, ${rate}`
      + ` · columns pinch into beads at ${s.breakup.toFixed(2)} s · ${s.ppm.toFixed(1)} px a metre`
      + ` · host clock read in ${unit}`
      + (host.audio === 'kicks' ? ` · ${s.onsets} onsets heard, Low band ${s.low.toFixed(2)}, Mid ${s.mid.toFixed(2)}` : ' · Audio buffer empty');
    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) fountain: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

export { renderer, mounted, PARAMS, DECLARED };
