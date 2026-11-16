#include "Fountain.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"
#include "Water.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace fountain
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (UpdateClock).
constexpr int kClockPatience = 30;
/// Host seconds a single frame may advance by.
constexpr double kMaxFrameDelta = 0.1;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

void instanceAttribute( GLuint index, GLint size, GLsizei stride, size_t offset )
{
	glEnableVertexAttribArray( index );
	glVertexAttribPointer( index, size, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< const void* >( offset ) );
	glVertexAttribDivisor( index, 1 );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );
static_assert( kMaxNozzles <= shaders::kMaxLamps, "the lamp table must cover every nozzle" );

//---------------------------------------------------------------------------
FountainPlugin::FountainPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//Water flies in seconds: the plugin has to know what a second is.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//An FFT buffer: Resolume shows it as an audio-source picker and
			//writes the spectrum into it every frame. With nothing routed the
			//band levels are 0 and Onset never fires.
			SetBufferParamInfo( host, info.name, audio::kBins, FF_USAGE_FFT );
			for( int i = 0; i < audio::kBins; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint FountainPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult FountainPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &segmentShader, shaders::Pass::Segments, "segments" },
		{ &beadShader, shaders::Pass::Beads, "beads" },
		{ &sprayShader, shaders::Pass::Spray, "spray" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Fountain: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//The instanced draws: one vertex array each, its buffer stepping once an
	//instance, and an empty one for the composite's full-screen triangle (a
	//core profile draws nothing without a vertex array bound).
	SavedGLState saved;
	saved.Capture();
	glGenVertexArrays( 4, vaos );
	glGenBuffers( 3, vbos );
	glBindVertexArray( vaos[ 0 ] );
	glBindBuffer( GL_ARRAY_BUFFER, vbos[ 0 ] );
	instanceAttribute( 0, 4, sizeof( SegmentInstance ), 0 );
	instanceAttribute( 1, 4, sizeof( SegmentInstance ), 4 * sizeof( float ) );
	glBindVertexArray( vaos[ 1 ] );
	glBindBuffer( GL_ARRAY_BUFFER, vbos[ 1 ] );
	instanceAttribute( 0, 4, sizeof( BeadInstance ), 0 );
	glBindVertexArray( vaos[ 2 ] );
	glBindBuffer( GL_ARRAY_BUFFER, vbos[ 2 ] );
	instanceAttribute( 0, 4, sizeof( SprayInstance ), 0 );
	instanceAttribute( 1, 4, sizeof( SprayInstance ), 4 * sizeof( float ) );
	saved.Restore();

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
SimInputs FountainPlugin::Inputs() const
{
	SimInputs in;
	in.layout        = static_cast< LayoutId >( OptionIndex( params[ PT_LAYOUT ], static_cast< int >( LayoutId::Count ) ) );
	in.pressure      = PressureFromParam( params[ PT_PRESSURE ] );
	in.response      = std::clamp( static_cast< double >( params[ PT_RESPONSE ] ), 0.0, 1.0 );
	in.fireOn        = static_cast< FireOn >( OptionIndex( params[ PT_FIRE_ON ], static_cast< int >( FireOn::Count ) ) );
	in.shot          = ShotFromParam( params[ PT_SHOT ] );
	in.lead          = LeadFromParam( params[ PT_LEAD ] );
	in.nozzle        = NozzleFromParam( params[ PT_NOZZLE ] );
	in.lnDisturbance = DisturbanceFromParam( params[ PT_BREAKUP ] );
	in.valveTau      = ValveFromParam( params[ PT_VALVE ] );
	in.pumpTau       = PumpFromParam( params[ PT_PUMP ] );
	in.wind          = WindFromParam( params[ PT_WIND ] );
	in.gust          = std::clamp( static_cast< double >( params[ PT_GUST ] ), 0.0, 1.0 );
	in.screen        = std::clamp( static_cast< double >( params[ PT_SCREEN ] ), 0.0, 1.0 );
	for( int b = 0; b < 3; ++b )
		in.level[ b ] = analyser.BandLevel( b );
	in.onset    = analyser.Fired();
	in.bpm      = bpm;
	in.barPhase = barPhase;
	in.hostBeat = hostBeatSeen;
	return in;
}

//---------------------------------------------------------------------------
void FountainPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	//The camera: orthographic, face-on, View metres from the bottom of the
	//frame to the top, the basin's centre in the middle and its waterline
	//kWaterline up.
	const double view = ViewFromParam( params[ PT_VIEW ] );
	scale             = static_cast< double >( height ) / view;
	originX           = 0.5 * width;
	originY           = kWaterline * height;

	if( !accum.Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the water buffer" );
		return;
	}

	float lamps[ 3 * shaders::kMaxLamps ] = {};
	{
		const Lamps which = isEffect ? Lamps::White : static_cast< Lamps >( OptionIndex( params[ PT_LAMPS ], static_cast< int >( Lamps::Count ) ) );
		const int count   = static_cast< int >( sim.Nozzles().size() );
		for( int i = 0; i < std::min( count, shaders::kMaxLamps ); ++i )
			LampColour( which, i, count, lamps + 3 * i );
	}

	//The water: optical depth, additively.
	glBindFramebuffer( GL_FRAMEBUFFER, accum.GetGLID() );
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	setAdditiveBlend();
	struct Draw
	{
		FFGLShader* shader;
		int vao;
		const void* data;
		size_t bytes;
		size_t count;
		int bit;
	};
	const Draw draws[] = {
		{ &sprayShader, 2, sprayDraws.data(), sprayDraws.size() * sizeof( SprayInstance ), sprayDraws.size(), 4 },
		{ &segmentShader, 0, segments.data(), segments.size() * sizeof( SegmentInstance ), segments.size(), 1 },
		{ &beadShader, 1, beadDraws.data(), beadDraws.size() * sizeof( BeadInstance ), beadDraws.size(), 2 },
	};
	for( const Draw& d : draws )
	{
		if( d.count == 0 || ( drawMask & d.bit ) == 0 )
			continue;
		glUseProgram( d.shader->GetGLID() );
		glUniform2f( where( *d.shader, "Origin" ), static_cast< float >( originX ), static_cast< float >( originY ) );
		glUniform1f( where( *d.shader, "Scale" ), static_cast< float >( scale ) );
		glUniform2f( where( *d.shader, "Size" ), static_cast< float >( width ), static_cast< float >( height ) );
		glUniform1f( where( *d.shader, "Opacity" ), static_cast< float >( water::kJetOpacity ) );
		glUniform3fv( where( *d.shader, "Lamps" ), shaders::kMaxLamps, lamps );
		glBindVertexArray( vaos[ d.vao ] );
		glBindBuffer( GL_ARRAY_BUFFER, vbos[ d.vao ] );
		glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( d.bytes ), d.data, GL_STREAM_DRAW );
		glDrawArraysInstanced( GL_TRIANGLES, 0, 6, static_cast< GLsizei >( d.count ) );
	}
	glDisable( GL_BLEND );

	//The picture, into the host's framebuffer and viewport.
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, accum.TextureID() );
	//Always a real texture on the clip's unit, even in the source: a sampler on
	//texture 0 is "unloadable" to Apple's GL (boreal's trap).
	bindUnit( 1, input != nullptr ? input->Handle : accum.TextureID() );
	glUniform1i( where( compositeShader, "Accum" ), 0 );
	glUniform1i( where( compositeShader, "InputTexture" ), 1 );
	glUniform2i( where( compositeShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( compositeShader, "Size" ), width, height );
	glUniform1i( where( compositeShader, "IsOver" ), isEffect ? 1 : 0 );
	glUniform1f( where( compositeShader, "Exposure" ), static_cast< float >( ExposureFromParam( params[ PT_EXPOSURE ] ) ) );
	glUniform1f( where( compositeShader, "MixAmount" ), isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	glUniform1f( where( compositeShader, "Waterline" ), static_cast< float >( originY ) );
	glUniform1i( where( compositeShader, "Additive" ), additive ? 1 : 0 );
	glBindVertexArray( vaos[ 3 ] );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void FountainPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (ferrofluid's, by way of
	//rosette and millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest, fttest -- never wins the vote, and the fountain would
		//run on the wall clock: 120 frames in a fifth of a second, and the
		//Over still black (oxbow's selftest, before this). So after
		//kClockPatience frames the step's size decides: a frame is well under
		//a second and well over a millisecond (fountain's addition).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. Only differences reach the fountain.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( floatClock )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

void FountainPlugin::UpdateAudio( double dt )
{
	float bins[ audio::kBins ] = {};
	int binCount               = 0;
	const int host             = idToHost[ PT_AUDIO ];
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( host ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	audio::Settings settings;
	settings.sensitivity = std::clamp( params[ PT_SENSITIVITY ], 0.0f, 1.0f );
	analyser.SetUnprimedForTest( unprimed );
	analyser.Update( bins, binCount, static_cast< float >( dt ), settings );
}

//---------------------------------------------------------------------------
FFResult FountainPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//Time, the audio and the beat.
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	UpdateAudio( dt );
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;

	//The fountain lives in metres, not pixels: a new raster changes nothing
	//but the drawing (fttest --state). The negative control empties it.
	if( resizeClears && lastWidth > 0 && ( width != lastWidth || height != lastHeight ) )
		sim.Reset();
	lastWidth  = width;
	lastHeight = height;

	SimInputs in = Inputs();
	in.presses   = firePresses;
	firePresses  = 0;
	sim.Advance( dt, in );
	sim.Collect( segments, beadDraws, sprayDraws );

	render( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FountainPlugin::DeInitGL()
{
	uniformCache.clear();
	segmentShader.FreeGLResources();
	beadShader.FreeGLResources();
	sprayShader.FreeGLResources();
	compositeShader.FreeGLResources();
	if( vaos[ 0 ] != 0 )
		glDeleteVertexArrays( 4, vaos );
	if( vbos[ 0 ] != 0 )
		glDeleteBuffers( 3, vbos );
	std::fill( std::begin( vaos ), std::end( vaos ), 0u );
	std::fill( std::begin( vbos ), std::end( vbos ), 0u );
	accum.Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FountainPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void FountainPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* FountainPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult FountainPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void FountainPlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult FountainPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Fire acts on the press, once: an event arrives as 1 on press and 0 on
	//release, and a host is free to restate either.
	if( id == PT_FIRE )
	{
		const bool down = value >= 0.5f;
		if( down && !fireHeld )
			++firePresses;
		fireHeld = down;
	}
	params[ id ] = value;
	return FF_SUCCESS;
}

float FountainPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace fountain
