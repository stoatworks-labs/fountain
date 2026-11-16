#pragma once

#include "Layout.h"
#include "Sim.h"
#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count -- map by index); Fire is a press; Audio is Resolume's
    64-bin FFT buffer.

    Units: metres, seconds, millimetres for the nozzle in the names only.
*/
namespace fountain
{

/// Every control either plugin has, by what it is -- NOT by the index a host
/// sees. Each plugin declares its own subset, densely, in `HostOrder()`'s
/// order, with the About block LAST in both.
enum ParamId : unsigned int
{
	// -- Fountain: what the music does to the pumps and valves -----------------
	PT_LAYOUT = 0,
	PT_PRESSURE,
	PT_RESPONSE,
	PT_AUDIO,
	PT_FIRE_ON,
	PT_FIRE,
	PT_SHOT,
	PT_LEAD,
	PT_SENSITIVITY,

	// -- Water -------------------------------------------------------------------
	PT_NOZZLE,
	PT_BREAKUP,
	PT_VALVE,
	PT_PUMP,

	// -- Air and screen ----------------------------------------------------------
	PT_WIND,
	PT_GUST,
	PT_SCREEN,

	// -- View --------------------------------------------------------------------
	PT_VIEW,
	PT_LAMPS,   ///< source only
	PT_EXPOSURE,///< source only
	PT_MIX,     ///< Over only

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// The ids a plugin declares, in the order the host sees them.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum class Lamps
{
	White = 0,
	Warm,
	Rainbow,
	Duo,
	Count
};

/// A nozzle's lamp, linear RGB: index of count.
void LampColour( Lamps lamps, int index, int count, float rgb[ 3 ] );

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double PressureFromParam( float v );///< m of head, 0.5..20, geometric
float ParamFromPressure( double metres );
double ShotFromParam( float v );///< s, 0.04..1, geometric
float ParamFromShot( double seconds );
double LeadFromParam( float v );///< x the command-to-peak time, 0..1.5
float ParamFromLead( double lead );
double NozzleFromParam( float v );///< radius, m, 1..12 mm, geometric
float ParamFromNozzle( double metres );
double DisturbanceFromParam( float v );///< ln( a/delta0 ), 20 at 0 down to 2 at 1
float ParamFromDisturbance( double ln );
double ValveFromParam( float v );///< s, 0..0.3
float ParamFromValve( double seconds );
double PumpFromParam( float v );///< s, 0.05..3, geometric
float ParamFromPump( double seconds );
double WindFromParam( float v );///< m/s, -10..10
float ParamFromWind( double metresPerSecond );
double ViewFromParam( float v );///< frame height, m, 1..40, geometric
float ParamFromView( double metres );
double ExposureFromParam( float v );///< x, 1/4..4, geometric
float ParamFromExposure( double gain );

/// Where the waterline sits, as a share of the frame's height from the bottom.
constexpr double kWaterline = 0.08;

} // namespace fountain
