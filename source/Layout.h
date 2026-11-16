#pragma once

#include <vector>

/**
    Where the nozzles stand. A layout is a list of nozzles along the basin's
    front edge, in metres from its centre, each with a role, a direction and a
    share of the Pressure control.

    - **Shooters** point straight up. Their valves open on Fire On; their pump
      follows the Low band.
    - **Arcs** lean. Their valves stay open; their pump follows the Mid band,
      so their reach (2 H sin 2 theta, drag-free) breathes with the mids.
    - **Mist** emitters stand in a bar and spray the screen; their pump follows
      the High band.

    The basin is seen face-on, so a nozzle is a point on the waterline (y = 0)
    and a direction in the picture's plane.
*/
namespace fountain
{

enum class Role
{
	Shooter = 0,
	Arc,
	Mist
};

enum class LayoutId
{
	Show = 0,///< shooters in a crown, two pairs of crossing arcs, the mist bar behind
	Row,     ///< eleven equal shooters
	Arcs,    ///< crossing arcs, no shooters
	Fan,     ///< nine jets fanned from one point, the centre one a shooter
	Screen,  ///< the mist bar alone, with a shooter at each end
	Count
};

struct Nozzle
{
	Role role   = Role::Shooter;
	double x    = 0.0;///< m from the basin's centre
	double angle = 0.0;///< radians from +x; pi/2 is straight up
	double head = 1.0;///< share of Pressure
};

/// The nozzles of a layout, in drawing order (lamps are assigned by index).
const std::vector< Nozzle >& NozzlesOf( LayoutId layout );

/// The most nozzles any layout has (the lamp table's size).
constexpr int kMaxNozzles = 40;

} // namespace fountain
