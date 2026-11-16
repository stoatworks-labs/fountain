#pragma once

#include <string>

/**
    The passes. The CPU has already placed every drop of water in metres; the
    GPU only turns that into optical depth and then into a picture.

    1. **water** -- three instanced draws, additively, into one RGBA32F buffer
       the size of the output: (sum of tau x lamp colour, sum of tau).
       - *segments*: the intact columns. Each segment's coverage of a pixel is
         box-filtered across (the strip [-w, w] against the pixel's width) and
         along (the segment's length against it), so the coverage summed over
         pixels is the segment's area at any raster, and a column's end is a
         one-pixel linear ramp centred on the end itself.
       - *beads*: discs, a ramp one pixel wide at the edge, normalised so the
         coverage summed over pixels is the disc's area (a bead smaller than a
         pixel becomes a fainter half-pixel disc, not a lost one).
       - *spray*: normalised Gaussians of optical depth, widened by half a
         pixel in quadrature so a parcel narrower than a pixel keeps its
         integral.
       Optical depth of a jet or bead is water::kJetOpacity times its coverage;
       nothing below the waterline is drawn (the basin is in front of it).
    2. **composite** -- to the host. W = 1 - exp( -tau ). The source: the
       tau-weighted lamp colour times W times Exposure on black, the basin
       below the waterline with a dimmed mirror image of the water. The Over:
       the clip, as light from a projector at the camera, times W; Mix to the
       clip, and alpha mix( src.a, 1, Mix ).

    The fragment shaders are assembled from pieces at InitGL by `Assemble()`;
    tools/glslc.sh's ASSEMBLED table mirrors it. No time and no trig reach a
    shader: positions come from the CPU, in metres relative to the basin.
*/
namespace fountain::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kSegmentVertex;
extern const char* const kBeadVertex;
extern const char* const kSprayVertex;
extern const char* const kCommon;
extern const char* const kWaterCommon;
extern const char* const kSegmentFragment;
extern const char* const kBeadFragment;
extern const char* const kSprayFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Segments,
	Beads,
	Spray,
	Composite
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

/// The lamp table's size (Layout.h's kMaxNozzles).
constexpr int kMaxLamps = 40;

} // namespace fountain::shaders
