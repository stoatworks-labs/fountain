#include "Sim.h"

#include "Water.h"

#include <algorithm>
#include <cmath>

namespace fountain
{
namespace
{
/// Frame durations are rounded to this before they are added to the clock.
constexpr double kTimeQuantum = 1e-8;
/// The most beat boundaries one frame may fire per nozzle (more is a seek).
constexpr int kMaxBeatsPerFrame = 8;
/// The longest a frame may be: a stalled host must not throw a tenth of a
/// second of water at once.
constexpr double kMaxFrame = 0.1;
/// A drawn column is never wider than this many nozzle radii (near an apex
/// mass conservation would make it unbounded).
constexpr double kMaxWidening = 3.0;
/// Spray older than this share of its volume has evaporated.
constexpr double kSprayGone = 0.05;
constexpr double kForever = 1e300;

double quantise( double dt )
{
	return std::round( dt / kTimeQuantum ) * kTimeQuantum;
}

/// A first-order lag toward a constant target over h seconds, exactly.
double lagStep( double x, double target, double h, double tau )
{
	if( tau <= 1e-9 )
		return target;
	return target + ( x - target ) * std::exp( -h / tau );
}

/// PCG hash (O'Neill), integer in, integer out: the spray's angles and
/// shares, the same on every machine.
std::uint32_t pcg( std::uint32_t v )
{
	const std::uint32_t state = v * 747796405u + 2891336453u;
	const std::uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double unit( std::uint32_t v )
{
	return static_cast< double >( pcg( v ) ) / 4294967296.0;
}
} // namespace

//---------------------------------------------------------------------------
void Sim::Reset()
{
	jets.clear();
	beads.clear();
	spray.clear();
	layout       = LayoutId::Count;
	simTime      = 0.0;
	frameStart   = 0.0;
	barPosition  = 0.0;
	lastBarPhase = -1.0;
	mistHead     = 0.0;
	plumeHead    = 0.0;
	plumeScreen  = 0.0;
}

bool Sim::active( int i ) const
{
	return hooks.solo == 0 || ( ( hooks.solo >> i ) & 1u ) != 0;
}

double Sim::Gravity() const
{
	return water::kGravity * hooks.gravityScale;
}

double Sim::WavelengthFactor() const
{
	switch( hooks.wavelength )
	{
	case 1: return water::kPlateauLambda;
	case 2: return water::kLongWaveLambda;
	default: return water::kRayleighLambda;
	}
}

double Sim::DropFactor() const
{
	return hooks.wavelength == 0 ? water::kDropRadius : std::cbrt( 0.75 * WavelengthFactor() );
}

double Sim::bandLevel( Role role ) const
{
	const int band = role == Role::Shooter ? 0 : ( role == Role::Arc ? 1 : 2 );
	return std::clamp( inputs.level[ band ], 0.0, 1.0 );
}

double Sim::Head( int i ) const
{
	return i >= 0 && i < static_cast< int >( jets.size() ) ? jets[ static_cast< size_t >( i ) ].head : 0.0;
}

double Sim::Opening( int i ) const
{
	return i >= 0 && i < static_cast< int >( jets.size() ) ? jets[ static_cast< size_t >( i ) ].opening : 0.0;
}

const std::deque< double >& Sim::ShotTimes( int i ) const
{
	static const std::deque< double > none;
	return i >= 0 && i < static_cast< int >( jets.size() ) ? jets[ static_cast< size_t >( i ) ].shotTimes : none;
}

double Sim::SpeedFor( int i, double opening, double head ) const
{
	const double g = Gravity();
	head           = std::max( head, 0.0 );
	switch( hooks.speedLaw )
	{
	case 1: return opening * std::sqrt( g * head );
	case 2:
	{
		//The --louder negative: speed proportional to the head, so height
		//goes as its square.
		const double full = inputs.pressure * ( *nozzles )[ static_cast< size_t >( i ) ].head;
		return full > 0.0 ? opening * std::sqrt( 2.0 * g * full ) * head / full : 0.0;
	}
	default: return opening * std::sqrt( 2.0 * g * head );
	}
}

void Sim::Material( int i, double speed, double age, double& x, double& y, double& vx, double& vy ) const
{
	const Nozzle& n    = ( *nozzles )[ static_cast< size_t >( i ) ];
	const double angle = n.angle + hooks.angleOffset;
	const double g     = Gravity();
	vx                 = speed * std::cos( angle );
	vy                 = speed * std::sin( angle ) - g * age;
	x                  = n.x + speed * std::cos( angle ) * age;
	y                  = speed * std::sin( angle ) * age - 0.5 * g * age * age;
}

double Sim::AirVelocity( double x, double t, double wind, double gust )
{
	//Three travelling waves of chosen wavelength (m) and period (s), weights
	//summing to one, frozen into the mean wind.
	static constexpr double kLength[ 3 ] = { 23.0, 9.7, 4.1 };
	static constexpr double kPeriod[ 3 ] = { 11.0, 5.3, 2.9 };
	static constexpr double kWeight[ 3 ] = { 0.5, 0.3, 0.2 };
	static constexpr double kPhase[ 3 ]  = { 0.3, 2.1, 4.4 };
	if( gust <= 0.0 )
		return wind;
	const double amplitude = gust * ( 1.0 + std::fabs( wind ) );
	const double xi        = x - wind * t;
	double sum             = 0.0;
	for( int i = 0; i < 3; ++i )
		sum += kWeight[ i ] * std::sin( 2.0 * water::kPi * ( xi / kLength[ i ] - t / kPeriod[ i ] ) + kPhase[ i ] );
	return wind + amplitude * sum;
}

//---------------------------------------------------------------------------
// Pumps and valves.
//---------------------------------------------------------------------------
double Sim::headAt( const Jet& jet, double t ) const
{
	return lagStep( jet.headStart, jet.headCommand, std::max( 0.0, t - frameStart ), inputs.pumpTau );
}

bool Sim::commandOpen( const Jet& jet, double t ) const
{
	for( const Interval& v : jet.open )
		if( v.start <= t && t < v.end )
			return true;
	return false;
}

double Sim::openingAt( const Jet& jet, double t, bool left ) const
{
	if( inputs.valveTau <= 1e-9 && !hooks.valveHeldPerFrame )
	{
		//No lag: the opening IS the command, which is open on [ start, end ).
		//At an edge the two one-sided limits differ, and the launch at the edge
		//takes the side it belongs to (fttest --flight).
		for( const Interval& v : jet.open )
			if( left ? ( v.start < t && t <= v.end ) : ( v.start <= t && t < v.end ) )
				return 1.0;
		return 0.0;
	}
	if( hooks.valveHeldPerFrame )
	{
		//The negative control: the opening held over the frame, then one
		//forward-Euler step at its end.
		if( t < simTime )
			return jet.openingStart;
		const double target = commandOpen( jet, simTime - 1e-12 ) ? 1.0 : 0.0;
		const double k      = inputs.valveTau > 1e-9 ? std::min( ( simTime - frameStart ) / inputs.valveTau, 1.0 ) : 1.0;
		return jet.openingStart + ( target - jet.openingStart ) * k;
	}
	//Exact: the command is constant between its edges.
	double edges[ 2 * 64 + 2 ];
	int count = 0;
	for( const Interval& v : jet.open )
		for( double e : { v.start, v.end } )
			if( e > frameStart && e < t && count < 2 * 64 )
				edges[ count++ ] = e;
	std::sort( edges, edges + count );
	double s    = jet.openingStart;
	double from = frameStart;
	for( int k = 0; k <= count; ++k )
	{
		const double to = k < count ? edges[ k ] : t;
		if( to > from )
		{
			const double target = commandOpen( jet, 0.5 * ( from + to ) ) ? 1.0 : 0.0;
			s                   = lagStep( s, target, to - from, inputs.valveTau );
			from                = to;
		}
	}
	return s;
}

void Sim::fire( int i, double at )
{
	if( ( *nozzles )[ static_cast< size_t >( i ) ].role != Role::Shooter || !active( i ) )
		return;
	Jet& jet = jets[ static_cast< size_t >( i ) ];
	jet.open.push_back( { at, at + std::max( inputs.shot, 0.0 ) } );
	while( jet.open.size() > 64 )
		jet.open.pop_front();
	jet.shotTimes.push_back( at );
	while( jet.shotTimes.size() > 64 )
		jet.shotTimes.pop_front();
	++shots;
}

double Sim::CommandToPeak( int i ) const
{
	if( i < 0 || i >= static_cast< int >( jets.size() ) )
		return 0.0;
	const Jet& jet  = jets[ static_cast< size_t >( i ) ];
	const double g  = Gravity();
	const double sm = ( inputs.valveTau > 1e-9 && !hooks.predictorIgnoresValve ) ? 1.0 - std::exp( -inputs.shot / inputs.valveTau ) : 1.0;
	const double v  = SpeedFor( i, sm, jet.head );
	const double tb = breakupAge;
	double apex     = v / g;
	if( hooks.dragScale > 0.0 && tb < v / g )
	{
		//Drag-free until it pinches off, then a bead rising against quadratic
		//drag: t = t_b + ( v_t / g ) atan( v_b / v_t ).
		const double r  = DropFactor() * inputs.nozzle;
		const double K  = water::DragConstant( 2.0 * r ) * hooks.dragScale;
		const double vt = std::sqrt( g / K );
		const double vb = v - g * tb;
		apex            = tb + ( vt / g ) * std::atan( vb / vt );
	}
	return std::max( inputs.shot, 0.0 ) + apex;
}

void Sim::scheduleValves( double dt )
{
	const double t0 = frameStart;
	const int count = static_cast< int >( jets.size() );

	for( int i = 0; i < count; ++i )
	{
		Jet& jet = jets[ static_cast< size_t >( i ) ];
		if( ( *nozzles )[ static_cast< size_t >( i ) ].role != Role::Shooter )
			continue;
		bool steady = false;
		for( Interval& v : jet.open )
		{
			if( v.end >= kForever )
			{
				if( inputs.fireOn == FireOn::Steady )
					steady = true;
				else
					v.end = t0;//leaving Steady closes the valve at the frame's start
			}
		}
		if( inputs.fireOn == FireOn::Steady && !steady && active( i ) )
			jet.open.push_back( { -kForever, kForever } );
		while( !jet.open.empty() && jet.open.front().end < t0 - 1.0 )
			jet.open.pop_front();
	}

	//The button and the onsets carry no moment of their own: the frame's start.
	for( int p = 0; p < inputs.presses; ++p )
		for( int i = 0; i < count; ++i )
			fire( i, t0 );
	if( inputs.fireOn == FireOn::Onset && inputs.onset )
		for( int i = 0; i < count; ++i )
			fire( i, t0 );

	//The beat. The bar position is the host's bar phase unwrapped; a host
	//that never moves it gets a clock of the plugin's own at its BPM.
	const double bpm = inputs.bpm > 1.0 ? inputs.bpm : 120.0;
	if( inputs.hostBeat )
	{
		if( lastBarPhase >= 0.0 )
		{
			double d = inputs.barPhase - lastBarPhase;
			if( d < -0.5 )
				d += 1.0;
			else if( d > 0.5 )
				d -= 1.0;
			barPosition += d;
		}
		else
			barPosition = std::floor( barPosition ) + inputs.barPhase;//the host's own phase, from its first beat on
		lastBarPhase = inputs.barPhase;
	}
	else
		barPosition += dt * bpm / 240.0;
	const double rate = bpm / 240.0;//bars a second

	double step = 0.0;
	switch( inputs.fireOn )
	{
	case FireOn::Beat: step = 0.25; break;
	case FireOn::HalfBeat: step = 0.125; break;
	case FireOn::TwoBeats: step = 0.5; break;
	case FireOn::Bar: step = 1.0; break;
	default: break;
	}
	for( int i = 0; i < count; ++i )
	{
		Jet& jet = jets[ static_cast< size_t >( i ) ];
		if( ( *nozzles )[ static_cast< size_t >( i ) ].role != Role::Shooter || step <= 0.0 )
		{
			jet.advancedValid = false;
			continue;
		}
		//Lead: the valve opens when the bar position, advanced by Lead times
		//this nozzle's own command-to-peak time, crosses a boundary -- so the
		//column peaks when the real one does.
		const double advanced = barPosition + std::max( inputs.lead, 0.0 ) * CommandToPeak( i ) * rate;
		if( jet.advancedValid && dt > 0.0 && advanced > jet.advancedPrev )
		{
			const double a0 = jet.advancedPrev, a1 = advanced;
			double boundary = ( std::floor( a0 / step + 1e-9 ) + 1.0 ) * step;
			for( int n = 0; boundary <= a1 + 1e-12 && n < kMaxBeatsPerFrame; ++n, boundary += step )
				fire( i, t0 + ( boundary - a0 ) / ( a1 - a0 ) * dt );
		}
		jet.advancedPrev  = advanced;
		jet.advancedValid = true;
	}
}

//---------------------------------------------------------------------------
// Launching.
//---------------------------------------------------------------------------
void Sim::emit( int i, double t, std::vector< Spray >& born )
{
	const Nozzle& n = ( *nozzles )[ static_cast< size_t >( i ) ];
	Jet& jet        = jets[ static_cast< size_t >( i ) ];
	const double H  = headAt( jet, t );

	if( n.role == Role::Mist )
	{
		//The spray: a fixed number of parcels a second, each carrying the
		//flow's water since the last, so a thin screen is thin rather than
		//sparse.
		const double full = std::max( inputs.pressure * kMistHead, 1e-6 );
		const double flow = std::clamp( inputs.screen, 0.0, 1.0 ) * kMaxFlowPerMetre * barSpacing * std::clamp( H / full, 0.0, 1.0 );
		if( flow <= 0.0 )
		{
			jet.sprayOwed = 0.0;
			return;
		}
		jet.sprayOwed += kSprayRate / kRate;
		while( jet.sprayOwed >= 1.0 )
		{
			jet.sprayOwed -= 1.0;
			const std::uint32_t seed = static_cast< std::uint32_t >( i ) * 0x9E3779B9u + jet.sprayCount * 4u;
			++jet.sprayCount;
			const double plumeTop = kPlumeShare * H;
			const double speed    = std::sqrt( 2.0 * Gravity() * plumeTop ) * ( 0.4 + 0.6 * unit( seed ) );
			const double angle    = 0.5 * water::kPi + ( unit( seed + 1u ) - 0.5 ) * 0.9;
			Spray s;
			s.x      = n.x + ( unit( seed + 2u ) - 0.5 ) * barSpacing;
			s.y      = 0.0;
			s.vx     = speed * std::cos( angle );
			s.vy     = speed * std::sin( angle );
			s.born   = t;
			s.volume = flow / kSprayRate;
			s.plume  = 0.25 + 0.75 * unit( seed + 3u );
			s.nozzle = i;
			born.push_back( s );
		}
		return;
	}

	const double s      = n.role == Role::Shooter ? openingAt( jet, t ) : 1.0;
	const double v      = SpeedFor( i, s, H );
	const double lambda = WavelengthFactor() * inputs.nozzle;
	if( s < kEmitFloor || v < 0.05 )
	{
		//A valve with no lag shuts AT its edge: the stream's last material
		//leaves at that instant, at the speed it had just before.
		if( jet.emitting && n.role == Role::Shooter && t > jet.lastT )
		{
			const double sl = openingAt( jet, t, true );
			const double vl = SpeedFor( i, sl, H );
			if( sl >= kEmitFloor && vl >= 0.05 )
			{
				jet.phase += 0.5 * ( jet.lastSpeed + vl ) * ( t - jet.lastT ) / lambda;
				jet.parcels.push_back( { t, vl, jet.phase, jet.stream } );
				jet.lastT     = t;
				jet.lastSpeed = vl;
			}
		}
		jet.emitting = false;
		return;
	}
	if( !jet.emitting )
	{
		++jet.stream;
		jet.phase    = 0.0;
		jet.emitting = true;
	}
	else
	{
		//Wavelengths of material through the nozzle since the stream began:
		//the trapezoid of v over launch time, over lambda_0.
		jet.phase += 0.5 * ( jet.lastSpeed + v ) * ( t - jet.lastT ) / lambda;
	}
	jet.lastT     = t;
	jet.lastSpeed = v;
	jet.parcels.push_back( { t, v, jet.phase, jet.stream } );
}

void Sim::pinch( int i, double upTo, double stepStart, std::vector< Bead >& born )
{
	Jet& jet = jets[ static_cast< size_t >( i ) ];
	if( upTo <= jet.brokenTo )
		return;
	const Nozzle& n    = ( *nozzles )[ static_cast< size_t >( i ) ];
	const double angle = n.angle + hooks.angleOffset;
	const double g     = Gravity();
	const double r     = DropFactor() * inputs.nozzle;
	const double K     = water::DragConstant( 2.0 * r ) * hooks.dragScale;

	const auto& p = jet.parcels;
	for( size_t k = 0; k + 1 < p.size(); ++k )
	{
		const Parcel& A = p[ k ];
		const Parcel& B = p[ k + 1 ];
		if( B.t0 <= jet.brokenTo )
			continue;
		if( A.t0 >= upTo )
			break;
		if( A.stream != B.stream || B.phase <= A.phase )
			continue;
		for( double m = std::floor( A.phase ) + 1.0; m <= B.phase; m += 1.0 )
		{
			const double f   = ( m - A.phase ) / ( B.phase - A.phase );
			const double tau = A.t0 + f * ( B.t0 - A.t0 );
			if( tau <= jet.brokenTo || tau > upTo )
				continue;
			const double v     = A.speed + f * ( B.speed - A.speed );
			const double birth = std::max( tau + breakupAge, stepStart );
			const double age   = birth - tau;
			const double rise  = v * std::sin( angle );
			if( rise <= 0.0 || age >= 2.0 * rise / g )
				continue;//it reached the basin still a column
			Bead b;
			Material( i, v, age, b.x, b.y, b.vx, b.vy );
			b.radius = r;
			b.drag   = K;
			b.nozzle = i;
			b.serial = ++beadSerial;
			b.born   = birth;
			b.x0     = b.x;
			b.y0     = b.y;
			b.vx0    = b.vx;
			b.vy0    = b.vy;
			born.push_back( b );
		}
	}
	jet.brokenTo = upTo;
}

//---------------------------------------------------------------------------
// Flight.
//---------------------------------------------------------------------------
void Sim::stepBead( Bead& b, double t, double h ) const
{
	const double g    = Gravity();
	const double K    = b.drag;
	const double wind = inputs.wind, gust = inputs.gust;
	auto accel        = [&]( double tt, double x, double vx, double vy, double& ax, double& ay ) {
        const double rx = vx - AirVelocity( x, tt, wind, gust );
        const double m  = std::sqrt( rx * rx + vy * vy );
        ax              = -K * m * rx;
        ay              = -g - K * m * vy;
	};
	if( hooks.eulerBeads )
	{
		double ax, ay;
		accel( t, b.x, b.vx, b.vy, ax, ay );
		b.x += b.vx * h;
		b.y += b.vy * h;
		b.vx += ax * h;
		b.vy += ay * h;
		return;
	}
	double a1x, a1y, a2x, a2y, a3x, a3y, a4x, a4y;
	accel( t, b.x, b.vx, b.vy, a1x, a1y );
	const double x2 = b.x + 0.5 * h * b.vx, vx2 = b.vx + 0.5 * h * a1x, vy2 = b.vy + 0.5 * h * a1y;
	accel( t + 0.5 * h, x2, vx2, vy2, a2x, a2y );
	const double x3 = b.x + 0.5 * h * vx2, vx3 = b.vx + 0.5 * h * a2x, vy3 = b.vy + 0.5 * h * a2y;
	accel( t + 0.5 * h, x3, vx3, vy3, a3x, a3y );
	const double x4 = b.x + h * vx3, vx4 = b.vx + h * a3x, vy4 = b.vy + h * a3y;
	accel( t + h, x4, vx4, vy4, a4x, a4y );
	b.x += h / 6.0 * ( b.vx + 2.0 * vx2 + 2.0 * vx3 + vx4 );
	b.y += h / 6.0 * ( b.vy + 2.0 * vy2 + 2.0 * vy3 + vy4 );
	b.vx += h / 6.0 * ( a1x + 2.0 * a2x + 2.0 * a3x + a4x );
	b.vy += h / 6.0 * ( a1y + 2.0 * a2y + 2.0 * a3y + a4y );
}

double Sim::updraft( const Spray& s ) const
{
	if( plumeScreen <= 1e-6 || plumeHead <= 1e-6 || s.x < barMin - 0.5 || s.x > barMax + 0.5 )
		return 0.0;
	const double top    = kPlumeShare * plumeHead;
	const double height = top * s.plume;
	if( s.y >= height || height <= 0.0 )
		return 0.0;
	const double u0 = std::sqrt( 2.0 * Gravity() * top * plumeScreen );
	return u0 * ( 1.0 - s.y / height );
}

void Sim::stepSpray( Spray& s, double t, double h ) const
{
	//Linear drag toward the air, whose velocity is held over the step: the
	//exact solution, so the step size changes nothing but the air's sampling.
	const double tau = water::kMistSettle / water::kGravity;
	const double ux  = hooks.mistIgnoresWind ? 0.0 : AirVelocity( s.x, t, inputs.wind, inputs.gust );
	const double uy  = updraft( s ) - water::kMistSettle;
	const double e   = std::exp( -h / tau );
	s.x += ux * h + ( s.vx - ux ) * tau * ( 1.0 - e );
	s.y += uy * h + ( s.vy - uy ) * tau * ( 1.0 - e );
	s.vx = ux + ( s.vx - ux ) * e;
	s.vy = uy + ( s.vy - uy ) * e;
}

void Sim::stepBeads( std::vector< Bead >& list, double from, double to ) const
{
	for( Bead& b : list )
		stepBead( b, from, to - from );
}

//---------------------------------------------------------------------------
void Sim::Advance( double dtIn, const SimInputs& in )
{
	inputs          = in;
	const double dt = quantise( std::clamp( dtIn, 0.0, kMaxFrame ) );

	if( in.layout != layout )
	{
		layout  = in.layout;
		nozzles = &NozzlesOf( layout );
		jets.assign( nozzles->size(), Jet {} );
		beads.clear();
		spray.clear();
		int mistCount = 0;
		double lo = 1e9, hi = -1e9;
		for( const Nozzle& n : *nozzles )
			if( n.role == Role::Mist )
			{
				++mistCount;
				lo = std::min( lo, n.x );
				hi = std::max( hi, n.x );
			}
		barSpacing = mistCount > 1 ? ( hi - lo ) / ( mistCount - 1 ) : 1.0;
	}
	const int count = static_cast< int >( jets.size() );
	barMin = 1e9;
	barMax = -1e9;
	for( int i = 0; i < count; ++i )
		if( ( *nozzles )[ static_cast< size_t >( i ) ].role == Role::Mist && active( i ) )
		{
			barMin = std::min( barMin, ( *nozzles )[ static_cast< size_t >( i ) ].x );
			barMax = std::max( barMax, ( *nozzles )[ static_cast< size_t >( i ) ].x );
		}

	frameStart = simTime;
	simTime += dt;
	const double t0 = frameStart, t1 = simTime;
	breakupAge      = water::BreakupAge( std::max( in.nozzle, 1e-5 ), in.lnDisturbance );

	//Pumps.
	double mistSum = 0.0;
	int mistRunning = 0;
	for( int i = 0; i < count; ++i )
	{
		const Nozzle& n = ( *nozzles )[ static_cast< size_t >( i ) ];
		Jet& jet        = jets[ static_cast< size_t >( i ) ];
		const double share = n.role == Role::Mist ? kMistHead : n.head;
		jet.headStart   = jet.head;
		jet.headCommand = active( i ) ? std::max( 0.0, in.pressure ) * share
		                                    * ( 1.0 - std::clamp( in.response, 0.0, 1.0 ) + std::clamp( in.response, 0.0, 1.0 ) * bandLevel( n.role ) )
		                              : 0.0;
		jet.head = headAt( jet, t1 );
		jet.openingStart = jet.opening;
		if( n.role == Role::Mist && active( i ) )
		{
			mistSum += jet.head;
			++mistRunning;
		}
	}
	mistHead = mistRunning > 0 ? mistSum / mistRunning : 0.0;
	//The plume: its air was set moving by the spray and keeps moving for a
	//while after the spray changes (a puff stays aloft, fttest --wind).
	plumeHead   = lagStep( plumeHead, mistHead, dt, kPlumeLag );
	plumeScreen = lagStep( plumeScreen, std::clamp( in.screen, 0.0, 1.0 ), dt, kPlumeLag );

	if( dt <= 0.0 )
		return;

	//Valves, then everything launched inside the frame.
	scheduleValves( dt );
	std::vector< Spray > sprayBorn;
	std::vector< double > times;
	const long long kFirst = static_cast< long long >( std::floor( t0 * kRate ) ) + 1;
	const long long kLast  = static_cast< long long >( std::floor( t1 * kRate + 1e-9 ) );
	for( int i = 0; i < count; ++i )
	{
		if( !active( i ) )
			continue;
		Jet& jet = jets[ static_cast< size_t >( i ) ];
		times.clear();
		for( long long k = kFirst; k <= kLast; ++k )
			times.push_back( static_cast< double >( k ) / kRate );
		//Valve edges inside the frame, its start included: a press or an onset
		//opens the valve AT the frame's start, and its first water leaves then.
		if( ( *nozzles )[ static_cast< size_t >( i ) ].role == Role::Shooter )
			for( const Interval& v : jet.open )
				for( double e : { v.start, v.end } )
					if( e >= t0 && e <= t1 )
						times.push_back( e );
		std::sort( times.begin(), times.end() );
		for( double t : times )
		{
			if( t <= jet.emittedTo + 1e-12 )
				continue;//launched already (a grid time or an edge the last frame ended on)
			jet.emittedTo = t;
			emit( i, t, sprayBorn );
		}
		jet.opening = ( *nozzles )[ static_cast< size_t >( i ) ].role == Role::Shooter ? openingAt( jet, t1 ) : 1.0;
	}
	std::sort( sprayBorn.begin(), sprayBorn.end(), []( const Spray& a, const Spray& b ) { return a.born < b.born; } );

	//The substeps: pinch-offs at their own moments, then flight.
	const int substeps = std::max( 1, static_cast< int >( std::ceil( dt * kRate - 1e-9 ) ) );
	const double h     = dt / substeps;
	size_t nextSpray   = 0;
	std::vector< Bead > bornBeads;
	for( int k = 0; k < substeps; ++k )
	{
		const double ta = t0 + k * h;
		const double tb = k + 1 == substeps ? t1 : t0 + ( k + 1 ) * h;
		bornBeads.clear();
		for( int i = 0; i < count; ++i )
			if( active( i ) && ( *nozzles )[ static_cast< size_t >( i ) ].role != Role::Mist )
				pinch( i, tb - breakupAge, ta, bornBeads );
		stepBeads( beads, ta, tb );
		for( Bead& b : bornBeads )
		{
			if( tb > b.born )
				stepBead( b, b.born, tb - b.born );
			beads.push_back( b );
		}
		for( Spray& s : spray )
			stepSpray( s, ta, tb - ta );
		while( nextSpray < sprayBorn.size() && sprayBorn[ nextSpray ].born <= tb )
		{
			Spray s = sprayBorn[ nextSpray++ ];
			if( tb > s.born )
				stepSpray( s, s.born, tb - s.born );
			spray.push_back( s );
		}
	}
	beads.erase( std::remove_if( beads.begin(), beads.end(), []( const Bead& b ) { return b.y < 0.0 || std::fabs( b.x ) > 500.0; } ), beads.end() );
	spray.erase( std::remove_if( spray.begin(), spray.end(),
	                             [&]( const Spray& s ) {
		                             return s.y < 0.0 || std::fabs( s.x ) > 500.0
		                                    || std::exp( -( t1 - s.born ) / water::kMistEvaporate ) < kSprayGone;
	                             } ),
	             spray.end() );

	//Parcels nobody needs: older than the pinch-off, or under the waterline
	//(one is kept, as the end of the column the shader clips there).
	for( int i = 0; i < count; ++i )
	{
		Jet& jet  = jets[ static_cast< size_t >( i ) ];
		auto& p   = jet.parcels;
		auto gone = [&]( const Parcel& q ) {
			double x, y, vx, vy;
			const double age = t1 - q.t0;
			Material( i, q.speed, age, x, y, vx, vy );
			return q.t0 <= jet.brokenTo || ( age > 0.0 && y < 0.0 );
		};
		while( p.size() >= 2 && ( gone( p[ 1 ] ) || ( p[ 0 ].stream != p[ 1 ].stream && gone( p[ 0 ] ) ) ) )
			p.pop_front();
		if( p.size() == 1 && !jet.emitting && gone( p[ 0 ] ) )
			p.pop_front();
	}
}

//---------------------------------------------------------------------------
void Sim::Collect( std::vector< SegmentInstance >& segments, std::vector< BeadInstance >& beadsOut, std::vector< SprayInstance >& sprayOut ) const
{
	segments.clear();
	beadsOut.clear();
	sprayOut.clear();
	const double t1 = simTime;
	const double a0 = inputs.nozzle;

	struct Vertex
	{
		double x, y, w;
	};
	std::vector< Vertex > run;
	auto vertex = [&]( int i, double speed, double age ) {
		double x, y, vx, vy;
		Material( i, speed, age, x, y, vx, vy );
		const double now = std::sqrt( vx * vx + vy * vy );
		const double w   = a0 * std::min( kMaxWidening, std::sqrt( speed / std::max( now, 1e-9 ) ) );
		return Vertex { x, y, w };
	};
	auto flush = [&]( int i ) {
		for( size_t k = 0; k + 1 < run.size(); ++k )
			segments.push_back( { static_cast< float >( run[ k ].x ), static_cast< float >( run[ k ].y ), static_cast< float >( run[ k + 1 ].x ),
			                      static_cast< float >( run[ k + 1 ].y ), static_cast< float >( run[ k ].w ), static_cast< float >( run[ k + 1 ].w ),
			                      static_cast< float >( i ), 0.0f } );
		run.clear();
	};

	for( int i = 0; i < static_cast< int >( jets.size() ); ++i )
	{
		if( ( *nozzles )[ static_cast< size_t >( i ) ].role == Role::Mist )
			continue;
		const Jet& jet  = jets[ static_cast< size_t >( i ) ];
		const auto& p   = jet.parcels;
		unsigned stream = 0;
		bool open       = false;
		for( size_t k = 0; k < p.size(); ++k )
		{
			const Parcel& q = p[ k ];
			if( q.t0 <= jet.brokenTo )
				continue;
			if( !open || q.stream != stream )
			{
				flush( i );
				open   = true;
				stream = q.stream;
				//The column starts where it pinched off: the material of age
				//exactly t_b, between the last broken parcel and this one.
				if( k > 0 && p[ k - 1 ].stream == q.stream && p[ k - 1 ].t0 <= jet.brokenTo )
				{
					const Parcel& A = p[ k - 1 ];
					const double f  = ( jet.brokenTo - A.t0 ) / ( q.t0 - A.t0 );
					run.push_back( vertex( i, A.speed + f * ( q.speed - A.speed ), t1 - jet.brokenTo ) );
				}
			}
			run.push_back( vertex( i, q.speed, t1 - q.t0 ) );
		}
		//And it ends at the nozzle while the nozzle is still running.
		if( open && jet.emitting && stream == jet.stream && !p.empty() && p.back().t0 < t1 )
		{
			const double s = ( *nozzles )[ static_cast< size_t >( i ) ].role == Role::Shooter ? jet.opening : 1.0;
			const double v = SpeedFor( i, s, jet.head );
			if( s >= kEmitFloor && v >= 0.05 )
				run.push_back( vertex( i, v, 0.0 ) );
		}
		flush( i );
	}

	for( const Bead& b : beads )
		beadsOut.push_back( { static_cast< float >( b.x ), static_cast< float >( b.y ), static_cast< float >( b.radius ), static_cast< float >( b.nozzle ) } );

	for( const Spray& s : spray )
	{
		const double age    = t1 - s.born;
		const double sigma2 = water::kMistSigma0 * water::kMistSigma0 + 2.0 * water::kMistDiffusion * age;
		const double volume = s.volume * std::exp( -age / water::kMistEvaporate );
		const double tau    = water::kTauPerVolumeOverRadius * volume / water::kMistDropRadius;
		sprayOut.push_back( { static_cast< float >( s.x ), static_cast< float >( s.y ), static_cast< float >( std::sqrt( sigma2 ) ), static_cast< float >( tau ),
		                      static_cast< float >( s.nozzle ), 0.0f, 0.0f, 0.0f } );
	}
}

} // namespace fountain
