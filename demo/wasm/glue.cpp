/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Fountain.cpp` and everything it calls, compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `FountainPlugin`, as SourcePlugin.cpp / EffectPlugin.cpp's
        registrations would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, SetBeatInfo,
        the Audio buffer's elements, and ProcessOpenGL with a viewport (the
        source) or one input texture (the Over), as `tools/fttest`'s Rig does.

    One thing here is the plugin's harness rather than a host: `feedKicks`,
    with the three functions it needs, is fttest's own music (`--kicks`),
    copied from tools/fttest/main.cpp character for character (and held to it
    by demo/tools/check_shaders.py). The page's host row offers it as the one
    thing a browser can put in the Audio buffer besides silence.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Fountain.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

using namespace fountain;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	explicit Instance( bool effect ) : plugin( effect )
	{
	}

	FountainPlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

//---------------------------------------------------------------------------
// tools/fttest/main.cpp's music, verbatim (check_shaders.py compares).
//---------------------------------------------------------------------------
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double hash01( uint32_t a, uint32_t b = 0 )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

/// Write a whole spectrum into the Audio buffer, the way the host does.
void writeSpectrum( FountainPlugin& plugin, const std::function< double( int ) >& bin )
{
	const unsigned int host = static_cast< unsigned int >( plugin.HostIndexOf( PT_AUDIO ) );
	for( int b = 0; b < audio::kBins; ++b )
		plugin.SetParamElementValue( host, static_cast< unsigned int >( b ), static_cast< float >( bin( b ) ) );
}

/// The harness's music: a steady bass bed in the low quarter (so the first
/// frame of a cold start is not a rise from nothing), a busy hi-hat at the
/// top (a fresh level every frame) and a kick on frames `first`,
/// `first + every`, ..., soft and hard by turns. With `ghosts`, a ghost note
/// a thirtieth of a soft kick halfway between: an onset only a high
/// Sensitivity hears (measured: 6 onsets in 3 s at Sensitivity 0 and 0.5, 10
/// at 1; tools/sweep.py sweeps Sensitivity on it).
void feedKicks( FountainPlugin& plugin, int frame, int first = 0, int every = 30, bool ghosts = false )
{
	const int since  = frame >= first ? ( frame - first ) % every : every;
	const bool hard  = frame >= first && ( ( frame - first ) / every ) % 2 == 1;
	const int ghost  = ( since - every / 2 + every ) % every;
	const double hat = 0.3 * hash01( static_cast< uint32_t >( frame ), 11 );
	writeSpectrum( plugin, [ & ]( int b ) {
		double v = b < 16 ? 0.15 : ( b > 40 ? 0.02 + hat : 0.02 );
		if( since < 4 && b < 10 )
			v += ( hard ? 0.9 : 0.6 ) * std::exp( -since * 0.8 );
		if( ghosts && frame >= first && ghost < 4 && b < 10 )
			v += 0.02 * std::exp( -ghost * 0.8 );
		return v;
	} );
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* ft_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void ft_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int ft_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ft_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ft_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ft_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ft_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float ft_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ft_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ft_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ft_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int ft_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ft_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ft_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ft_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// Resolume's SetBeatInfo, sent every frame: the tempo and the bar's phase.
EMSCRIPTEN_KEEPALIVE void ft_set_beat( Instance* instance, float bpm, float barPhase )
{
	instance->plugin.SetBeatInfo( bpm, barPhase );
}

/// The Audio buffer with nothing in it: every bin 0, as a layer with no audio
/// routed to it leaves the buffer.
EMSCRIPTEN_KEEPALIVE void ft_audio_silence( Instance* instance )
{
	writeSpectrum( instance->plugin, []( int ) { return 0.0; } );
}

/// The harness's music for frame `frame` of its 60 fps clock, as
/// `fttest --kicks` writes it before rendering that frame.
EMSCRIPTEN_KEEPALIVE void ft_audio_kicks( Instance* instance, int frame )
{
	feedKicks( instance->plugin, frame, 0, 30, true );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound.
/// The source reads its size from the viewport the page set; the Over is
/// handed one input of clipWidth x clipHeight whose GL name is `clip` (a
/// texture the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int ft_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	if( instance->plugin.IsEffect() )
	{
		instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
		instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
		instance->input.Handle                                 = static_cast< GLuint >( clip );
		instance->process.numInputTextures                     = 1;
		instance->process.inputTextures                        = instance->inputs;
	}
	else
	{
		instance->process.numInputTextures = 0;
		instance->process.inputTextures    = nullptr;
	}
	instance->process.HostFBO = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Fountain.cpp's Inputs() and render() apply to it. NaN where the plugin
// uses the host's 0..1 as it is (or an index, or a press).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ft_convert( Instance* instance, int index, float v )
{
	unsigned int id = PT_COUNT;
	for( unsigned int candidate = 0; candidate < PT_ABOUT_TEXT; ++candidate )
		if( instance->plugin.HostIndexOf( candidate ) == index )
			id = candidate;
	switch( id )
	{
	case PT_PRESSURE: return PressureFromParam( v );
	case PT_SHOT: return ShotFromParam( v );
	case PT_LEAD: return LeadFromParam( v );
	case PT_NOZZLE: return NozzleFromParam( v );
	case PT_BREAKUP: return DisturbanceFromParam( v );
	case PT_VALVE: return ValveFromParam( v );
	case PT_PUMP: return PumpFromParam( v );
	case PT_WIND: return WindFromParam( v );
	case PT_VIEW: return ViewFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). The page's presets are written in metres and
/// seconds and go through this. NaN for a parameter with no conversion.
EMSCRIPTEN_KEEPALIVE double ft_param_from( Instance* instance, int index, double value )
{
	unsigned int id = PT_COUNT;
	for( unsigned int candidate = 0; candidate < PT_ABOUT_TEXT; ++candidate )
		if( instance->plugin.HostIndexOf( candidate ) == index )
			id = candidate;
	switch( id )
	{
	case PT_PRESSURE: return ParamFromPressure( value );
	case PT_SHOT: return ParamFromShot( value );
	case PT_LEAD: return ParamFromLead( value );
	case PT_NOZZLE: return ParamFromNozzle( value );
	case PT_BREAKUP: return ParamFromDisturbance( value );
	case PT_VALVE: return ParamFromValve( value );
	case PT_PUMP: return ParamFromPump( value );
	case PT_WIND: return ParamFromWind( value );
	case PT_VIEW: return ParamFromView( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (State(), Analyser(), PixelsPerMetre()).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ft_sim_time( Instance* instance )
{
	return instance->plugin.State().Time();
}
EMSCRIPTEN_KEEPALIVE int ft_nozzles( Instance* instance )
{
	return static_cast< int >( instance->plugin.State().Nozzles().size() );
}
EMSCRIPTEN_KEEPALIVE int ft_beads( Instance* instance )
{
	return static_cast< int >( instance->plugin.State().Beads().size() );
}
EMSCRIPTEN_KEEPALIVE int ft_spray( Instance* instance )
{
	return static_cast< int >( instance->plugin.State().SprayParcels().size() );
}
EMSCRIPTEN_KEEPALIVE double ft_shots( Instance* instance )
{
	return static_cast< double >( instance->plugin.State().Shots() );
}
EMSCRIPTEN_KEEPALIVE double ft_breakup_age( Instance* instance )
{
	return instance->plugin.State().BreakupAgeNow();
}
EMSCRIPTEN_KEEPALIVE double ft_pixels_per_metre( Instance* instance )
{
	return instance->plugin.PixelsPerMetre();
}
/// The host clock's unit as the plugin settled it: 1 seconds, 0.001 ms, 0 not yet.
EMSCRIPTEN_KEEPALIVE double ft_clock_scale( Instance* instance )
{
	return instance->plugin.ClockScale();
}
EMSCRIPTEN_KEEPALIVE double ft_onsets( Instance* instance )
{
	return static_cast< double >( instance->plugin.Analyser().Onsets() );
}
EMSCRIPTEN_KEEPALIVE double ft_band_level( Instance* instance, int band )
{
	return instance->plugin.Analyser().BandLevel( band );
}
/// The model's command-to-peak time for nozzle i (what Lead scales).
EMSCRIPTEN_KEEPALIVE double ft_command_to_peak( Instance* instance, int nozzle )
{
	return instance->plugin.State().CommandToPeak( nozzle );
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Its own `mkdir -p` goes through system(), which a browser does not have.
/// Call before the first InitGL, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void ft_prepare_log()
{
	setenv( "FOUNTAIN_LOG_DIR", "/fountain/logs", 1 );
	mkdir( "/fountain", 0755 );
	mkdir( "/fountain/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ft_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
