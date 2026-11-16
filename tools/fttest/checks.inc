// The checks. Included into main.cpp's anonymous namespace.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the stated law; `--negative` sets one field at a time to a
// deliberately wrong MODEL -- a hook in the plugin, so the shipped code
// computes the wrong thing -- and requires the check to FAIL.

//===========================================================================
// The harness's own physics. Nothing here calls Water.cpp: every number is
// computed again from the published formula (ATTRIBUTIONS.md has the
// sources), with a different Bessel routine and a different maximiser.
//===========================================================================
namespace ref
{
/// Standard gravity, 3rd CGPM (1901): 980.665 cm/s^2.
constexpr double g = 9.80665;

/// IAPWS R1-76(2014) at 20 degC.
double sigma20()
{
	const double tau = 1.0 - 293.15 / 647.096;
	return 235.8e-3 * std::pow( tau, 1.256 ) * ( 1.0 - 0.625 * tau );
}

/// Tanaka et al. (2001) eq. (1) at 20 degC.
double rho20()
{
	const double t = 20.0;
	return 999.974950 * ( 1.0 - ( t - 3.983035 ) * ( t - 3.983035 ) * ( t + 301.797 ) / ( 522528.9 * ( t + 69.34881 ) ) );
}

/// I_n(x) = (1/pi) int_0^pi exp( x cos t ) cos( n t ) dt, by the trapezoid
/// rule, which converges geometrically for a smooth periodic integrand. Not
/// the power series Water.cpp and constants.py use.
double besselI( int n, double x )
{
	constexpr int N = 256;
	double sum = 0.0;
	for( int k = 0; k <= N; ++k )
	{
		const double t = kPi * k / N;
		const double w = ( k == 0 || k == N ) ? 0.5 : 1.0;
		sum += w * std::exp( x * std::cos( t ) ) * std::cos( n * t );
	}
	return sum / N;
}

/// Rayleigh's q^2 in units of sigma / ( rho a^3 ): ( 1 - x^2 ) x I1(x) / I0(x),
/// or with `longWave` the long-wave approximation ( 1 - x^2 ) x^2 / 2.
double growth2( double x, bool longWave = false )
{
	if( longWave )
		return ( 1.0 - x * x ) * x * x / 2.0;
	return ( 1.0 - x * x ) * x * besselI( 1, x ) / besselI( 0, x );
}

struct Rayleigh
{
	double x, rate, lambda, drop;
};

/// The maximum by bisection on the central-difference derivative (Water.cpp
/// uses golden section on the function).
Rayleigh rayleigh()
{
	auto slope = []( double x ) {
		const double h = 1e-6;
		return growth2( x + h ) - growth2( x - h );
	};
	double lo = 0.3, hi = 0.95;
	for( int i = 0; i < 100; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( slope( mid ) > 0.0 ? lo : hi ) = mid;
	}
	Rayleigh r;
	r.x      = 0.5 * ( lo + hi );
	r.rate   = std::sqrt( growth2( r.x ) );
	r.lambda = 2.0 * kPi / r.x;
	r.drop   = std::cbrt( 0.75 * r.lambda );
	return r;
}
const Rayleigh& R()
{
	static const Rayleigh r = rayleigh();
	return r;
}

/// The fastest growth rate, 1/s, of a column of radius a.
double rate( double a )
{
	return R().rate * std::sqrt( sigma20() / ( rho20() * a * a * a ) );
}
double breakupAge( double a, double lnDisturbance )
{
	return lnDisturbance / rate( a );
}

/// Atlas et al. (1973)'s fit to Gunn and Kinzer (1949), D clamped to its range.
double terminalVelocity( double diameterMetres )
{
	const double d = std::clamp( diameterMetres * 1e3, 0.5, 5.0 );
	return 9.65 - 10.3 * std::exp( -0.6 * d );
}

/// The gusts, as AGENTS.md documents them (a chosen model; typed in here from
/// the documentation, not called from Sim.cpp).
double air( double x, double t, double wind, double gust )
{
	if( gust <= 0.0 )
		return wind;
	const double length[ 3 ] = { 23.0, 9.7, 4.1 }, period[ 3 ] = { 11.0, 5.3, 2.9 }, weight[ 3 ] = { 0.5, 0.3, 0.2 }, phase[ 3 ] = { 0.3, 2.1, 4.4 };
	const double xi = x - wind * t;
	double s        = 0.0;
	for( int i = 0; i < 3; ++i )
		s += weight[ i ] * std::sin( 2.0 * kPi * ( xi / length[ i ] - t / period[ i ] ) + phase[ i ] );
	return wind + gust * ( 2.0 + std::fabs( wind ) ) * s;
}
} // namespace ref

//===========================================================================
// Shared by the checks.
//===========================================================================

constexpr int kRowCentre  = 5;///< Row: the shooter at x = 0
constexpr int kShowCentre = 3;///< Show: the tallest shooter, x = 0

/// The quiet basin: nothing fires, no audio, Response 0 (the head is Pressure
/// x the nozzle's share), a 12 mm nozzle that never breaks up in flight
/// (ln 20: t_b = 9 s), no lag on the valve, the pump settled in a twentieth of
/// a second, still air, no mist, White lamps at Exposure 1. Every check then
/// sets what it measures (honeydew's lesson: a check sets every control it
/// depends on).
void quiet( Rig& rig )
{
	rig.Set( PT_LAYOUT, static_cast< int >( LayoutId::Row ) );
	rig.Set( PT_PRESSURE, ParamFromPressure( 4.0 ) );
	rig.Set( PT_RESPONSE, 0.0f );
	rig.Set( PT_FIRE_ON, static_cast< int >( FireOn::Onset ) );
	rig.Set( PT_SHOT, ParamFromShot( 0.15 ) );
	rig.Set( PT_LEAD, 0.0f );
	rig.Set( PT_SENSITIVITY, 0.5f );
	rig.Set( PT_NOZZLE, ParamFromNozzle( 12e-3 ) );
	rig.Set( PT_BREAKUP, 0.0f );
	rig.Set( PT_VALVE, 0.0f );
	rig.Set( PT_PUMP, 0.0f );
	rig.Set( PT_WIND, ParamFromWind( 0.0 ) );
	rig.Set( PT_GUST, 0.0f );
	rig.Set( PT_SCREEN, 0.0f );
	rig.Set( PT_VIEW, ParamFromView( 10.0 ) );
	rig.Set( PT_LAMPS, static_cast< int >( Lamps::White ) );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 1.0 ) );
	rig.Set( PT_MIX, 1.0f );
}

/// Every control back at its shipped default.
void defaults( Rig& rig )
{
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		if( info.kind == Kind::Standard || info.kind == Kind::Option )
			rig.Set( id, info.defaultValue );
	}
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, SimHooks base, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	if( perturb.sim )
		perturb.sim( base );
	rig.plugin.SimForTest().hooks = base;
	rig.plugin.SetResizeClearsForTest( perturb.resizeClears );
	rig.plugin.SetFloatClockForTest( perturb.floatClock );
	rig.plugin.SetUnprimedForTest( perturb.unprimed );
	rig.plugin.SetAdditiveForTest( perturb.additive );
	quiet( rig );
	return true;
}

/// The plugin's clock for frame f of a rig started at frame 0: what the Sim
/// calls its time, the sum of the frames' durations.
double simTimeOf( const Rig& rig, int f )
{
	return rig.Seconds( f ) - rig.Seconds( 0 );
}

/// y = c0 + c1 u + c2 u^2 by least squares; returns false if singular.
bool fitParabola( const std::vector< double >& u, const std::vector< double >& y, double c[ 3 ] )
{
	double s[ 5 ] = {}, t[ 3 ] = {};
	for( size_t i = 0; i < u.size(); ++i )
	{
		double p = 1.0;
		for( int k = 0; k < 5; ++k )
		{
			if( k < 3 )
				t[ k ] += p * y[ i ];
			s[ k ] += p;
			p *= u[ i ];
		}
	}
	double m[ 3 ][ 4 ] = { { s[ 0 ], s[ 1 ], s[ 2 ], t[ 0 ] }, { s[ 1 ], s[ 2 ], s[ 3 ], t[ 1 ] }, { s[ 2 ], s[ 3 ], s[ 4 ], t[ 2 ] } };
	for( int col = 0; col < 3; ++col )
	{
		int pivot = col;
		for( int r = col + 1; r < 3; ++r )
			if( std::fabs( m[ r ][ col ] ) > std::fabs( m[ pivot ][ col ] ) )
				pivot = r;
		if( std::fabs( m[ pivot ][ col ] ) < 1e-300 )
			return false;
		for( int k = 0; k < 4; ++k )
			std::swap( m[ col ][ k ], m[ pivot ][ k ] );
		for( int r = 0; r < 3; ++r )
			if( r != col )
			{
				const double f = m[ r ][ col ] / m[ col ][ col ];
				for( int k = col; k < 4; ++k )
					m[ r ][ k ] -= f * m[ col ][ k ];
			}
	}
	for( int k = 0; k < 3; ++k )
		c[ k ] = m[ k ][ 3 ] / m[ k ][ k ];
	return true;
}

/// The column sums of tau over every row above the waterline.
std::vector< double > columnSums( const Picture& p )
{
	std::vector< double > s( static_cast< size_t >( p.w ), 0.0 );
	for( int y = p.waterRow; y < p.h; ++y )
		for( int x = 0; x < p.w; ++x )
			s[ static_cast< size_t >( x ) ] += p.at( x, y );
	return s;
}

//===========================================================================
// --offline: the constants.
//===========================================================================
int runConstants( const Perturb& perturb )
{
	std::printf( "\n=== constants: every number in Water.h, re-derived here from its source\n" );
	const ref::Rayleigh& r = ref::R();
	auto rel               = []( double a, double b ) { return std::fabs( a - b ) / std::fabs( b ); };
	Check( rel( water::kRayleighX, r.x ) < 1e-9, fmt( "Rayleigh's fastest x = ka: Water.h %.12f, harness %.12f (trapezoid Bessel, bisection on the slope)", water::kRayleighX, r.x ) );
	Check( rel( water::kRayleighRate, r.rate ) < 1e-9, fmt( "its growth rate: Water.h %.12f, harness %.12f sqrt( sigma / rho a^3 )", water::kRayleighRate, r.rate ) );
	Check( rel( water::kRayleighLambda, r.lambda ) < 1e-9, fmt( "lambda / a: Water.h %.9f, harness %.9f (Rayleigh's (18): 4.508 x 2 = 9.016)", water::kRayleighLambda, r.lambda ) );
	Check( rel( water::kDropRadius, r.drop ) < 1e-9, fmt( "a wavelength gathered into one drop, r / a: Water.h %.9f, harness %.9f", water::kDropRadius, r.drop ) );
	Check( std::fabs( r.x * r.x - 0.4858 ) <= 0.5e-4 + 1e-12, fmt( "x^2 = %.6f against Rayleigh's printed .4858 (his (17), rounded to 1e-4)", r.x * r.x ) );

	//His table under (18): x^2 and the square root of (13).
	const double table[][ 2 ] = { { 0.05, 0.1536 }, { 0.1, 0.2108 }, { 0.2, 0.2794 }, { 0.3, 0.3182 }, { 0.4, 0.3382 },
		                          { 0.5, 0.3432 },  { 0.6, 0.3344 }, { 0.8, 0.2701 }, { 0.9, 0.2015 } };
	double worst = 0.0;
	for( const auto& row : table )
		worst = std::max( worst, std::fabs( std::sqrt( ref::growth2( std::sqrt( row[ 0 ] ), perturb.longWave ) ) - row[ 1 ] ) );
	Check( worst <= 0.5e-4 + 1e-12, fmt( "Rayleigh's printed table, nine rows, reproduced to %.2g (his figures are rounded to 1e-4)%s", worst,
	                                     perturb.longWave ? " -- with the LONG-WAVE formula" : "" ) );

	Check( rel( water::kSurfaceTension, ref::sigma20() ) < 1e-12 && std::fabs( ref::sigma20() * 1e3 - 72.74 ) <= 0.005,
	       fmt( "surface tension at 20 C: %.6f mN/m (IAPWS R1-76(2014) formula; its table, 72.74)", ref::sigma20() * 1e3 ) );
	Check( rel( water::kDensity, ref::rho20() ) < 1e-12 && std::fabs( ref::rho20() - 998.2067 ) <= 0.00005,
	       fmt( "density at 20 C: %.5f kg/m^3 (Tanaka et al. 2001 eq. (1); their table, 998.2067)", ref::rho20() ) );
	Check( water::kGravity == ref::g, fmt( "standard gravity %.5f m/s^2 (CGPM 1901)", water::kGravity ) );

	//What the running model uses, hooks included.
	Sim sim;
	if( perturb.sim )
		perturb.sim( sim.hooks );
	SimInputs in;
	in.nozzle        = 3e-3;
	in.lnDisturbance = 11.0;
	sim.Advance( 1.0 / 60.0, in );
	const double age = ref::breakupAge( in.nozzle, in.lnDisturbance );
	Check( rel( sim.WavelengthFactor(), r.lambda ) < 1e-9 && rel( sim.DropFactor(), r.drop ) < 1e-9 && rel( sim.BreakupAgeNow(), age ) < 1e-9,
	       fmt( "the running Sim: lambda/a %.6f, r/a %.6f, breakup age at a = 3 mm, ln 11: %.6f s (harness %.6f s)", sim.WavelengthFactor(),
	            sim.DropFactor(), sim.BreakupAgeNow(), age ) );
	return Verdict();
}

//===========================================================================
// --apex
//===========================================================================
int runApex( const Perturb& perturb )
{
	std::printf( "\n=== apex: a steady vertical jet, drag off, tops out at its head: v = sqrt( 2 g H ) gives H = v^2 / 2g\n" );
	for( const Raster& raster : kRasters )
		for( double head : { 2.0, 4.5, 7.0 } )
		{
			Rig rig;
			SimHooks base;
			base.solo      = 1ull << kRowCentre;
			base.dragScale = 0.0;
			if( !startRig( rig, raster.w, raster.h, perturb, base ) )
				return 1;
			rig.Set( PT_FIRE_ON, static_cast< int >( FireOn::Steady ) );
			rig.Set( PT_PRESSURE, ParamFromPressure( head ) );
			if( !rig.Render( 150 ) )
				return 1;
			const Picture pic = pictureOf( rig );
			const double H    = rig.plugin.State().Inputs().pressure;
			const double top  = stripTop( pic, pic.px( 0.0 ), 8 );
			const double want = pic.py( H );
			Check( std::fabs( top - want ) <= 1.0, fmt( "%4dx%-4d H %.2f m: the column's top at %.2f px, H at %.2f px (%+.2f px; tolerance 1 px, the end's box filter)",
			                                             raster.w, raster.h, H, top, want, top - want ) );
		}
	return Verdict();
}

//===========================================================================
// --range
//===========================================================================
int runRange( const Perturb& perturb )
{
	std::printf( "\n=== range: a steady arc, drag off, falls along the closed-form parabola -- centroid of each row of its descending branch\n" );
	for( const Raster& raster : kRasters )
		for( int index : { 1, 2 } )
		{
			Rig rig;
			SimHooks base;
			base.solo      = 1ull << index;
			base.dragScale = 0.0;
			if( !startRig( rig, raster.w, raster.h, perturb, base ) )
				return 1;
			rig.Set( PT_LAYOUT, static_cast< int >( LayoutId::Fan ) );
			rig.Set( PT_PRESSURE, ParamFromPressure( 4.0 ) );
			if( !rig.Render( 180 ) )
				return 1;
			const Picture pic  = pictureOf( rig );
			const Nozzle& n    = NozzlesOf( LayoutId::Fan )[ static_cast< size_t >( index ) ];
			const double H     = rig.plugin.State().Inputs().pressure * n.head;
			const double V     = std::sqrt( 2.0 * ref::g * H );
			const double c     = std::cos( n.angle ), s = std::sin( n.angle );
			const double apexX = n.x + V * V * s * c / ref::g, apexY = V * V * s * s / ( 2.0 * ref::g );
			double worst       = 0.0;
			int rows           = 0;
			bool empty         = false;
			for( int r = pic.waterRow + 2; r < pic.h; ++r )
			{
				const double y = ( r + 0.5 - pic.oy ) / pic.scale;
				if( y > 0.75 * apexY )
					break;
				const double vy = std::sqrt( std::max( 0.0, V * V * s * s - 2.0 * ref::g * y ) );
				if( vy < 0.5 * std::fabs( V * c ) )
					continue;
				const double xd = n.x + V * c * ( V * s + vy ) / ref::g;
				double sum = 0.0, moment = 0.0;
				for( int x = static_cast< int >( std::floor( pic.px( apexX ) ) ); x < pic.w; ++x )
				{
					sum += pic.at( x, r );
					moment += ( x + 0.5 ) * pic.at( x, r );
				}
				if( sum <= 0.0 )
				{
					empty = true;
					continue;
				}
				worst = std::max( worst, std::fabs( moment / sum - pic.px( xd ) ) );
				++rows;
			}
			Check( !empty && rows >= 8 && worst <= 1.0, fmt( "%4dx%-4d arc at %.0f deg, H %.2f m: %d rows, worst centroid %.3f px from the parabola (tolerance 1 px)%s",
			                                                  raster.w, raster.h, n.angle * 180.0 / kPi, H, rows, worst, empty ? "; a row had no water" : "" ) );
		}
	return Verdict();
}

//===========================================================================
// --flight
//===========================================================================
int runFlight( const Perturb& perturb )
{
	std::printf( "\n=== flight: a shot with no valve lag -- its top rises as V t - g t^2 / 2, and a tilted shot lands at 2 V sin(theta) / g\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo      = 1ull << kRowCentre;
		base.dragScale = 0.0;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_PRESSURE, ParamFromPressure( 10.0 ) );
		rig.Set( PT_VIEW, ParamFromView( 20.0 ) );
		rig.Set( PT_SHOT, ParamFromShot( 1.0 ) );
		if( !rig.Render( 30 ) || !rig.Press( PT_FIRE ) )
			return 1;
		const double fired = simTimeOf( rig, rig.frame - 2 );//the press's frame began here
		const Sim& sim     = rig.plugin.State();
		const bool when    = !sim.ShotTimes( kRowCentre ).empty() && std::fabs( sim.ShotTimes( kRowCentre ).back() - fired ) < 1e-6;
		const double H     = sim.Inputs().pressure;
		const double V     = std::sqrt( 2.0 * ref::g * H );
		double worst       = 0.0;
		int frames         = 0;
		for( ;; )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const double age = simTimeOf( rig, rig.frame - 1 ) - fired;
			if( age > 0.92 * V / ref::g )
				break;
			const double y    = V * age - 0.5 * ref::g * age * age;
			const Picture pic = pictureOf( rig );
			if( pic.py( y ) < pic.waterRow + 3 )
				continue;
			worst = std::max( worst, std::fabs( stripTop( pic, pic.px( 0.0 ), 6 ) - pic.py( y ) ) );
			++frames;
		}
		Check( when && frames >= 30 && worst <= 1.0, fmt( "%4dx%-4d H %.1f m: %d frames of the rise, worst %.3f px from V t - g t^2 / 2 (tolerance 1 px); fired at the press's frame start: %s",
		                                                  raster.w, raster.h, H, frames, worst, when ? "yes" : "NO" ) );
	}
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo        = 1ull << kRowCentre;
		base.dragScale   = 0.0;
		base.angleOffset = -kPi / 6.0;//the test tilts the shooter to 60 degrees
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_PRESSURE, ParamFromPressure( 10.0 ) );
		rig.Set( PT_VIEW, ParamFromView( 20.0 ) );
		rig.Set( PT_SHOT, ParamFromShot( 0.04 ) );
		if( !rig.Render( 30 ) || !rig.Press( PT_FIRE ) )
			return 1;
		const double fired = simTimeOf( rig, rig.frame - 2 );
		const double H     = rig.plugin.State().Inputs().pressure;
		const double V     = std::sqrt( 2.0 * ref::g * H );
		const double theta = kPi / 3.0;
		const double T     = 2.0 * V * std::sin( theta ) / ref::g;
		const double land  = V * std::cos( theta ) * T;
		const int expected = static_cast< int >( std::ceil( ( fired + T ) * rig.fps - 1e-9 ) );
		int seen           = -1;
		while( rig.frame <= expected + 6 && seen < 0 )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const Picture pic = pictureOf( rig );
			const int xc      = static_cast< int >( std::floor( pic.px( land ) ) );
			for( int x = xc - 6; x <= xc + 6 && seen < 0; ++x )
				if( pic.at( x, pic.waterRow ) > 1e-6 )
					seen = rig.frame - 1;
		}
		Check( seen >= 0 && std::abs( seen - expected ) <= 1,
		       fmt( "%4dx%-4d a 40 ms shot at 60 deg, H %.1f m: first water at the basin on frame %d, 2 V sin(theta) / g after the press says frame %d "
		            "(%.4f s; tolerance 1 frame)",
		            raster.w, raster.h, H, seen, expected, T ) );
	}
	return Verdict();
}

//===========================================================================
// --drag
//===========================================================================
int runDrag( const Perturb& perturb )
{
	std::printf( "\n=== drag: a 1 mm jet that breaks up early tops out at y_b + ( v_t^2 / 2g ) ln( 1 + v_b^2 / v_t^2 ), well below H\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo = 1ull << kRowCentre;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_FIRE_ON, static_cast< int >( FireOn::Steady ) );
		rig.Set( PT_PRESSURE, ParamFromPressure( 4.0 ) );
		rig.Set( PT_NOZZLE, ParamFromNozzle( 1e-3 ) );
		rig.Set( PT_BREAKUP, 1.0f );
		rig.Set( PT_VIEW, ParamFromView( 4.0 ) );
		if( !rig.Render( 180 ) )
			return 1;
		const Picture pic = pictureOf( rig );
		const SimInputs& in = rig.plugin.State().Inputs();
		const double H    = in.pressure;
		const double V    = std::sqrt( 2.0 * ref::g * H );
		const double tb   = ref::breakupAge( in.nozzle, in.lnDisturbance );
		const double vb = V - ref::g * tb, yb = V * tb - 0.5 * ref::g * tb * tb;
		const double rb   = ref::R().drop * in.nozzle;
		const double vt   = ref::terminalVelocity( 2.0 * rb );
		const double apex = yb + vt * vt / ( 2.0 * ref::g ) * std::log( 1.0 + vb * vb / ( vt * vt ) );
		//The top bead lights every pixel centre within its shown radius + 1/2.
		const double reach = std::max( rb * pic.scale, 0.5 ) + 0.5;
		const int row      = highestRow( pic, pic.px( 0.0 ), 10, 1e-4 );
		const double top   = row + 0.5 - ( reach - 0.5 );
		Check( row >= 0 && std::fabs( top - pic.py( apex ) ) <= 1.5 && top < pic.py( H ) - 10.0,
		       fmt( "%4dx%-4d breaks up at %.4f s, %.3f m, %.3f m/s; beads %.2f mm, v_t %.3f m/s: top %.2f px, closed form %.2f px (%+.2f; "
		            "tolerance 1.5 px, a bead's disc and the box filter) -- drag-free H would be %.2f px",
		            raster.w, raster.h, tb, yb, vb, 2e3 * rb, vt, top, pic.py( apex ), top - pic.py( apex ), pic.py( H ) ) );
	}
	return Verdict();
}

//===========================================================================
// --rk4
//===========================================================================
int runRK4( const Perturb& perturb )
{
	std::printf( "\n=== rk4: every bead in wind and gusts against the harness's own RK4 at 1/64 of the plugin's step (state)\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo = 0x1EFull;//the Fan's eight arcs
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_LAYOUT, static_cast< int >( LayoutId::Fan ) );
		rig.Set( PT_PRESSURE, ParamFromPressure( 6.0 ) );
		rig.Set( PT_NOZZLE, ParamFromNozzle( 2e-3 ) );
		rig.Set( PT_BREAKUP, 1.0f );
		rig.Set( PT_WIND, ParamFromWind( 3.0 ) );
		rig.Set( PT_GUST, 0.5f );
		if( !rig.Render( 240 ) )
			return 1;
		const Sim& sim      = rig.plugin.State();
		const SimInputs& in = sim.Inputs();
		const double now    = sim.Time();
		const double vt     = ref::terminalVelocity( 2.0 * ref::R().drop * in.nozzle );
		const double K      = ref::g / ( vt * vt );
		double worst = 0.0, worstK = 0.0;
		int count    = 0;
		const auto& beads = sim.Beads();
		for( size_t i = 0; i < beads.size(); i += 7 )
		{
			const Bead& b = beads[ i ];
			worstK        = std::max( worstK, std::fabs( b.drag - K ) / K );
			double x = b.x0, y = b.y0, vx = b.vx0, vy = b.vy0, t = b.born;
			const int steps = std::max( 1, static_cast< int >( std::ceil( ( now - b.born ) * Sim::kRate * 64.0 ) ) );
			const double h  = ( now - b.born ) / steps;
			auto accel      = [ & ]( double tt, double xx, double ux, double uy, double& ax, double& ay ) {
                const double rx = ux - ref::air( xx, tt, in.wind, in.gust );
                const double m  = std::sqrt( rx * rx + uy * uy );
                ax              = -K * m * rx;
                ay              = -ref::g - K * m * uy;
			};
			for( int k = 0; k < steps; ++k )
			{
				double a1x, a1y, a2x, a2y, a3x, a3y, a4x, a4y;
				accel( t, x, vx, vy, a1x, a1y );
				accel( t + h / 2, x + h / 2 * vx, vx + h / 2 * a1x, vy + h / 2 * a1y, a2x, a2y );
				accel( t + h / 2, x + h / 2 * ( vx + h / 2 * a1x ), vx + h / 2 * a2x, vy + h / 2 * a2y, a3x, a3y );
				accel( t + h, x + h * ( vx + h / 2 * a2x ), vx + h * a3x, vy + h * a3y, a4x, a4y );
				x += h / 6 * ( vx + 2 * ( vx + h / 2 * a1x ) + 2 * ( vx + h / 2 * a2x ) + ( vx + h * a3x ) );
				y += h / 6 * ( vy + 2 * ( vy + h / 2 * a1y ) + 2 * ( vy + h / 2 * a2y ) + ( vy + h * a3y ) );
				vx += h / 6 * ( a1x + 2 * a2x + 2 * a3x + a4x );
				vy += h / 6 * ( a1y + 2 * a2y + 2 * a3y + a4y );
				t += h;
			}
			worst = std::max( worst, std::hypot( x - b.x, y - b.y ) );
			++count;
		}
		Check( count >= 100 && worst <= 1e-4 && worstK <= 1e-9,
		       fmt( "%4dx%-4d %d beads (every 7th of %zu) at wind %.1f m/s, gust %.2f: worst %.2e m from the reference (tolerance 0.1 mm); drag constant %.5f 1/m "
		            "(harness's v_t %.4f m/s; worst %.1e relative)",
		            raster.w, raster.h, count, beads.size(), in.wind, in.gust, worst, K, vt, worstK ) );
	}
	return Verdict();
}

//===========================================================================
// --breakup
//===========================================================================
int runBreakup( const Perturb& perturb )
{
	std::printf( "\n=== breakup: drag off, an arc's beads are one Rayleigh wavelength of material apart -- 9.0144 a cos(theta) on the picture --\n"
	             "    its column ends at the closed-form age t_b = ln( a / delta0 ) / q_max, and an arc that lands sooner has no beads\n" );
	const int index = 0;//the Fan's 50-degree arc
	const Nozzle& n = NozzlesOf( LayoutId::Fan )[ static_cast< size_t >( index ) ];
	const double c = std::cos( n.angle ), s = std::sin( n.angle );
	auto setUp = [ & ]( Rig& rig, const Raster& raster, int mask ) {
		SimHooks base;
		base.solo      = 1ull << index;
		base.dragScale = 0.0;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return false;
		rig.plugin.SetDrawMaskForTest( mask );
		rig.Set( PT_LAYOUT, static_cast< int >( LayoutId::Fan ) );
		rig.Set( PT_PRESSURE, ParamFromPressure( 0.617 ) );
		rig.Set( PT_NOZZLE, ParamFromNozzle( 6e-3 ) );
		rig.Set( PT_BREAKUP, 1.0f );
		rig.Set( PT_VIEW, ParamFromView( 1.0 ) );
		return rig.Render( 120 );
	};
	for( const Raster& raster : kRasters )
	{
		//The spacing.
		Rig rig;
		if( !setUp( rig, raster, 7 ) )
			return 1;
		const Picture pic   = pictureOf( rig );
		const SimInputs& in = rig.plugin.State().Inputs();
		const double H      = in.pressure * n.head;
		const double V      = std::sqrt( 2.0 * ref::g * H );
		const double tb     = ref::breakupAge( in.nozzle, in.lnDisturbance );
		const double spacing = ref::R().lambda * in.nozzle * c * pic.scale;
		const double rb      = ref::R().drop * in.nozzle;
		const double landX   = n.x + V * V * 2.0 * s * c / ref::g;
		//From one wavelength past the column's end (the newest bead sits within
		//it) to where the arc comes within a bead and a pixel of the waterline.
		const double from = pic.px( n.x + V * c * tb ) + spacing;
		double to         = pic.px( landX );
		for( double x = from; x < pic.px( landX ); x += 0.25 )
		{
			const double t = ( ( x - pic.ox ) / pic.scale - n.x ) / ( V * c );
			if( V * s * t - 0.5 * ref::g * t * t < rb + 1.0 / pic.scale )
			{
				to = x;
				break;
			}
		}
		const std::vector< double > P = columnSums( pic );
		const int x0 = static_cast< int >( std::ceil( from ) ), x1 = static_cast< int >( std::floor( to ) );
		double peak = 0.0;
		for( int x = x0; x <= x1; ++x )
			peak = std::max( peak, P[ static_cast< size_t >( x ) ] );
		//Blobs between local minima, each one's centroid.
		std::vector< double > centres;
		int start = x0;
		for( int x = x0 + 1; x <= x1; ++x )
		{
			const bool minimum = x == x1 || ( P[ static_cast< size_t >( x ) ] <= P[ static_cast< size_t >( x - 1 ) ] && P[ static_cast< size_t >( x ) ] < P[ static_cast< size_t >( x + 1 ) ] );
			if( !minimum )
				continue;
			double sum = 0.0, moment = 0.0, top = 0.0;
			for( int k = start; k <= x; ++k )
			{
				const double w = ( k == start || k == x ) ? 0.5 : 1.0;
				sum += w * P[ static_cast< size_t >( k ) ];
				moment += w * ( k + 0.5 ) * P[ static_cast< size_t >( k ) ];
				top = std::max( top, P[ static_cast< size_t >( k ) ] );
			}
			//A whole blob only: not cut by the window's ends.
			if( top > 0.25 * peak && start > x0 && x < x1 )
				centres.push_back( moment / sum );
			start = x;
		}
		double slope = 0.0;
		const int count = static_cast< int >( centres.size() );
		if( count >= 2 )
		{
			double su = 0, sy = 0, suu = 0, suy = 0;
			for( int k = 0; k < count; ++k )
			{
				su += k;
				sy += centres[ static_cast< size_t >( k ) ];
				suu += static_cast< double >( k ) * k;
				suy += k * centres[ static_cast< size_t >( k ) ];
			}
			slope = ( count * suy - su * sy ) / ( count * suu - su * su );
		}
		Check( count >= 5 && std::fabs( slope - spacing ) * ( count - 1 ) <= 1.0,
		       fmt( "%4dx%-4d a = %.1f mm at %.0f deg: %d beads %.3f px apart, lambda cos(theta) = %.3f px (%+.2f px over the span; tolerance 1 px)", raster.w,
		            raster.h, in.nozzle * 1e3, n.angle * 180.0 / kPi, count, slope, spacing, ( slope - spacing ) * ( count - 1 ) ) );

		//The column's end, with the beads left out of the picture.
		Rig bare;
		if( !setUp( bare, raster, 1 ) )
			return 1;
		const Picture col = pictureOf( bare );
		const std::vector< double > Q = columnSums( col );
		const double endX = col.px( n.x + V * c * tb );
		const int ref     = static_cast< int >( std::floor( endX ) ) - 4;
		double tail       = 0.0;
		for( int x = ref + 1; x < col.w; ++x )
			tail += Q[ static_cast< size_t >( x ) ];
		//The strip's per-column depth, extrapolated linearly from the two
		//columns before the reference: the equivalent sharp end is where the
		//tail's area runs out.
		const double q0 = Q[ static_cast< size_t >( ref ) ], dq = q0 - Q[ static_cast< size_t >( ref - 1 ) ];
		double length   = q0 > 0.0 ? tail / q0 : 0.0;
		for( int i = 0; i < 3; ++i )
			length = tail / std::max( 1e-12, q0 + dq * ( 0.5 + 0.5 * length ) );
		const double measured = ref + 1 + length;
		Check( q0 > 0.0 && std::fabs( measured - endX ) <= 1.0,
		       fmt( "%4dx%-4d the column ends at %.2f px, the material of age t_b = %.4f s at %.2f px (%+.2f px; tolerance 1 px)", raster.w, raster.h, measured, tb,
		            endX, measured - endX ) );

		//An arc that lands before t_b.
		Rig shortArc;
		if( !setUp( shortArc, raster, 7 ) )
			return 1;
		shortArc.Set( PT_RESPONSE, 0.6f );
		shortArc.Set( PT_PRESSURE, ParamFromPressure( 0.6 ) );
		if( !shortArc.Render( 120 ) )
			return 1;
		const SimInputs& si  = shortArc.plugin.State().Inputs();
		const double Hs      = si.pressure * n.head * ( 1.0 - si.response );
		const double Vs      = std::sqrt( 2.0 * ref::g * Hs );
		const double flight  = 2.0 * Vs * s / ref::g;
		const Picture sp     = pictureOf( shortArc );
		const int xl         = static_cast< int >( std::floor( sp.px( n.x + Vs * Vs * 2.0 * s * c / ref::g ) ) );
		bool reaches         = false;
		for( int x = xl - 4; x <= xl + 4; ++x )
			reaches = reaches || sp.at( x, sp.waterRow ) > 1e-6;
		Check( flight < tb && shortArc.plugin.State().Beads().empty() && reaches,
		       fmt( "%4dx%-4d an arc that lands after %.4f s (< t_b %.4f s): %zu beads, the column reaches the basin intact: %s", raster.w, raster.h, flight, tb,
		            shortArc.plugin.State().Beads().size(), reaches ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// --lead
//===========================================================================
int runLead( const Perturb& perturb )
{
	std::printf( "\n=== lead: at the defaults (valve and pump lag, drag, breakup), host beats at 120 BPM -- a parabola through the column's top\n"
	             "    peaks on the beat, to the frame (the centre shooter solo). Twice: at the default Pressure, camera zoomed to View 5 m,\n"
	             "    where the column is still intact at its apex, and at 12 m, where it pinches off well below it and the prediction has\n"
	             "    to include the beads' rise against drag -- fired once a bar there, so a lead a whole beat out cannot pass for right\n" );
	struct Case
	{
		const char* name;
		double pressure, view;
		FireOn fireOn;
		int period, frames, peaks;///< frames between cues, frames measured, peaks required
	};
	const Case cases[] = { { "default Pressure, on the beat", 6.0, 5.0, FireOn::Beat, 30, 240, 6 },
		                   { "Pressure 12 m, on the bar", 12.0, 10.0, FireOn::Bar, 120, 480, 3 } };
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			Rig rig;
			rig.bpm = 120.0;
			SimHooks base;
			base.solo = 1ull << kShowCentre;
			if( !startRig( rig, raster.w, raster.h, perturb, base ) )
				return 1;
			defaults( rig );
			rig.Set( PT_PRESSURE, ParamFromPressure( c.pressure ) );
			rig.Set( PT_VIEW, ParamFromView( c.view ) );
			rig.Set( PT_FIRE_ON, static_cast< int >( c.fireOn ) );
			rig.Set( PT_LAMPS, static_cast< int >( Lamps::White ) );
			rig.Set( PT_EXPOSURE, ParamFromExposure( 1.0 ) );
			if( perturb.leadZero )
				rig.Set( PT_LEAD, 0.0f );
			if( !rig.Render( 300 ) )
				return 1;
			std::map< int, double > top;
			const int first = rig.frame;
			for( int f = first; f < first + c.frames; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const Picture pic = pictureOf( rig );
				top[ f ]          = stripTop( pic, pic.px( 0.0 ), std::max( 4, static_cast< int >( 0.25 * pic.scale ) ) );
			}
			double worst = 0.0;
			int beats    = 0;
			bool shaped  = true;
			for( int beat = first + c.period - first % c.period; beat + 12 < first + c.frames; beat += c.period )
			{
				std::vector< double > u, y;
				for( int f = beat - 12; f <= beat + 12; ++f )
				{
					u.push_back( f - beat );
					y.push_back( top[ f ] );
				}
				double k[ 3 ];
				if( !fitParabola( u, y, k ) || k[ 2 ] >= 0.0 )
				{
					shaped = false;
					continue;
				}
				worst = std::max( worst, std::fabs( -k[ 1 ] / ( 2.0 * k[ 2 ] ) ) );
				++beats;
			}
			const Sim& sim       = rig.plugin.State();
			const double v       = sim.SpeedFor( kShowCentre, 1.0 - std::exp( -sim.Inputs().shot / sim.Inputs().valveTau ), sim.Head( kShowCentre ) );
			const double apex    = v / ref::g;
			Check( shaped && beats >= c.peaks && worst <= 1.0,
			       fmt( "%4dx%-4d %s: %d cues, the fitted peak at most %.2f frames from the cue (tolerance 1 frame); command-to-peak %.4f s;"
			            " pinch-off at age %.3f s, the drag-free apex at %.3f s",
			            raster.w, raster.h, c.name, beats, worst, sim.CommandToPeak( kShowCentre ), sim.BreakupAgeNow(), apex ) );
		}
	return Verdict();
}

//===========================================================================
// --late
//===========================================================================
int runLate( const Perturb& perturb )
{
	std::printf( "\n=== late: Fire On Onset, the harness's kicks -- the first kick, 0.25 s in, is heard; the water tops out V/g after the kick's\n"
	             "    frame began (a vertex fitted to the rise of the second kick's shot, 4 s on, the pump long settled), and no Lead moves it:\n"
	             "    an onset is never predicted\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo      = 1ull << kRowCentre;
		base.dragScale = 0.0;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_PRESSURE, ParamFromPressure( 15.0 ) );
		rig.Set( PT_VIEW, ParamFromView( 20.0 ) );
		rig.Set( PT_LEAD, 1.0f );
		constexpr int kKick = 15, kEvery = 240;
		rig.beforeFrame     = [ &rig ]( int f ) { feedKicks( rig.plugin, f, kKick, kEvery ); };
		const double first  = simTimeOf( rig, kKick - 1 );
		const double second = simTimeOf( rig, kKick + kEvery - 1 );
		const double H      = PressureFromParam( ParamFromPressure( 15.0 ) );
		const double V      = std::sqrt( 2.0 * ref::g * H );
		const double vertex = second + V / ref::g;
		std::vector< double > u, y;
		while( simTimeOf( rig, rig.frame ) < vertex - 0.04 )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const double t = simTimeOf( rig, rig.frame - 1 );
			if( t < vertex - 0.45 )
				continue;
			const Picture pic = pictureOf( rig );
			u.push_back( ( t - vertex ) * rig.fps );
			y.push_back( stripTop( pic, pic.px( 0.0 ), 6 ) );
		}
		const auto& shots = rig.plugin.State().ShotTimes( kRowCentre );
		const bool heard  = !shots.empty() && std::fabs( shots.front() - first ) < 1e-6;
		const bool twice  = shots.size() == 2 && std::fabs( shots.back() - second ) < 1e-6;
		double k[ 3 ]     = {};
		const bool fitted = fitParabola( u, y, k ) && k[ 2 ] < 0.0;
		const double off  = fitted ? -k[ 1 ] / ( 2.0 * k[ 2 ] ) : 1e9;
		Check( heard, fmt( "%4dx%-4d the kick on frame %d (0.25 s after start) fired a shot at its frame's start: %s", raster.w, raster.h, kKick,
		                   heard ? "yes" : ( shots.empty() ? "NO SHOT" : fmt( "NO, at %.4f s", shots.front() ).c_str() ) ) );
		Check( twice && fitted && std::fabs( off ) <= 1.0,
		       fmt( "%4dx%-4d H %.1f m: %zu shots; the second's rise has its vertex %.2f frames from its kick + V/g = %.4f s (tolerance 1 frame) -- %.0f frames late",
		            raster.w, raster.h, H, shots.size(), off, V / ref::g, V / ref::g * rig.fps ) );
	}
	return Verdict();
}

//===========================================================================
// --louder
//===========================================================================
int runLouder( const Perturb& perturb )
{
	std::printf( "\n=== louder: Response 0.6, drag off -- full-scale bins drive the pump to H, silence to ( 1 - Response ) H, and the\n"
	             "    column's height is the head: v^2 / 2g\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo      = 1ull << kRowCentre;
		base.dragScale = 0.0;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_FIRE_ON, static_cast< int >( FireOn::Steady ) );
		rig.Set( PT_PRESSURE, ParamFromPressure( 6.0 ) );
		rig.Set( PT_RESPONSE, 0.6f );
		rig.beforeFrame = [ &rig ]( int f ) { writeSpectrum( rig.plugin, [ f ]( int ) { return f < 180 ? 1.0 : 0.0; } ); };
		if( !rig.Render( 180 ) )
			return 1;
		const Picture loud = pictureOf( rig );
		if( !rig.Render( 180 ) )
			return 1;
		const Picture quietPic = pictureOf( rig );
		const SimInputs& in    = rig.plugin.State().Inputs();
		const double H         = in.pressure;
		const double low       = ( 1.0 - in.response ) * H;
		const double a = stripTop( loud, loud.px( 0.0 ), 8 ), b = stripTop( quietPic, quietPic.px( 0.0 ), 8 );
		Check( std::fabs( a - loud.py( H ) ) <= 1.0 && std::fabs( b - quietPic.py( low ) ) <= 1.0,
		       fmt( "%4dx%-4d loud: top %.2f px, H %.2f m at %.2f px (%+.2f); silent: top %.2f px, ( 1 - R ) H = %.2f m at %.2f px (%+.2f) (tolerance 1 px)", raster.w,
		            raster.h, a, H, loud.py( H ), a - loud.py( H ), b, low, quietPic.py( low ), b - quietPic.py( low ) ) );
	}
	return Verdict();
}

//===========================================================================
// --lag
//===========================================================================
int runLag( const Perturb& perturb )
{
	std::printf( "\n=== lag: an 80 ms solenoid -- the column's top every frame is the envelope of closed-form parcels launched at\n"
	             "    V s(t), s = 1 - exp( -( t - t0 ) / tau ) while open and decaying after (drag off)\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		SimHooks base;
		base.solo      = 1ull << kRowCentre;
		base.dragScale = 0.0;
		if( !startRig( rig, raster.w, raster.h, perturb, base ) )
			return 1;
		rig.Set( PT_PRESSURE, ParamFromPressure( 4.0 ) );
		rig.Set( PT_VIEW, ParamFromView( 5.0 ) );
		rig.Set( PT_VALVE, ParamFromValve( 0.08 ) );
		rig.Set( PT_SHOT, ParamFromShot( 0.4 ) );
		if( !rig.Render( 30 ) || !rig.Press( PT_FIRE ) )
			return 1;
		const double fired  = simTimeOf( rig, rig.frame - 2 );
		const SimInputs& in = rig.plugin.State().Inputs();
		const double H = in.pressure, tau = in.valveTau, shot = in.shot;
		const double V = std::sqrt( 2.0 * ref::g * H );
		auto opening   = [ & ]( double t ) {
            const double a = t - fired;
            if( a <= 0.0 )
                return 0.0;
            if( a <= shot )
                return 1.0 - std::exp( -a / tau );
            return ( 1.0 - std::exp( -shot / tau ) ) * std::exp( -( a - shot ) / tau );
		};
		auto envelope = [ & ]( double t ) {
			double best = -1.0;
			for( double launch = fired; launch <= t; launch += 1e-4 )
			{
				const double s = opening( launch );
				if( s < Sim::kEmitFloor )
					continue;
				const double age = t - launch;
				best             = std::max( best, V * s * age - 0.5 * ref::g * age * age );
			}
			return best;
		};
		double worst = 0.0;
		int frames   = 0;
		while( simTimeOf( rig, rig.frame ) < fired + shot + 2.0 * V / ref::g )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const double want = envelope( simTimeOf( rig, rig.frame - 1 ) );
			const Picture pic = pictureOf( rig );
			if( pic.py( want ) < pic.waterRow + 3 )
				continue;
			worst = std::max( worst, std::fabs( stripTop( pic, pic.px( 0.0 ), 8 ) - pic.py( want ) ) );
			++frames;
		}
		Check( frames >= 40 && worst <= 1.5, fmt( "%4dx%-4d tau %.0f ms, shot %.0f ms, H %.1f m: %d frames, worst %.3f px from the envelope (tolerance 1.5 px)", raster.w,
		                                          raster.h, tau * 1e3, shot * 1e3, H, frames, worst ) );
	}
	return Verdict();
}

//===========================================================================
// --screen
//===========================================================================
int runScreen( const Perturb& perturb )
{
	std::printf( "\n=== screen: the Over is the clip as light on the water -- out = clip x M in linear light, M read off a white clip;\n"
	             "    M is 0 wherever the source shows no water, and nowhere else\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats white = flatCard( raster.w, raster.h, 1.0f, 1.0f, 1.0f );
		const Floats card  = buildCard( raster.w, raster.h );
		Rig lit( true ), shown( true ), source( false );
		SimHooks base;
		if( !startRig( lit, raster.w, raster.h, perturb, base, &white ) || !startRig( shown, raster.w, raster.h, perturb, base, &card )
		    || !startRig( source, raster.w, raster.h, perturb, base ) )
			return 1;
		for( Rig* r : { &lit, &shown, &source } )
		{
			defaults( *r );
			r->Set( PT_LAMPS, static_cast< int >( Lamps::White ) );
			r->Set( PT_EXPOSURE, ParamFromExposure( 1.0 ) );
			if( !r->Render( 180 ) )
				return 1;
		}
		const Floats M = lit.Output(), O = shown.Output(), S = source.Output();
		const int waterRow = static_cast< int >( std::ceil( source.plugin.OriginY() - 0.5 ) );
		double worst       = 0.0;
		int wet = 0, mismatched = 0, leaks = 0;
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				const size_t i = ( static_cast< size_t >( y ) * raster.w + x ) * 4;
				for( int ch = 0; ch < 3; ++ch )
				{
					const double want = codeOf( linearOf( card[ i + ch ] ) * card[ i + 3 ] * linearOf( M[ i + ch ] ) );
					worst             = std::max( worst, std::fabs( O[ i + ch ] - want ) * 255.0 );
				}
				const bool inOver = M[ i + 1 ] > 0.0f;
				if( inOver )
					++wet;
				if( y >= waterRow && inOver != ( S[ i + 1 ] > 0.0f ) )
					++mismatched;
				if( !inOver && ( O[ i ] != 0.0f || O[ i + 1 ] != 0.0f || O[ i + 2 ] != 0.0f ) )
					++leaks;
			}
		Check( worst <= 2.0 && wet > raster.w * raster.h / 200 && mismatched == 0 && leaks == 0,
		       fmt( "%4dx%-4d worst %.4f codes from clip x M (tolerance 2: two 8-bit roundings in a host); %d wet pixels; %d where M and the source "
		            "disagree on water; %d dry pixels not black",
		            raster.w, raster.h, worst, wet, mismatched, leaks ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the Over's alpha is mix( clip alpha, 1, Mix ) -- opaque at Mix 1, the clip handed through untouched at Mix 0\n" );
	for( const Raster& raster : kRasters )
	{
		Floats card = buildCard( raster.w, raster.h );
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
				card[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + 3 ] = std::round( 255.0f * ( x + 0.5f ) / raster.w ) / 255.0f;
		Rig rig( true );
		if( !startRig( rig, raster.w, raster.h, perturb, SimHooks {}, &card ) )
			return 1;
		defaults( rig );
		double worst[ 2 ] = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		for( int m = 0; m < 2; ++m )
		{
			rig.Set( PT_MIX, mixes[ m ] );
			if( !rig.Render( m == 0 ? 120 : 1 ) )
				return 1;
			const Floats out = rig.Output();
			for( size_t i = 3; i < out.size(); i += 4 )
				worst[ m ] = std::max( worst[ m ], std::fabs( out[ i ] - ( card[ i ] + ( 1.0f - card[ i ] ) * mixes[ m ] ) ) * 255.0 );
		}
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats through = rig.Output();
		size_t differ        = 0;
		for( size_t i = 0; i < through.size(); ++i )
			differ += through[ i ] != card[ i ];
		Check( worst[ 0 ] <= 1.0 && worst[ 1 ] <= 1.0 && differ == 0,
		       fmt( "%4dx%-4d alpha at Mix 1: worst %.4f codes from 1; at Mix 0.5: %.4f codes from mix( a, 1, 0.5 ) (tolerance 1 code); Mix 0: %zu of %zu values "
		            "differ from the clip (bit-exact)",
		            raster.w, raster.h, worst[ 0 ], worst[ 1 ], differ, through.size() ) );
	}
	return Verdict();
}

//===========================================================================
// --wind
//===========================================================================
int runWind( const Perturb& perturb )
{
	std::printf( "\n=== wind: a puff from the mist bar drifts with the wind -- its tau-weighted centroid moves at the wind's speed\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, SimHooks {} ) )
			return 1;
		rig.Set( PT_LAYOUT, static_cast< int >( LayoutId::Screen ) );
		rig.Set( PT_PRESSURE, ParamFromPressure( 20.0 ) );
		rig.Set( PT_WIND, ParamFromWind( 2.0 ) );
		rig.Set( PT_VIEW, ParamFromView( 40.0 ) );
		//Three seconds of screen, so that the plume is up and the spray is in
		//it, then none: the puff is everything aloft when it stops.
		rig.beforeFrame = [ &rig ]( int f ) { rig.Set( PT_SCREEN, f < 180 ? 1.0f : 0.0f ); };
		double centroid[ 2 ] = {};
		size_t parcels[ 2 ]  = {};
		const int at[ 2 ]    = { 240, 300 };
		double scale         = 1.0;
		for( int k = 0; k < 2; ++k )
		{
			if( !rig.Render( at[ k ] - rig.frame + 1 ) )
				return 1;
			const Picture pic = pictureOf( rig );
			double sum = 0.0, moment = 0.0;
			for( int y = pic.waterRow; y < pic.h; ++y )
				for( int x = 0; x < pic.w; ++x )
				{
					sum += pic.at( x, y );
					moment += ( x + 0.5 ) * pic.at( x, y );
				}
			centroid[ k ] = sum > 0.0 ? moment / sum : 0.0;
			parcels[ k ]  = rig.plugin.State().SprayParcels().size();
			scale         = pic.scale;
		}
		const double wind  = rig.plugin.State().Inputs().wind;
		const double want  = wind * ( simTimeOf( rig, at[ 1 ] ) - simTimeOf( rig, at[ 0 ] ) ) * scale;
		const double moved = centroid[ 1 ] - centroid[ 0 ];
		//A few stragglers from the bar's downwind end leave the plume low and
		//land; more than one in a hundred would make the centroid a different
		//population's.
		Check( parcels[ 0 ] > 100 && parcels[ 1 ] <= parcels[ 0 ] && parcels[ 0 ] - parcels[ 1 ] <= parcels[ 0 ] / 100 && std::fabs( moved - want ) <= 2.0,
		       fmt( "%4dx%-4d %zu parcels (%zu a second later): the centroid moved %.2f px in 1 s, the wind %.1f m/s says %.2f px (%+.2f; tolerance 2 px)", raster.w,
		            raster.h, parcels[ 0 ], parcels[ 1 ], moved, wind, want, moved - want ) );
	}
	return Verdict();
}

//===========================================================================
// --state
//===========================================================================
int runState( const Perturb& perturb )
{
	std::printf( "\n=== state: the fountain lives in metres -- resized mid-run, the last frame equals a run that was always at that size\n" );
	const Raster pairs[ 2 ][ 2 ] = { { { 1280, 720 }, { 320, 180 } }, { { 320, 180 }, { 1280, 720 } } };
	for( const auto& pair : pairs )
	{
		Rig moved, always;
		if( !startRig( moved, pair[ 0 ].w, pair[ 0 ].h, perturb, SimHooks {} ) || !startRig( always, pair[ 1 ].w, pair[ 1 ].h, perturb, SimHooks {} ) )
			return 1;
		for( Rig* r : { &moved, &always } )
		{
			defaults( *r );
			r->bpm = 120.0;
		}
		if( !moved.Render( 90 ) || !moved.Resize( pair[ 1 ].w, pair[ 1 ].h ) || !moved.Render( 90 ) || !always.Render( 180 ) )
			return 1;
		const Floats a = moved.Output(), b = always.Output();
		size_t differ  = 0;
		for( size_t i = 0; i < a.size(); ++i )
			differ += a[ i ] != b[ i ];
		Check( differ == 0, fmt( "%dx%d -> %dx%d at frame 90: %zu of %zu values differ from a run always at %dx%d (bit-exact, same context)", pair[ 0 ].w, pair[ 0 ].h,
		                         pair[ 1 ].w, pair[ 1 ].h, differ, a.size(), pair[ 1 ].w, pair[ 1 ].h ) );
	}
	return Verdict();
}

//===========================================================================
// --clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: a host clock at 4.99e8 ms (Resolume's has been seen there) renders the frames a clock from 0 s does,\n"
	             "    and a clock left to settle its own unit settles on the right one\n" );
	for( const Raster& raster : kRasters )
	{
		Rig zero, late;
		if( !startRig( zero, raster.w, raster.h, perturb, SimHooks {} ) || !startRig( late, raster.w, raster.h, perturb, SimHooks {} ) )
			return 1;
		late.plugin.SetClockScaleForTest( 0.001 );
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		size_t differ = 0, total = 0;
		for( Rig* r : { &zero, &late } )
		{
			defaults( *r );
			r->bpm = 120.0;
		}
		for( int k = 0; k < 3; ++k )
		{
			if( !zero.Render( 60 ) || !late.Render( 60 ) )
				return 1;
			const Floats a = zero.Output(), b = late.Output();
			for( size_t i = 0; i < a.size(); ++i )
				differ += a[ i ] != b[ i ];
			total += a.size();
		}
		Check( differ == 0, fmt( "%4dx%-4d frames 60, 120, 180: %zu of %zu values differ (bit-exact, same context)", raster.w, raster.h, differ, total ) );

		//The unit, unforced: this harness renders far faster than real time,
		//as an offline host or oxbow's selftest does, so the wall-clock vote
		//cannot settle it and the step's size has to.
		for( const double unit : { 1.0, 1000.0 } )
		{
			Rig r;
			if( !startRig( r, raster.w, raster.h, perturb, SimHooks {} ) )
				return 1;
			r.plugin.SetClockScaleForTest( 0.0 );
			r.timeOffset = unit == 1.0 ? 12.5 : 4.99e8;
			r.timeScale  = unit;
			if( !r.Render( 60 ) )
				return 1;
			const double want = 1.0 / unit;
			const double got  = r.plugin.ClockScale();
			const double host = ( r.timeOffset + r.timeScale * r.Seconds( r.frame - 1 ) ) * want;
			Check( got == want && std::fabs( r.plugin.HostNow() - host ) < 1e-6,
			       fmt( "%4dx%-4d a host clock in %s at 60 fps, rendered faster than real time: settled on %s by frame 60, and reads %.6f s (host %.6f s)",
			            raster.w, raster.h, unit == 1.0 ? "seconds" : "milliseconds", got == 1.0 ? "seconds" : ( got == 0.001 ? "milliseconds" : "nothing" ),
			            r.plugin.HostNow(), host ) );
		}
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "constants", runConstants, true },
		{ "apex", runApex, false },
		{ "range", runRange, false },
		{ "flight", runFlight, false },
		{ "drag", runDrag, false },
		{ "rk4", runRK4, false },
		{ "breakup", runBreakup, false },
		{ "lead", runLead, false },
		{ "late", runLate, false },
		{ "louder", runLouder, false },
		{ "lag", runLag, false },
		{ "screen", runScreen, false },
		{ "alpha", runAlpha, false },
		{ "wind", runWind, false },
		{ "state", runState, false },
		{ "clock", runClock, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	auto sim = []( std::function< void( SimHooks& ) > f ) { return [ f ]( Perturb& p ) { p.sim = f; }; };
	add( "constants", runConstants, "the running Sim on the long-wave wavelength", sim( []( SimHooks& h ) { h.wavelength = 2; } ) );
	add( "constants", runConstants, "Rayleigh's table recomputed from the long-wave formula", []( Perturb& p ) { p.longWave = true; } );
	add( "apex", runApex, "exit speed sqrt( g H )", sim( []( SimHooks& h ) { h.speedLaw = 1; } ) );
	add( "range", runRange, "every jet's launch angle 3 degrees off", sim( []( SimHooks& h ) { h.angleOffset += 3.0 * kPi / 180.0; } ) );
	add( "flight", runFlight, "gravity 3 % weak", sim( []( SimHooks& h ) { h.gravityScale = 0.97; } ) );
	add( "drag", runDrag, "no drag: K = 0", sim( []( SimHooks& h ) { h.dragScale = 0.0; } ) );
	add( "rk4", runRK4, "the beads stepped by forward Euler", sim( []( SimHooks& h ) { h.eulerBeads = true; } ) );
	add( "breakup", runBreakup, "Plateau's wavelength 2 pi a for Rayleigh's", sim( []( SimHooks& h ) { h.wavelength = 1; } ) );
	add( "lead", runLead, "the predictor forgets the valve's lag", sim( []( SimHooks& h ) { h.predictorIgnoresValve = true; } ) );
	add( "lead", runLead, "Lead 0: fired on the beat", []( Perturb& p ) { p.leadZero = true; } );
	add( "late", runLate, "gravity 3 % weak", sim( []( SimHooks& h ) { h.gravityScale = 0.97; } ) );
	add( "late", runLate, "the onset detector never primed", []( Perturb& p ) { p.unprimed = true; } );
	add( "louder", runLouder, "speed in proportion to the head", sim( []( SimHooks& h ) { h.speedLaw = 2; } ) );
	add( "lag", runLag, "the valve's lag held a frame, then one Euler step", sim( []( SimHooks& h ) { h.valveHeldPerFrame = true; } ) );
	add( "screen", runScreen, "the water added to the clip", []( Perturb& p ) { p.additive = true; } );
	add( "wind", runWind, "the mist deaf to the wind", sim( []( SimHooks& h ) { h.mistIgnoresWind = true; } ) );
	add( "state", runState, "a new raster empties the fountain", []( Perturb& p ) { p.resizeClears = true; } );
	add( "clock", runClock, "the host's clock read in single precision", []( Perturb& p ) { p.floatClock = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
	};
	const Case cases[] = {
		{ "source, defaults at 120 BPM", false, []( Rig& ) {} },
		{ "Over, defaults at 120 BPM", true, []( Rig& ) {} },
		{ "source, Screen layout, Screen 1, Gust 1", false, []( Rig& r ) {
			 r.Set( PT_LAYOUT, static_cast< int >( LayoutId::Screen ) );
			 r.Set( PT_SCREEN, 1.0f );
			 r.Set( PT_GUST, 1.0f );
		 } },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 240 frames of warm-up, median and worst of 120; 4K once\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			//One 4K run only: the machine is shared.
			if( size.w == 3840 && &c != &cases[ 0 ] )
				continue;
			Rig rig( c.over );
			rig.bpm = 120.0;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			if( !rig.Render( 240 ) )
				return 1;
			constexpr int kTimed = 120;
			std::vector< double > times, cpu;
			size_t primitives = 0;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
				const Sim& sim = rig.plugin.State();
				primitives     = std::max( primitives, sim.Beads().size() + sim.SprayParcels().size() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-40s %-6s GPU median %6.2f ms, worst %6.2f  (%4.1f%% of a 60 fps frame); wall %5.2f ms; up to %zu beads + spray parcels\n", c.what,
			             size.name, times[ kTimed / 2 ], times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ], primitives );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
