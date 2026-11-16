#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace fountain
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kPressureLow = 0.5, kPressureHigh = 20.0;
constexpr double kShotLow = 0.04, kShotHigh = 1.0;
constexpr double kLeadHigh = 1.5;
constexpr double kNozzleLow = 1e-3, kNozzleHigh = 12e-3;
constexpr double kDisturbanceCalm = 20.0, kDisturbanceRough = 2.0;
constexpr double kValveHigh = 0.3;
constexpr double kPumpLow = 0.05, kPumpHigh = 3.0;
constexpr double kWindHigh = 10.0;
constexpr double kViewLow = 1.0, kViewHigh = 40.0;
constexpr double kExposureLow = 0.25, kExposureHigh = 4.0;

const char* const kLayoutNames[] = { "Show", "Row", "Arcs", "Fan", "Screen" };
const char* const kFireOnNames[] = { "Steady", "Beat", "Half Beat", "2 Beats", "Bar", "Onset" };
const char* const kLampNames[]   = { "White", "Warm", "Rainbow", "Duo" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: the Show layout at 6 m of head, half of it from the music,
// shooters on the beat and led onto it, a 4 mm nozzle disturbed to ln 9 that
// turns a tall jet to beads near its top, and enough gust to tear the mist.
// AGENTS.md ("Decisions") says how they were chosen, over Resolume's own clips.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_LAYOUT, "Layout", "Fountain", 0.0f, kLayoutNames, static_cast< int >( LayoutId::Count ) ),
	standard( PT_PRESSURE, "Pressure", "Fountain", ParamFromPressure( 6.0 ) ),
	standard( PT_RESPONSE, "Response", "Fountain", 0.5f ),
	{ PT_AUDIO, "Audio", "Fountain", Kind::Buffer, 0.0f, nullptr, 0 },
	option( PT_FIRE_ON, "Fire On", "Fountain", 1.0f, kFireOnNames, static_cast< int >( FireOn::Count ) ),
	event( PT_FIRE, "Fire", "Fountain" ),
	standard( PT_SHOT, "Shot", "Fountain", ParamFromShot( 0.15 ) ),
	standard( PT_LEAD, "Lead", "Fountain", ParamFromLead( 1.0 ) ),
	standard( PT_SENSITIVITY, "Sensitivity", "Fountain", 0.5f ),

	standard( PT_NOZZLE, "Nozzle", "Water", ParamFromNozzle( 4e-3 ) ),
	standard( PT_BREAKUP, "Breakup", "Water", ParamFromDisturbance( 9.0 ) ),
	standard( PT_VALVE, "Valve", "Water", ParamFromValve( 0.06 ) ),
	standard( PT_PUMP, "Pump", "Water", ParamFromPump( 0.6 ) ),

	standard( PT_WIND, "Wind", "Air", ParamFromWind( 0.0 ) ),
	standard( PT_GUST, "Gust", "Air", 0.35f ),
	standard( PT_SCREEN, "Screen", "Air", 0.5f ),

	standard( PT_VIEW, "View", "View", ParamFromView( 10.0 ) ),
	option( PT_LAMPS, "Lamps", "View", 2.0f, kLampNames, static_cast< int >( Lamps::Count ) ),
	standard( PT_EXPOSURE, "Exposure", "View", ParamFromExposure( 1.0 ) ),
	standard( PT_MIX, "Mix", "View", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const bool overOnly   = id == PT_MIX;
			const bool sourceOnly = id == PT_LAMPS || id == PT_EXPOSURE;
			if( ( over && sourceOnly ) || ( !over && overOnly ) )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false );
	static const std::vector< unsigned int > over   = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

void LampColour( Lamps lamps, int index, int count, float rgb[ 3 ] )
{
	switch( lamps )
	{
	case Lamps::White: rgb[ 0 ] = rgb[ 1 ] = rgb[ 2 ] = 1.0f; return;
	case Lamps::Warm:
		rgb[ 0 ] = 1.0f;
		rgb[ 1 ] = 0.72f;
		rgb[ 2 ] = 0.40f;
		return;
	case Lamps::Duo:
		if( index % 2 == 0 )
		{
			rgb[ 0 ] = 0.10f;
			rgb[ 1 ] = 0.75f;
			rgb[ 2 ] = 1.0f;
		}
		else
		{
			rgb[ 0 ] = 1.0f;
			rgb[ 1 ] = 0.15f;
			rgb[ 2 ] = 0.70f;
		}
		return;
	default:
	{
		//Hue round the wheel by position, full saturation, linear light.
		const double h = 6.0 * ( count > 1 ? static_cast< double >( index ) / count : 0.0 );
		const int sector = static_cast< int >( std::floor( h ) ) % 6;
		const double f   = h - std::floor( h );
		double r = 0, g = 0, b = 0;
		switch( sector )
		{
		case 0: r = 1; g = f; break;
		case 1: r = 1 - f; g = 1; break;
		case 2: g = 1; b = f; break;
		case 3: g = 1 - f; b = 1; break;
		case 4: r = f; b = 1; break;
		default: r = 1; b = 1 - f; break;
		}
		rgb[ 0 ] = static_cast< float >( 0.15 + 0.85 * r );
		rgb[ 1 ] = static_cast< float >( 0.15 + 0.85 * g );
		rgb[ 2 ] = static_cast< float >( 0.15 + 0.85 * b );
		return;
	}
	}
}

double PressureFromParam( float v )
{
	return geometric( v, kPressureLow, kPressureHigh );
}
float ParamFromPressure( double m )
{
	return inverseGeometric( m, kPressureLow, kPressureHigh );
}
double ShotFromParam( float v )
{
	return geometric( v, kShotLow, kShotHigh );
}
float ParamFromShot( double s )
{
	return inverseGeometric( s, kShotLow, kShotHigh );
}
double LeadFromParam( float v )
{
	return linear( v, 0.0, kLeadHigh );
}
float ParamFromLead( double lead )
{
	return inverseLinear( lead, 0.0, kLeadHigh );
}
double NozzleFromParam( float v )
{
	return geometric( v, kNozzleLow, kNozzleHigh );
}
float ParamFromNozzle( double m )
{
	return inverseGeometric( m, kNozzleLow, kNozzleHigh );
}
double DisturbanceFromParam( float v )
{
	return linear( v, kDisturbanceCalm, kDisturbanceRough );
}
float ParamFromDisturbance( double ln )
{
	return inverseLinear( ln, kDisturbanceCalm, kDisturbanceRough );
}
double ValveFromParam( float v )
{
	return linear( v, 0.0, kValveHigh );
}
float ParamFromValve( double s )
{
	return inverseLinear( s, 0.0, kValveHigh );
}
double PumpFromParam( float v )
{
	return geometric( v, kPumpLow, kPumpHigh );
}
float ParamFromPump( double s )
{
	return inverseGeometric( s, kPumpLow, kPumpHigh );
}
double WindFromParam( float v )
{
	return linear( v, -kWindHigh, kWindHigh );
}
float ParamFromWind( double w )
{
	return inverseLinear( w, -kWindHigh, kWindHigh );
}
double ViewFromParam( float v )
{
	return geometric( v, kViewLow, kViewHigh );
}
float ParamFromView( double m )
{
	return inverseGeometric( m, kViewLow, kViewHigh );
}
double ExposureFromParam( float v )
{
	return geometric( v, kExposureLow, kExposureHigh );
}
float ParamFromExposure( double g )
{
	return inverseGeometric( g, kExposureLow, kExposureHigh );
}

} // namespace fountain
