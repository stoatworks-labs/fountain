#!/usr/bin/env python3
"""Every physical number fountain uses, computed from its source.

Nothing here is typed in from memory. Each block names the published formula or
figure it starts from (checked against the source; see ATTRIBUTIONS.md), computes
what source/Water.h embeds, and prints both so that a reader can diff them. The
harness re-derives the Rayleigh numbers in C++ under `fttest --offline`; this
script is the second, independent derivation, in a different language with a
different Bessel routine (power series here, scipy-free on purpose).

Run: python3 tools/constants.py
"""
import math

# ---------------------------------------------------------------------------
# Standard gravity. 3rd CGPM (1901), Declaration 2, item 3: "980.665 cm/s2".
G_N = 980.665e-2  # m/s^2

# ---------------------------------------------------------------------------
# Water at 20 degC.
T_C = 20.0
T_K = T_C + 273.15

# Surface tension against air: IAPWS R1-76(2014), Revised Release on the
# Surface Tension of Ordinary Water Substance.
#   sigma = B tau^mu (1 + b tau),  tau = 1 - T/Tc
#   Tc = 647.096 K, B = 235.8 mN/m, b = -0.625, mu = 1.256
TC = 647.096
tau = 1.0 - T_K / TC
SIGMA = 235.8e-3 * tau ** 1.256 * (1.0 - 0.625 * tau)  # N/m
# The release's own table gives 72.74 mN/m at 20 degC (uncertainty 0.5 %).

# Density: Tanaka, Girard, Davis, Peuto, Bignell (2001), "Recommended table
# for the density of water between 0 C and 40 C based on recent experimental
# reports", Metrologia 38(4) 301-309, equation (1) with Table 1 constants:
#   rho = a5 [1 - (t + a1)^2 (t + a2) / (a3 (t + a4))]
A1, A2, A3, A4, A5 = -3.983035, 301.797, 522528.9, 69.34881, 999.974950
RHO = A5 * (1.0 - (T_C + A1) ** 2 * (T_C + A2) / (A3 * (T_C + A4)))  # kg/m^3

# ---------------------------------------------------------------------------
# Rayleigh (1878), "On the instability of jets", Proc. London Math. Soc.
# s1-10(1) 4-13, doi:10.1112/plms/s1-10.1.4. Inviscid capillary column of
# radius a, axisymmetric disturbance exp(qt) cos(kz), x = ka:
#   q^2 = (T / (rho a^3)) * (1 - x^2) * x I1(x) / I0(x)       (his (12)-(13))
# He finds x^2 = .4858 (17) and lambda = 4.508 x 2a (18), and tabulates
# sqrt of (13) (the table under (18)). We reproduce the table to his 4
# figures and find the maximum to 1e-12.


def bessel_i(n, x):
    """Modified Bessel function I_n(x) by its power series (x < 2 here)."""
    term = (x / 2.0) ** n / math.factorial(n)
    total = term
    k = 0
    while True:
        k += 1
        term *= (x / 2.0) ** 2 / (k * (k + n))
        total += term
        if term < 1e-18 * total:
            return total


def growth(x):
    """sqrt((1 - x^2) x I1(x)/I0(x)): q in units of sqrt(T/(rho a^3))."""
    v = (1.0 - x * x) * x * bessel_i(1, x) / bessel_i(0, x)
    return math.sqrt(v) if v > 0 else 0.0


def argmax_growth():
    """Bisection on the analytic slope of q^2. With r = I1/I0,
    r' = 1 - r/x - r^2, so d/dx[(x - x^3) r] = (1 - 3x^2) r + (x - x^3) r'.
    (Golden section on the function stalls near sqrt(eps) relative: the
    maximum is flat and its neighbours round to the same value.)"""
    def slope(x):
        r = bessel_i(1, x) / bessel_i(0, x)
        return (1.0 - 3.0 * x * x) * r + (x - x ** 3) * (1.0 - r / x - r * r)
    lo, hi = 0.3, 0.95
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if slope(mid) > 0.0:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


X_MAX = argmax_growth()
Q_MAX = growth(X_MAX)  # dimensionless fastest growth rate
LAMBDA_OVER_A = 2.0 * math.pi / X_MAX
# One wavelength of column pinches into one main drop: pi a^2 lambda = 4/3 pi r^3
DROP_OVER_A = (0.75 * LAMBDA_OVER_A) ** (1.0 / 3.0)
# The long-wave approximation (drop the Bessel ratio's higher terms):
# x I1/I0 ~ x^2/2 gives q^2 ~ (1 - x^2) x^2 / 2, max at x^2 = 1/2.
LONGWAVE_LAMBDA_OVER_A = 2.0 * math.pi / math.sqrt(0.5)
# Plateau's limit: every disturbance longer than the circumference grows.
PLATEAU_LAMBDA_OVER_A = 2.0 * math.pi

RAYLEIGH_TABLE = [  # (x^2, sqrt of (13)) as printed under his (18)
    (0.05, 0.1536), (0.1, 0.2108), (0.2, 0.2794), (0.3, 0.3182),
    (0.4, 0.3382), (0.5, 0.3432), (0.6, 0.3344), (0.8, 0.2701),
    (0.9, 0.2015),
]

# ---------------------------------------------------------------------------
# Raindrop terminal velocity. Atlas, Srivastava and Sekhon (1973), "Doppler
# radar characteristics of precipitation at vertical incidence", Rev.
# Geophys. 11(1) 1-35: their empirical fit to the laboratory fall speeds of
# Gunn and Kinzer (1949), "The terminal velocity of fall for water droplets
# in stagnant air", J. Meteor. 6, 243 (sea level):
#   v_t = 9.65 - 10.3 exp(-0.6 D)   [m/s, D in mm]
# Checked through two reproductions, not the 1973 paper itself: DISDRODB's
# fall_velocity module (v0.2.0) and arXiv:1707.05225, which quotes it in
# cm/s and gives its range of use as 0.5-5.0 mm.


def terminal_velocity(d_mm):
    return 9.65 - 10.3 * math.exp(-0.6 * d_mm)


# ---------------------------------------------------------------------------
# Mist optical depth. For drops much larger than the wavelength the
# extinction efficiency tends to 2 (van de Hulst's extinction paradox), so a
# column holding water volume V per area A, in drops of radius r, has
# tau = N pi r^2 Q / A = (V / (4/3 pi r^3)) pi r^2 2 / A = 1.5 (V/A) / r.
Q_EXT = 2.0
TAU_PER_VA_OVER_R = 0.75 * Q_EXT  # tau = this * (V/A) / r


def capillary_rate(a):
    """sqrt(sigma / (rho a^3)) for a column of radius a metres."""
    return math.sqrt(SIGMA / (RHO * a ** 3))


def main():
    print("standard gravity          g   = %.5f m/s^2" % G_N)
    print("surface tension (20 C)    sigma = %.6f N/m  (IAPWS table: 0.07274)" % SIGMA)
    print("density (20 C)            rho = %.4f kg/m^3  (Tanaka Table: 998.2067)" % RHO)
    print()
    print("Rayleigh fastest mode     x   = ka = %.16f" % X_MAX)
    print("                          x^2 = %.6f   (Rayleigh (17): .4858)" % (X_MAX ** 2))
    print("                          q   = %.17f sqrt(sigma/(rho a^3))" % Q_MAX)
    print("                          lambda/a = %.15f   (Rayleigh (18): 4.508 x 2 = 9.016)" % LAMBDA_OVER_A)
    print("                          drop r/a = %.16f" % DROP_OVER_A)
    print("long-wave approximation   lambda/a = %.6f" % LONGWAVE_LAMBDA_OVER_A)
    print("Plateau cut-off           lambda/a = %.6f" % PLATEAU_LAMBDA_OVER_A)
    print()
    print("Rayleigh's table, recomputed:")
    worst = 0.0
    for x2, his in RAYLEIGH_TABLE:
        ours = growth(math.sqrt(x2))
        worst = max(worst, abs(ours - his))
        print("  x^2 = %-4g  his %.4f  ours %.6f  diff %+.6f" % (x2, his, ours, ours - his))
    print("  worst difference %.6f (his figures are rounded to 1e-4)" % worst)
    assert worst <= 0.5e-4 + 1e-12, "Rayleigh's table does not reproduce"
    print()
    print("Breakup at the default nozzle and disturbance:")
    for a_mm in (1.0, 2.0, 4.0, 8.0, 12.0):
        a = a_mm * 1e-3
        q = Q_MAX * capillary_rate(a)
        d_mm = 2.0 * DROP_OVER_A * a_mm
        vt = terminal_velocity(d_mm)
        print("  a = %4.1f mm  q = %7.3f 1/s  t_b(ln 12) = %.3f s  drop D = %5.2f mm  v_t = %.3f m/s  K = %.4f 1/m"
              % (a_mm, q, 12.0 / q, d_mm, vt, G_N / vt ** 2))
    print()
    print("Gunn and Kinzer points through the Atlas fit:")
    for d in (1.0, 2.0, 3.0, 4.0, 5.0):
        print("  D = %.1f mm  v_t = %.3f m/s" % (d, terminal_velocity(d)))
    print()
    print("mist tau = %.2f (V/A)/r" % TAU_PER_VA_OVER_R)


if __name__ == "__main__":
    main()
