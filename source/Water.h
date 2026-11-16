#pragma once

/**
    Water, air and Rayleigh's jet: every physical number fountain uses, with
    where it comes from. Plain C++ in double, no GL.

    Nothing here is typed in from memory. Each constant is either quoted from a
    source that was read (ATTRIBUTIONS.md has the full entries and how each was
    checked) or computed from one, in which case `tools/constants.py` computes
    it independently and `fttest --offline` recomputes it again in C++ from the
    published formula and compares. What is CHOSEN rather than measured says so
    here and in AGENTS.md.
*/
namespace fountain::water
{

constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Published constants.
//---------------------------------------------------------------------------

/// Standard gravity, m/s^2. 3rd CGPM (1901), Declaration 2: 980.665 cm/s^2.
constexpr double kGravity = 9.80665;

/// Water's surface tension against air at 20 degC, N/m: IAPWS R1-76(2014),
/// sigma = B tau^mu (1 + b tau), tau = 1 - T/Tc, Tc 647.096 K, B 235.8 mN/m,
/// b -0.625, mu 1.256, at T = 293.15 K. The release tabulates 72.74 mN/m.
constexpr double kSurfaceTension = 0.07273614042160757;

/// Water's density at 20 degC, kg/m^3: Tanaka et al. (2001), Metrologia
/// 38(4) 301-309, eq. (1).
constexpr double kDensity = 998.2067455596167;

//---------------------------------------------------------------------------
// Rayleigh (1878), "On the instability of jets". Computed, not quoted: the
// maximum over x = ka of (1 - x^2) x I1(x)/I0(x). Rayleigh's own rounded
// figures are x^2 = .4858 (his 17) and lambda = 4.508 x 2a (his 18).
//---------------------------------------------------------------------------

/// The fastest-growing x = ka.
constexpr double kRayleighX = 0.6970188983063286;
/// Its growth rate in units of sqrt( sigma / ( rho a^3 ) ).
constexpr double kRayleighRate = 0.34333870186129922;
/// Its wavelength over the jet's radius, 2 pi / x.
constexpr double kRayleighLambda = 9.014368652624720;
/// One wavelength of column gathered into one sphere: (3/4 lambda/a)^(1/3).
constexpr double kDropRadius = 1.8908867827492628;
/// The long-wave approximation's wavelength (x^2 = 1/2), for the negative control.
constexpr double kLongWaveLambda = 8.885765876316732;
/// Plateau's limit: everything longer than the circumference grows.
constexpr double kPlateauLambda = 2.0 * kPi;

/// The Rayleigh growth function sqrt( (1 - x^2) x I1(x)/I0(x) ), by power
/// series. For `--offline`, which re-derives the constants above from it.
double RayleighGrowth( double x );
/// Its maximum, by golden section on [ 0.3, 0.95 ].
double RayleighArgmax();
/// The modified Bessel function I_n(x) by its power series, small x.
double BesselI( int n, double x );

/// The fastest growth rate, 1/s, for a column of radius a metres.
double BreakupRate( double a );
/// Seconds from leaving the nozzle to pinching off: ln( a / delta0 ) / rate.
double BreakupAge( double a, double lnDisturbance );

//---------------------------------------------------------------------------
// Drops in air.
//---------------------------------------------------------------------------

/// Atlas, Srivastava and Sekhon (1973)'s fit to Gunn and Kinzer (1949):
/// v_t = 9.65 - 10.3 exp( -0.6 D ), m/s, D in mm. Its range of use is
/// 0.5-5.0 mm and D is clamped to it (a Rayleigh bead from a nozzle over
/// 1.33 mm is bigger than any raindrop; see AGENTS.md).
double TerminalVelocity( double diameterMetres );
constexpr double kFitLowMm = 0.5, kFitHighMm = 5.0;

/// Quadratic drag's constant for a drop of that diameter, 1/m: g / v_t^2,
/// so the drop falls at exactly v_t in still air.
double DragConstant( double diameterMetres );

//---------------------------------------------------------------------------
// The mist screen.
//---------------------------------------------------------------------------

/// Extinction efficiency of a drop much larger than the wavelength (van de
/// Hulst's extinction paradox): 2. So water volume V per area A, in drops of
/// radius r, has optical depth tau = 1.5 ( V / A ) / r.
constexpr double kExtinction = 2.0;
constexpr double kTauPerVolumeOverRadius = 0.75 * kExtinction;

/// CHOSEN: the spray's drop radius, m. Only the optical depth uses it.
constexpr double kMistDropRadius = 50e-6;
/// CHOSEN: the spray's settling speed in still air, m/s; its relaxation time
/// to the air is v_s / g.
constexpr double kMistSettle = 0.30;
/// CHOSEN: the spray parcel's spread, m, and how it grows (m^2/s).
constexpr double kMistSigma0 = 0.20;
constexpr double kMistDiffusion = 0.02;
/// CHOSEN: evaporation e-folding, s.
constexpr double kMistEvaporate = 4.0;

/// CHOSEN: optical depth of a jet or bead per unit of its pixel coverage (an
/// aerated jet's opacity across its diameter): coverage 1 is 86 % opaque.
constexpr double kJetOpacity = 2.0;

} // namespace fountain::water
