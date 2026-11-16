#include "Shaders.h"

namespace fountain::shaders
{

const char* const kVersion = "#version 410 core\n";

//---------------------------------------------------------------------------
// Vertex shaders.
//---------------------------------------------------------------------------

/// The composite's full-screen triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// One segment of intact column per instance: a quad one pixel past the
/// segment's ends and its widest half-width.
const char* const kSegmentVertex = R"(
layout( location = 0 ) in vec4 Ends;  // x0 y0 x1 y1, metres
layout( location = 1 ) in vec4 Widths;// half-width at each end, metres; lamp
uniform vec2 Origin;                  // the basin's centre on the waterline, pixels
uniform float Scale;                  // pixels a metre
uniform vec2 Size;                    // the buffer, pixels
flat out vec4 vEnds;
flat out vec3 vWidth;
const int kCorner[ 6 ] = int[ 6 ]( 0, 1, 2, 2, 1, 3 );
void main()
{
	vec2 a     = Origin + Ends.xy * Scale;
	vec2 b     = Origin + Ends.zw * Scale;
	vec2 d     = b - a;
	float span = length( d );
	vec2 u     = span > 1e-6 ? d / span : vec2( 1.0, 0.0 );
	vec2 n     = vec2( -u.y, u.x );
	float w    = max( Widths.x, Widths.y ) * Scale + 1.0;
	int k      = kCorner[ gl_VertexID % 6 ];
	float s    = ( k == 1 || k == 3 ) ? span + 1.0 : -1.0;
	float t    = k >= 2 ? w : -w;
	vec2 p     = a + u * s + n * t;
	gl_Position = vec4( p / Size * 2.0 - 1.0, 0.0, 1.0 );
	vEnds  = vec4( a, b );
	vWidth = vec3( Widths.xy * Scale, Widths.z );
}
)";

/// One bead per instance.
const char* const kBeadVertex = R"(
layout( location = 0 ) in vec4 Bead;// x y radius (metres), lamp
uniform vec2 Origin;
uniform float Scale;
uniform vec2 Size;
flat out vec4 vBead;
const int kCorner[ 6 ] = int[ 6 ]( 0, 1, 2, 2, 1, 3 );
void main()
{
	vec2 c       = Origin + Bead.xy * Scale;
	float radius = Bead.z * Scale;
	float reach  = max( radius, 0.5 ) + 1.5;
	int k        = kCorner[ gl_VertexID % 6 ];
	vec2 p       = c + vec2( ( k == 1 || k == 3 ) ? reach : -reach, k >= 2 ? reach : -reach );
	gl_Position  = vec4( p / Size * 2.0 - 1.0, 0.0, 1.0 );
	vBead = vec4( c, radius, Bead.w );
}
)";

/// One parcel of spray per instance.
const char* const kSprayVertex = R"(
layout( location = 0 ) in vec4 Parcel;// x y sigma (metres), tau x area (m^2)
layout( location = 1 ) in vec4 Extra; // lamp
uniform vec2 Origin;
uniform float Scale;
uniform vec2 Size;
flat out vec4 vParcel;
flat out float vLamp;
const int kCorner[ 6 ] = int[ 6 ]( 0, 1, 2, 2, 1, 3 );
void main()
{
	vec2 c      = Origin + Parcel.xy * Scale;
	float sigma = sqrt( Parcel.z * Parcel.z * Scale * Scale + 0.25 );
	float reach = 3.5 * sigma;
	int k       = kCorner[ gl_VertexID % 6 ];
	vec2 p      = c + vec2( ( k == 1 || k == 3 ) ? reach : -reach, k >= 2 ? reach : -reach );
	gl_Position = vec4( p / Size * 2.0 - 1.0, 0.0, 1.0 );
	vParcel = vec4( c, sigma, Parcel.w * Scale * Scale );
	vLamp   = Extra.x;
}
)";

//---------------------------------------------------------------------------
// Fragment pieces.
//---------------------------------------------------------------------------

/// sRGB both ways (IEC 61966-2-1).
const char* const kCommon = R"(
vec3 toLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
)";

/// What every water draw shares: the frame's mapping and the lamps.
const char* const kWaterCommon = R"(
uniform vec2 Origin;
uniform float Scale;
uniform float Opacity;             // optical depth per unit coverage (jets, beads)
uniform vec3 Lamps[ 40 ];
out vec4 FragColor;
bool underwater( vec2 p )
{
	return p.y < Origin.y;
}
vec4 deposit( float tau, float lamp )
{
	return vec4( Lamps[ int( lamp + 0.5 ) ] * tau, tau );
}
)";

const char* const kSegmentFragment = R"(
flat in vec4 vEnds;
flat in vec3 vWidth;
void main()
{
	vec2 p = gl_FragCoord.xy;
	if( underwater( p ) )
		discard;
	vec2 a     = vEnds.xy;
	vec2 d     = vEnds.zw - a;
	float span = length( d );
	vec2 u     = span > 1e-6 ? d / span : vec2( 1.0, 0.0 );
	vec2 q     = p - a;
	float s    = dot( q, u );
	float t    = dot( q, vec2( -u.y, u.x ) );
	float hw   = mix( vWidth.x, vWidth.y, clamp( s / max( span, 1e-6 ), 0.0, 1.0 ) );
	// The pixel's width against the strip, and its length against the segment.
	float across = clamp( min( t + 0.5, hw ) - max( t - 0.5, -hw ), 0.0, 1.0 );
	float along  = clamp( min( s + 0.5, span ) - max( s - 0.5, 0.0 ), 0.0, 1.0 );
	float tau    = Opacity * across * along;
	if( tau <= 0.0 )
		discard;
	FragColor = deposit( tau, vWidth.z );
}
)";

const char* const kBeadFragment = R"(
flat in vec4 vBead;
void main()
{
	vec2 p = gl_FragCoord.xy;
	if( underwater( p ) )
		discard;
	float radius = vBead.z;
	float shown  = max( radius, 0.5 );
	// The ramp clamp( R + 1/2 - d ) sums to pi ( R^2 + 1/12 ) over the plane:
	// scaled to the bead's own area, pi r^2.
	float weight = radius * radius / ( shown * shown + 1.0 / 12.0 );
	float cover  = weight * clamp( shown + 0.5 - distance( p, vBead.xy ), 0.0, 1.0 );
	float tau    = Opacity * cover;
	if( tau <= 0.0 )
		discard;
	FragColor = deposit( tau, vBead.w );
}
)";

const char* const kSprayFragment = R"(
flat in vec4 vParcel;
flat in float vLamp;
const float kTwoPi = 6.28318530717958648;
void main()
{
	vec2 p = gl_FragCoord.xy;
	if( underwater( p ) )
		discard;
	float sigma = vParcel.z;
	vec2 d      = p - vParcel.xy;
	float r2    = dot( d, d ) / ( sigma * sigma );
	if( r2 > 12.25 )
		discard;
	float tau = vParcel.w * exp( -0.5 * r2 ) / ( kTwoPi * sigma * sigma );
	FragColor = deposit( tau, vLamp );
}
)";

const char* const kCompositeFragment = R"(
uniform sampler2D Accum;
uniform sampler2D InputTexture;
uniform ivec2 ViewOrigin;     // the host viewport's corner
uniform ivec2 Size;           // the output, pixels
uniform int IsOver;
uniform float Exposure;
uniform float MixAmount;
uniform float Waterline;      // pixel row of the waterline, from the bottom
uniform int Additive;         // the --screen negative control only
out vec4 FragColor;
const vec3 kBasin   = vec3( 0.0035, 0.0050, 0.0080 );
const float kMirror = 0.3;
vec3 lit( vec4 water )
{
	vec3 tint = water.a > 0.0 ? water.rgb / water.a : vec3( 0.0 );
	return tint * ( 1.0 - exp( -water.a ) );
}
void main()
{
	ivec2 at   = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 water = texelFetch( Accum, at, 0 );
	if( IsOver == 1 )
	{
		vec4 src = texelFetch( InputTexture, at, 0 );
		if( MixAmount <= 0.0 )
		{
			FragColor = src;
			return;
		}
		// The projector is at the camera: the clip pixel under this one is the
		// light that lands here, and only water sends it back.
		vec3 light  = toLinear( src.rgb ) * src.a;
		float wet   = 1.0 - exp( -water.a );
		vec3 shown  = toCode( Additive == 1 ? light + vec3( wet ) : light * wet );
		FragColor   = vec4( mix( src.rgb, shown, MixAmount ), mix( src.a, 1.0, MixAmount ) );
		return;
	}
	vec3 colour = lit( water ) * Exposure;
	float y     = float( at.y ) + 0.5;
	if( y < Waterline )
	{
		int mirrored = int( floor( 2.0 * Waterline - y ) );
		vec3 seen    = vec3( 0.0 );
		if( mirrored >= 0 && mirrored < Size.y )
			seen = lit( texelFetch( Accum, ivec2( at.x, mirrored ), 0 ) ) * Exposure * kMirror;
		colour = kBasin + seen;
	}
	FragColor = vec4( toCode( colour ), 1.0 );
}
)";

std::string AssembleVertex( Pass pass )
{
	switch( pass )
	{
	case Pass::Segments: return std::string( kVersion ) + kSegmentVertex;
	case Pass::Beads: return std::string( kVersion ) + kBeadVertex;
	case Pass::Spray: return std::string( kVersion ) + kSprayVertex;
	case Pass::Composite: return std::string( kVersion ) + kQuadVertex;
	}
	return std::string();
}

std::string Assemble( Pass pass )
{
	switch( pass )
	{
	case Pass::Segments: return std::string( kVersion ) + kWaterCommon + kSegmentFragment;
	case Pass::Beads: return std::string( kVersion ) + kWaterCommon + kBeadFragment;
	case Pass::Spray: return std::string( kVersion ) + kWaterCommon + kSprayFragment;
	case Pass::Composite: return std::string( kVersion ) + kCommon + kCompositeFragment;
	}
	return std::string();
}

} // namespace fountain::shaders
