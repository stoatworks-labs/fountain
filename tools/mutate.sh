#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the flight, would pass every mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, 320x180, under a time limit (a mutant can hang: honeydew's
# lesson; a timeout counts as caught). (ferrofluid's script, fountain's
# mutants.)
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|vec2 a     = Origin + Ends.xy * Scale;|vec2 a     = Origin - Ends.xy * Scale;|--range|GLSL: a segment's first end mirrored through the basin's centre, + -> -"
	"source/Shaders.cpp|return tint * ( 1.0 - exp( -water.a ) );|return tint * ( 1.0 - exp( +water.a ) );|--apex|GLSL: the source's Beer-Lambert with the wrong sign, - -> +"
	"source/Shaders.cpp|vec2 c       = Origin + Bead.xy * Scale;|vec2 c       = Origin - Bead.xy * Scale;|--breakup|GLSL: a bead drawn mirrored through the basin's centre, + -> -"
	"source/Shaders.cpp|vec2 c      = Origin + Parcel.xy * Scale;|vec2 c      = Origin - Parcel.xy * Scale;|--wind|GLSL: a parcel of spray drawn mirrored, + -> -"
	"source/Shaders.cpp|float wet   = 1.0 - exp( -water.a );|float wet   = 2.0 - exp( -water.a );|--screen|GLSL: the Over lights dry air, 1 -> 2"
	"source/Shaders.cpp|mix( src.a, 1.0, MixAmount )|mix( src.a, 0.0, MixAmount )|--alpha|GLSL: the Over's alpha mixed toward 0, not 1, 1 -> 0"
	"source/Sim.cpp|default: return opening * std::sqrt( 2.0 * g * head );|default: return opening * std::sqrt( 3.0 * g * head );|--apex|C++: Torricelli's v = sqrt( 3 g H ), 2 -> 3"
	"source/Sim.cpp|x                  = n.x + speed * std::cos( angle ) * age;|x                  = n.x - speed * std::cos( angle ) * age;|--range|C++: an arc flying backwards, + -> -"
	"source/Sim.cpp|y                  = speed * std::sin( angle ) * age - 0.5 * g * age * age;|y                  = speed * std::sin( angle ) * age - 0.6 * g * age * age;|--flight|C++: g t^2 / 2 with 0.6 for the half, 5 -> 6"
	"source/Sim.cpp|return target + ( x - target ) * std::exp( -h / tau );|return target + ( x - target ) * std::exp( -h * tau );|--lag|C++: the solenoid's lag exponent h tau, / -> *"
	"source/Sim.cpp|apex            = tb + ( vt / g ) * std::atan( vb / vt );|apex            = tb - ( vt / g ) * std::atan( vb / vt );|--lead|C++: the bead's rise after pinch-off subtracted from the lead, + -> -"
	"source/Sim.cpp|s.x += ux * h + ( s.vx - ux ) * tau * ( 1.0 - e );|s.x -= ux * h + ( s.vx - ux ) * tau * ( 1.0 - e );|--wind|C++: the mist carried against the wind, + -> -"
	"source/Water.cpp|return kGravity / ( vt * vt );|return kGravity * ( vt * vt );|--drag|C++: the drag constant g v_t^2, / -> *"
	"source/Water.cpp|return std::max( 0.0, lnDisturbance ) / BreakupRate( a );|return std::max( 0.0, lnDisturbance ) * BreakupRate( a );|--breakup|C++: the breakup age ln( a / delta0 ) x q, / -> *"
	"source/Fountain.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--clock|C++: the clock-unit fallback upside down, < -> >"
	"source/Audio.cpp|			flux = 0.0f;|			flux = 1.0f;|--late|C++: the cold start primed with a flux of 1, not 0, 0 -> 1"
	"source/Sim.cpp|	heldPresses += in.presses;|	heldPresses  = in.presses;|--clock|C++: a press on a frame with no time in it forgotten by the next, += -> ' ='"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target fttest -j8 >/dev/null 2>&1
	status=0
	limit 300 "$tree/build/fttest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
