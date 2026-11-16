#include "Water.h"

#include <algorithm>
#include <cmath>

namespace fountain::water
{

double BesselI( int n, double x )
{
	double term = 1.0;
	for( int i = 1; i <= n; ++i )
		term *= ( x / 2.0 ) / i;
	double total = term;
	for( int k = 1; k < 200; ++k )
	{
		term *= ( x / 2.0 ) * ( x / 2.0 ) / ( static_cast< double >( k ) * ( k + n ) );
		total += term;
		if( term < 1e-18 * total )
			break;
	}
	return total;
}

double RayleighGrowth( double x )
{
	const double v = ( 1.0 - x * x ) * x * BesselI( 1, x ) / BesselI( 0, x );
	return v > 0.0 ? std::sqrt( v ) : 0.0;
}

double RayleighArgmax()
{
	//Bisection on the analytic slope. With r = I1/I0, r' = 1 - r/x - r^2, so
	//d/dx [ ( x - x^3 ) r ] = ( 1 - 3 x^2 ) r + ( x - x^3 ) r'. Golden section
	//on the function itself stalls near sqrt( eps ) relative: the maximum is
	//flat, and its neighbours round to the same value.
	auto slope = []( double x ) {
		const double r = BesselI( 1, x ) / BesselI( 0, x );
		return ( 1.0 - 3.0 * x * x ) * r + ( x - x * x * x ) * ( 1.0 - r / x - r * r );
	};
	double lo = 0.3, hi = 0.95;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( slope( mid ) > 0.0 ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}

double BreakupRate( double a )
{
	return kRayleighRate * std::sqrt( kSurfaceTension / ( kDensity * a * a * a ) );
}

double BreakupAge( double a, double lnDisturbance )
{
	return std::max( 0.0, lnDisturbance ) / BreakupRate( a );
}

double TerminalVelocity( double diameterMetres )
{
	const double d = std::clamp( diameterMetres * 1e3, kFitLowMm, kFitHighMm );
	return 9.65 - 10.3 * std::exp( -0.6 * d );
}

double DragConstant( double diameterMetres )
{
	const double vt = TerminalVelocity( diameterMetres );
	return kGravity / ( vt * vt );
}

} // namespace fountain::water
