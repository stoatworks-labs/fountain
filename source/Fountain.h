#pragma once

#include "Audio.h"
#include "Controls.h"
#include "PassBuffer.h"
#include "Sim.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <utility>
#include <vector>

namespace fountain
{

/**
    The plugin: the source (`SW Fountain`, the water lit by its lamps on a
    night sky) and, with `isEffect`, the Over (`SW Fountain Over`, the clip
    projected on the water from the camera's own position). One class, two
    registrations; see SourcePlugin.cpp and EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class FountainPlugin : public CFFGLPlugin
{
public:
	explicit FountainPlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	/// Set a control by what it is, whatever its host index.
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	Sim& SimForTest()
	{
		return sim;
	}
	const Sim& State() const
	{
		return sim;
	}
	const audio::Analyser& Analyser() const
	{
		return analyser;
	}
	/// The --state negative control: a new raster empties the fountain.
	void SetResizeClearsForTest( bool on )
	{
		resizeClears = on;
	}
	/// The --clock negative control: the host's clock read in single precision.
	void SetFloatClockForTest( bool on )
	{
		floatClock = on;
	}
	void SetUnprimedForTest( bool on )
	{
		unprimed = on;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// Which kinds of water are drawn: bit 0 the intact columns, bit 1 the
	/// beads, bit 2 the spray (fttest --breakup measures the column's end
	/// with the beads left out).
	void SetDrawMaskForTest( int mask )
	{
		drawMask = mask;
	}
	/// The --screen negative control: the Over adds the water to the clip
	/// instead of letting the clip light it.
	void SetAdditiveForTest( bool on )
	{
		additive = on;
	}
	/// The last frame's mapping: pixels a metre, and the pixel (from the
	/// bottom-left) of the basin's centre on the waterline.
	double PixelsPerMetre() const
	{
		return scale;
	}
	double OriginX() const
	{
		return originX;
	}
	double OriginY() const
	{
		return originY;
	}
	GLuint AccumTextureID() const
	{
		return accum.TextureID();
	}
	double HostNow() const
	{
		return now;
	}

private:
	void UpdateClock();
	void UpdateAudio( double dt );
	SimInputs Inputs() const;
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	Sim sim;
	audio::Analyser analyser;
	std::vector< SegmentInstance > segments;
	std::vector< BeadInstance > beadDraws;
	std::vector< SprayInstance > sprayDraws;

	ffglex::FFGLShader segmentShader, beadShader, sprayShader, compositeShader;
	GLuint vaos[ 4 ] = {};///< segments, beads, spray, the composite's (empty)
	GLuint vbos[ 3 ] = {};
	PassBuffer accum;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	double scale = 1.0, originX = 0.0, originY = 0.0;

	// The clock (ferrofluid's vote on seconds or milliseconds).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;
	double lastNow      = -1.0;

	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;

	int firePresses = 0;
	bool fireHeld   = false;

	int lastWidth = 0, lastHeight = 0;
	bool resizeClears = false;
	bool floatClock   = false;
	bool unprimed     = false;
	bool additive     = false;
	int drawMask      = 7;
};

} // namespace fountain
