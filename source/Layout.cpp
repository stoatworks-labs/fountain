#include "Layout.h"

#include "Water.h"

#include <array>

namespace fountain
{
namespace
{
constexpr double kDeg = water::kPi / 180.0;
constexpr double kUp  = 90.0 * kDeg;

Nozzle shooter( double x, double head )
{
	return { Role::Shooter, x, kUp, head };
}
Nozzle arc( double x, double degrees, double head )
{
	return { Role::Arc, x, degrees * kDeg, head };
}
Nozzle mist( double x )
{
	return { Role::Mist, x, kUp, 1.0 };
}

void mistBar( std::vector< Nozzle >& out, double from, double to, int count )
{
	for( int i = 0; i < count; ++i )
		out.push_back( mist( from + ( to - from ) * i / ( count - 1 ) ) );
}

std::vector< Nozzle > build( LayoutId id )
{
	std::vector< Nozzle > n;
	switch( id )
	{
	case LayoutId::Show:
	{
		const double xs[]    = { -6.0, -4.0, -2.0, 0.0, 2.0, 4.0, 6.0 };
		const double heads[] = { 0.55, 0.7, 0.85, 1.0, 0.85, 0.7, 0.55 };
		for( int i = 0; i < 7; ++i )
			n.push_back( shooter( xs[ i ], heads[ i ] ) );
		n.push_back( arc( -7.5, 62.0, 0.45 ) );
		n.push_back( arc( -5.0, 68.0, 0.45 ) );
		n.push_back( arc( 5.0, 112.0, 0.45 ) );
		n.push_back( arc( 7.5, 118.0, 0.45 ) );
		mistBar( n, -7.5, 7.5, 16 );
		break;
	}
	case LayoutId::Row:
		for( int i = 0; i < 11; ++i )
			n.push_back( shooter( -7.5 + 1.5 * i, 1.0 ) );
		break;
	case LayoutId::Arcs:
		n.push_back( arc( -7.5, 58.0, 0.6 ) );
		n.push_back( arc( -5.0, 62.0, 0.6 ) );
		n.push_back( arc( -2.5, 66.0, 0.6 ) );
		n.push_back( arc( -1.0, 76.0, 0.75 ) );
		n.push_back( arc( 1.0, 104.0, 0.75 ) );
		n.push_back( arc( 2.5, 114.0, 0.6 ) );
		n.push_back( arc( 5.0, 118.0, 0.6 ) );
		n.push_back( arc( 7.5, 122.0, 0.6 ) );
		break;
	case LayoutId::Fan:
		for( int i = 0; i < 9; ++i )
		{
			const double degrees = 50.0 + 10.0 * i;
			if( i == 4 )
				n.push_back( shooter( 0.0, 1.0 ) );
			else
				n.push_back( arc( 0.0, degrees, i == 0 || i == 8 ? 0.7 : 0.85 ) );
		}
		break;
	case LayoutId::Screen:
		n.push_back( shooter( -9.0, 0.8 ) );
		mistBar( n, -8.0, 8.0, 20 );
		n.push_back( shooter( 9.0, 0.8 ) );
		break;
	default: break;
	}
	return n;
}
} // namespace

const std::vector< Nozzle >& NozzlesOf( LayoutId layout )
{
	static const std::array< std::vector< Nozzle >, static_cast< size_t >( LayoutId::Count ) > table = [] {
		std::array< std::vector< Nozzle >, static_cast< size_t >( LayoutId::Count ) > t;
		for( int i = 0; i < static_cast< int >( LayoutId::Count ); ++i )
			t[ static_cast< size_t >( i ) ] = build( static_cast< LayoutId >( i ) );
		return t;
	}();
	const int i = static_cast< int >( layout );
	return table[ static_cast< size_t >( i >= 0 && i < static_cast< int >( LayoutId::Count ) ? i : 0 ) ];
}

} // namespace fountain
