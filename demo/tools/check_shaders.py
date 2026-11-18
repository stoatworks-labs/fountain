"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

The page at fountain-demo.stoatworks-labs.com carries three things that are
copies of this repository, and copies drift quietly: a fountain whose beads
are mirrored, or whose Over adds the water to the clip, still looks like a
fountain.

1. **The GLSL.** `demo/shaders.js` holds the ten GLSL pieces of
   `source/Shaders.cpp` (four vertex stages, kCommon, kWaterCommon and the four
   fragment bodies) plus kVersion, AND the order the plugin joins them in:
   `AssembleVertex()` and `Assemble()` build each pass's two stages, and
   Fountain.cpp's InitGL compiles every pass from them. This script reads that
   order out of the C++ and writes it into shaders.js as `ASSEMBLY`. The page
   builds its expected text for every stage from that table, and at run time
   REQUIRES the text the compiled plugin hands to glShaderSource to equal one of
   them before the kit's port() is applied (demo/wasm/gl_shim.cpp). The pieces
   are compared exactly -- no whitespace normalisation, no comment stripping: a
   comment updated on one side only is drift worth catching. Then:

     - every `const char* const k... = R"(` piece in Shaders.cpp must be one
       this script knows, so a new piece cannot be added to the plugin and
       silently left out of the page;
     - every value of `enum class Pass` in Shaders.h must have a case in both
       switches, InitGL must compile every one of them through those two
       functions, and ASSEMBLY must list exactly those stages, piece for piece,
       in the plugin's order;
     - and shaders.js must be byte for byte what `--write` produces, so
       nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **The WebAssembly.** `demo/fountain-core.wasm` is the plugin's C++ compiled
   by `demo/tools/build-wasm.sh`, and committed, because the deploy has no
   build step. Building it needs emscripten, which verify.sh cannot assume, so
   this checks the next best thing: `demo/wasm/inputs.sha256`, written by the
   build, records the hash of every file that went in (every source/*.h, the
   nine plugin .cpp files, the SDK files, the glue, the shim and the build
   script) and of both outputs, and the SDK commit. A source file changed since
   the last build -- or an output that is not the one the build wrote -- fails
   here, and the fix is to rerun build-wasm.sh.

3. **The harness's music.** The page's host row can put `fttest --kicks`'s
   spectrum in the Audio buffer. demo/wasm/glue.cpp carries fttest's own
   `pcg`, `hash01`, `writeSpectrum` and `feedKicks` for it, and each must be
   the text of tools/fttest/main.cpp's, character for character.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js, the page around the
plugin: its panel is built from the plugin's own declarations at run time, but
its read-out units, its tooltips and its layout are the page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("SEGMENT_VERTEX", "kSegmentVertex"),
    ("BEAD_VERTEX", "kBeadVertex"),
    ("SPRAY_VERTEX", "kSprayVertex"),
    ("COMMON", "kCommon"),
    ("WATER_COMMON", "kWaterCommon"),
    ("SEGMENT_FRAGMENT", "kSegmentFragment"),
    ("BEAD_FRAGMENT", "kBeadFragment"),
    ("SPRAY_FRAGMENT", "kSprayFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# fttest's functions the page's glue carries, for the host row's "harness kicks".
HARNESS = ["pcg", "hash01", "writeSpectrum", "feedKicks"]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/fountain-core.js", "demo/fountain-core.wasm")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def switch_of(cpp, function):
    """{pass: [pieces]} from `std::string <function>( Pass pass )`'s switch,
    whose every case is `return std::string( kVersion ) + kA + kB;`."""
    body = re.search(r"std::string " + function + r"\( Pass pass \)\s*\{(.*?)\n\}", cpp, re.S)
    if body is None:
        raise Stale(f"no {function}( Pass pass ) in source/Shaders.cpp")
    if body.group(1).count("switch") != 1:
        raise Stale(f"{function}() is not one switch")
    cases = re.findall(r"case Pass::(\w+):\s*return (.*?);", body.group(1), re.S)
    out = {}
    for name, expression in cases:
        terms = [t.strip() for t in expression.split("+")]
        pieces = []
        for term in terms:
            m = re.fullmatch(r"(?:std::string\(\s*)?(k\w+)(?:\s*\))?", term)
            if m is None:
                raise Stale(f"{function}()'s case {name} joins `{term}`, which is not a piece")
            pieces.append(m.group(1))
        out[name] = pieces
    return out


def assembly(cpp, plugin_cpp, shaders_h):
    """The plugin's own assembly: every pass from the Pass enum, its vertex
    stage from AssembleVertex() and its fragment stage from Assemble(), as
    InitGL compiles them. In JS names, keyed `<pass>_vertex` / `<pass>_fragment`."""
    enum = re.search(r"enum class Pass\s*\{(.*?)\};", shaders_h, re.S)
    if enum is None:
        raise Stale("no `enum class Pass` in source/Shaders.h")
    passes = [p.strip() for p in enum.group(1).split(",") if p.strip()]

    vertex = switch_of(cpp, "AssembleVertex")
    fragment = switch_of(cpp, "Assemble")
    for label, table in (("AssembleVertex", vertex), ("Assemble", fragment)):
        if sorted(table) != sorted(passes):
            raise Stale(f"{label}()'s cases ({sorted(table)}) are not the Pass enum ({passes})")

    # InitGL: a Stage table naming every pass, each compiled from the two
    # functions above and nothing else.
    if "shaders::AssembleVertex( stage.pass )" not in plugin_cpp or "shaders::Assemble( stage.pass )" not in plugin_cpp:
        raise Stale("Fountain.cpp's InitGL no longer compiles each stage from AssembleVertex( stage.pass ) and Assemble( stage.pass )")
    stages = re.search(r"const Stage stages\[\] = \{(.*?)\};", plugin_cpp, re.S)
    if stages is None:
        raise Stale("no `const Stage stages[]` in Fountain.cpp's InitGL")
    compiled = re.findall(r"shaders::Pass::(\w+)", stages.group(1))
    if compiled != passes:
        raise Stale(f"InitGL compiles {compiled}, the Pass enum is {passes}")

    out = []
    for name in passes:
        for stage, table in (("vertex", vertex), ("fragment", fragment)):
            symbols = table[name]
            for symbol in symbols:
                if symbol not in JS_NAME:
                    raise Stale(f"{name} {stage} uses {symbol}, a piece this script does not copy")
            out.append((f"{name.lower()}_{stage}", [JS_NAME[s] for s in symbols]))
    return out


def render(cpp, plugin_cpp, shaders_h):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// every pass as Fountain.cpp's InitGL compiles it, its vertex stage from\n"
        "// Shaders.cpp's AssembleVertex() and its fragment stage from Assemble().\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in assembly(cpp, plugin_cpp, shaders_h):
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    """kVersion's text as shaders.js carries it: the C literal's own escapes,
    which mean the same in a JS string."""
    match = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    return None if match is None else match.group(1)


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, plugin_cpp, shaders_h, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<24} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<24} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, plugin_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in table:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<24} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in table)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1

    # Anything else typed into the file.
    try:
        expected = render(cpp, plugin_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at column
    0 (its return type first) to the first `}` at column 0 after it."""
    match = re.search(r"^[A-Za-z_][\w:<> ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/fttest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<24} matches tools/fttest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/fttest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          fttest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    return problems


def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/fountain-core.wasm was built")
            problems += 1

    # A source file added since the build is an input the build never saw.
    for name in sorted(os.listdir(os.path.join(REPO, "source"))):
        if name in ("SourcePlugin.cpp", "EffectPlugin.cpp"):
            continue  # the registrations: the page constructs the plugin itself
        if (name.endswith(".h") or name.endswith(".cpp")) and f"source/{name}" not in recorded:
            print(f"FAIL  source/{name} is newer than the .wasm's manifest -- rebuild")
            problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<24} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    plugin_cpp = read("source", "Fountain.cpp")
    shaders_h = read("source", "Shaders.h")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, plugin_cpp, shaders_h)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, plugin_cpp, shaders_h, js)
    problems += check_harness(read("tools", "fttest", "main.cpp"), read("demo", "wasm", "glue.cpp"))
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    stages = len(assembly(cpp, plugin_cpp, shaders_h))
    print(f"all {len(SHADERS) + 1} shader pieces and the order all {stages} stages join them in are the plugin's,"
          f" the glue's {len(HARNESS)} harness functions are fttest's, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
