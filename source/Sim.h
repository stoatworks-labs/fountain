#pragma once

#include "Layout.h"

#include <cstdint>
#include <deque>
#include <vector>

/**
    The fountain: pumps, valves, jets, beads and spray, in metres and seconds,
    in double, on the CPU. No GL: `fttest` drives it directly as well as
    through the plugin.

    One frame is the interval [ t - dt, t ] of the plugin's own clock (the sum
    of the frames' durations, never the host's absolute time). Inside it:

    1. **Pumps.** Each nozzle's head H follows its command (Pressure x the
       nozzle's share x ( 1 - Response + Response x its band's level )) through
       a first-order lag, in closed form: the command is one value a frame.
    2. **Valves.** A shooter's valve command is a set of open intervals (a shot
       is [ t_fire, t_fire + Shot )); its opening s follows the command through
       its own first-order lag, exactly across every edge, at the edge's own
       time inside the frame.
    3. **Parcels.** Every jet launches a parcel on a 240 Hz grid of launch
       times, plus one at every valve edge, at v = s sqrt( 2 g H ). An intact
       parcel needs no integrator: where it is at age tau is closed form.
    4. **Breakup.** Material older than t_b = ln( a/delta0 ) / q_max has
       pinched off: one bead per Rayleigh wavelength of material (a launch-time
       phase that counts wavelengths through the nozzle), born at exactly its
       pinch time with the material's closed-form state.
    5. **Beads and spray** are integrated over the frame's substeps (at most
       1/240 s): beads by RK4 under quadratic drag against the air, spray by the
       exact solution of linear drag toward the air over a step.

    Everything that can change what a check measures has a hook (`SimHooks`)
    that the harness's negative controls set. A shipped plugin leaves them at
    their defaults.
*/
namespace fountain
{

enum class FireOn
{
	Steady = 0,///< valves held open
	Beat,
	HalfBeat,
	TwoBeats,
	Bar,
	Onset,
	Count
};

/// What one frame asks of the fountain, already in physical units.
struct SimInputs
{
	LayoutId layout      = LayoutId::Show;
	double pressure      = 6.0;  ///< m of head, a share-1 shooter at full drive
	double response      = 0.5;  ///< 0..1
	FireOn fireOn        = FireOn::Beat;
	double shot          = 0.15; ///< s
	double lead          = 1.0;  ///< x the command-to-peak time
	double nozzle        = 4e-3; ///< a0, m
	double lnDisturbance = 9.0;  ///< ln( a0 / delta0 )
	double valveTau      = 0.06; ///< s
	double pumpTau       = 0.6;  ///< s
	double wind          = 0.0;  ///< m/s, +x
	double gust          = 0.35; ///< 0..1
	double screen        = 0.5;  ///< 0..1
	double level[ 3 ]    = {};   ///< Low (shooters), Mid (arcs), High (unused: the mist runs steady), 0..1
	bool onset           = false;
	int presses          = 0;
	double bpm           = 120.0;
	double barPhase      = 0.0;
	bool hostBeat        = false;
};

/// The negative controls' handles. Every default is the physics.
struct SimHooks
{
	double gravityScale = 1.0;        ///< g x this, everywhere in the model
	int speedLaw        = 0;          ///< 0 sqrt(2gH); 1 sqrt(gH); 2 speed proportional to the head
	double dragScale    = 1.0;        ///< 0 is vacuum (the closed-form checks)
	int wavelength      = 0;          ///< 0 Rayleigh; 1 Plateau (2 pi a); 2 long-wave
	bool eulerBeads     = false;      ///< forward Euler instead of RK4
	bool valveHeldPerFrame = false;   ///< the opening held a frame, then one Euler step
	bool predictorIgnoresValve = false;
	bool mistIgnoresWind = false;
	double angleOffset  = 0.0;        ///< radians added to every jet's direction
	std::uint64_t solo  = 0;          ///< bit i set: only nozzle i runs (0: all)
};

struct Parcel
{
	double t0     = 0.0;///< launch time
	double speed  = 0.0;///< m/s
	double phase  = 0.0;///< wavelengths of material launched before this, this stream
	unsigned stream = 0;
};

struct Bead
{
	double x = 0.0, y = 0.0, vx = 0.0, vy = 0.0;
	double radius = 0.0;///< m
	double drag   = 0.0;///< K, 1/m
	int nozzle    = 0;
	std::uint64_t serial = 0;
	/// Its state when it pinched off, for the harness's reference integrator.
	double born = 0.0, x0 = 0.0, y0 = 0.0, vx0 = 0.0, vy0 = 0.0;
};

struct Spray
{
	double x = 0.0, y = 0.0, vx = 0.0, vy = 0.0;
	double born   = 0.0;
	double volume = 0.0;///< m^3 at birth
	double plume  = 1.0;///< this parcel's share of the updraft's height
	int nozzle    = 0;
};

/// What the GPU draws, in metres. One per segment of intact column.
struct SegmentInstance
{
	float x0, y0, x1, y1;
	float halfWidth0, halfWidth1, lamp, unused;
};
struct BeadInstance
{
	float x, y, radius, lamp;
};
struct SprayInstance
{
	float x, y, sigma, tauArea;///< tauArea: optical depth x area, m^2
	float lamp, unused0, unused1, unused2;
};

class Sim
{
public:
	/// The launch grid and the longest substep.
	static constexpr double kRate = 240.0;
	/// Below this opening a valve passes nothing worth drawing.
	static constexpr double kEmitFloor = 0.02;
	/// Spray parcels a second, per emitter, at any flow.
	static constexpr double kSprayRate = 40.0;
	/// m^3/s per metre of bar at Screen 1 and full mist head.
	static constexpr double kMaxFlowPerMetre = 0.12e-3;
	/// The mist pump's share of Pressure, and the updraft's share of its head.
	static constexpr double kMistHead = 1.6, kPlumeShare = 0.7;
	/// CHOSEN: the plume's air keeps moving after the spray changes; its
	/// strength follows the spray through a lag of this many seconds.
	static constexpr double kPlumeLag = 1.0;

	void Reset();
	/// One frame of `dt` seconds. The plugin's clock is the sum of the frames,
	/// each rounded to 10 ns so that the same frames from any host clock origin
	/// are bit-for-bit the same frames (`fttest --clock`).
	void Advance( double dt, const SimInputs& in );

	/// The frame's primitives, at its end.
	void Collect( std::vector< SegmentInstance >& segments, std::vector< BeadInstance >& beads,
	              std::vector< SprayInstance >& spray ) const;

	/// The air's horizontal velocity at x, sim time t: Wind plus three
	/// travelling gusts (chosen wavelengths and periods), frozen turbulence
	/// carried by the mean wind, of amplitude Gust x ( 2 + |Wind| ) m/s.
	static double AirVelocity( double x, double t, double wind, double gust );

	/// The model's own time from a shooter's valve command to its column's
	/// highest point: Shot plus the apex time of the fastest parcel (the one
	/// leaving as the valve starts to close), drag and breakup included.
	double CommandToPeak( int nozzle ) const;

	//-------------------------------------------------------------------
	// State, for the plugin's diagnostics and the harness.
	//-------------------------------------------------------------------
	double Time() const
	{
		return simTime;
	}
	double FrameStart() const
	{
		return frameStart;
	}
	const std::vector< Nozzle >& Nozzles() const
	{
		return *nozzles;
	}
	const std::vector< Bead >& Beads() const
	{
		return beads;
	}
	const std::vector< Spray >& SprayParcels() const
	{
		return spray;
	}
	double Head( int nozzle ) const;
	double Opening( int nozzle ) const;
	/// Valve openings commanded since the plugin loaded (all shooters).
	std::uint64_t Shots() const
	{
		return shots;
	}
	/// Launch times of the last 64 shots of nozzle i (command times).
	const std::deque< double >& ShotTimes( int nozzle ) const;
	/// The breakup age, the wavelength over a0 and the bead radius over a0
	/// the model is using (hooks included).
	double BreakupAgeNow() const
	{
		return breakupAge;
	}
	double WavelengthFactor() const;
	double DropFactor() const;
	double Gravity() const;
	double SpeedFor( int nozzle, double opening, double head ) const;
	const SimInputs& Inputs() const
	{
		return inputs;
	}
	/// Closed-form state of jet material launched at t0 with that speed, at age.
	void Material( int nozzle, double speed, double age, double& x, double& y, double& vx, double& vy ) const;

	SimHooks hooks;

private:
	struct Interval
	{
		double start, end;
	};
	struct Jet
	{
		double head = 0.0, headStart = 0.0, headCommand = 0.0;
		double opening = 0.0, openingStart = 0.0;
		std::deque< Interval > open;///< valve command intervals
		std::deque< Parcel > parcels;
		unsigned stream   = 0;
		bool emitting     = false;
		double phase      = 0.0;
		double brokenTo   = -1e300;
		double advancedPrev = 0.0;
		bool advancedValid  = false;
		double lastT      = 0.0, lastSpeed = 0.0;
		double emittedTo  = -1e300;///< the last launch time handled
		double sprayOwed  = 0.0;
		std::uint32_t sprayCount = 0;
		std::deque< double > shotTimes;
	};

	bool commandOpen( const Jet& jet, double t ) const;
	/// The valve's opening at t; `left` takes the limit from before t (they
	/// differ only at an edge of a valve with no lag).
	double openingAt( const Jet& jet, double t, bool left = false ) const;
	double headAt( const Jet& jet, double t ) const;
	void fire( int nozzle, double at );
	void scheduleValves( double dt );
	void emit( int nozzle, double t, std::vector< Spray >& born );
	void pinch( int nozzle, double upTo, double stepStart, std::vector< Bead >& born );
	void stepBeads( std::vector< Bead >& list, double from, double to ) const;
	void stepBead( Bead& b, double t, double h ) const;
	void stepSpray( Spray& s, double t, double h ) const;
	bool active( int nozzle ) const;
	double bandLevel( Role role ) const;
	double updraft( const Spray& s ) const;

	const std::vector< Nozzle >* nozzles = &NozzlesOf( LayoutId::Show );
	LayoutId layout = LayoutId::Count;
	std::vector< Jet > jets;
	std::vector< Bead > beads;
	std::vector< Spray > spray;
	SimInputs inputs;
	double simTime    = 0.0;
	double frameStart = 0.0;
	double mistHead   = 0.0;///< mean head of the running mist emitters, m
	double plumeHead = 0.0, plumeScreen = 0.0;///< the same, and Screen, through the plume's lag
	double barSpacing = 1.0;
	double barPosition = 0.0;
	double lastBarPhase = -1.0;
	double breakupAge = 1.0;
	double barMin = 0.0, barMax = 0.0;
	std::uint64_t shots = 0, beadSerial = 0;
	int heldPresses = 0;   ///< presses that came on frames with no time in them
	bool heldOnset  = false;///< the same, an onset
};

} // namespace fountain
