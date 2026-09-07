// KERR GEODESIC ACCEPTANCE TESTS. DOUBLE PRECISION AGAINST ANALYTIC TRUTH.

#define KERR_USE_DOUBLE
#include "physics/kerr.h"
#include "physics/disk.h"

#include "physics/tonemap.h"

#include "physics/freefall.hpp"
#include "physics/luts.hpp"

#include "tools/validate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bhs {

namespace {

constexpr double kPi = 3.14159265358979323846;

int gChecks = 0;
int gFailures = 0;

void check(const char* name, double measured, double expected, double tolerance,
           const char* units) {
    ++gChecks;
    const double error = std::fabs(measured - expected);
    const bool ok = error <= tolerance;
    if (!ok) {
        ++gFailures;
    }
    std::printf("  [%s] %-42s %12.7f vs %12.7f  (err %.2e %s)\n",
                ok ? "pass" : "FAIL", name, measured, expected, error, units);
}

void checkTrue(const char* name, bool condition, const char* detail) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
    }
    std::printf("  [%s] %-42s %s\n", condition ? "pass" : "FAIL", name, detail);
}

// FIRE EQUATORIAL RAY INWARD. TESTS INTEGRATOR AND POTENTIALS ISOLATED.
int traceEquatorial(double a, double b, double r0, double escapeRadius) {
    KerrBH bh; bh.a = a; bh.M = 1.0;
    KerrRay ray; ray.b = b; ray.Q = 0.0;

    const double delta = kerr_delta(r0, bh);
    const double R = kerr_R(r0, bh, ray);
    if (R <= 0.0) {
        return KERR_CAPTURED;   // UNPHYSICAL START POINT.
    }

    KerrState s;
    s.r = r0; s.theta = kPi / 2; s.phi = 0; s.t = 0;
    s.pr = -std::sqrt(R) / delta;   // INWARD.
    s.pth = 0.0;

    return kerr_trace(s, bh, ray, escapeRadius, 1e-10, 200000).outcome;
}

// BISECT IMPACT PARAMETER TO FIND SHADOW BOUNDARY. STALLED RAYS FAIL.
int gExhausted = 0;

double criticalImpactParameter(double a, double lo, double hi, double r0) {
    const double escapeRadius = r0 * 2.0;
    const int loOutcome = traceEquatorial(a, lo, r0, escapeRadius);

    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        const int outcome = traceEquatorial(a, mid, r0, escapeRadius);
        if (outcome == KERR_EXHAUSTED) {
            ++gExhausted;
        }
        if (outcome == loOutcome) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// CRITICAL IMPACT PARAMETER. BARDEEN-PRESS-TEUKOLSKY 1972 EQ 2.13.
double photonOrbitImpactParameter(double a, double r) {
    if (std::fabs(a) < 1e-12) {
        return 3.0 * std::sqrt(3.0);   // SCHWARZSCHILD LIMIT.
    }
    return -(r * r * r - 3.0 * r * r + a * a * r + a * a) / (a * (r - 1.0));
}

// TRACE THROUGH CAMERA TETRAD. PSI IS ANGLE OFF RADIAL IN EQUATOR.
int traceFromCamera(double a, double rCam, double psi, double escapeRadius) {
    KerrBH bh; bh.a = a; bh.M = 1.0;

    KerrRay ray;
    KerrState state;
    kerr_ray_from_direction(rCam, kPi / 2,
                            -std::cos(psi), 0.0, std::sin(psi),
                            bh, &ray, &state);

    return kerr_trace(state, bh, ray, escapeRadius, 1e-10, 200000).outcome;
}

double shadowEdgeAngle(double a, double rCam, double loPsi, double hiPsi) {
    const double escapeRadius = rCam * 40.0;
    const int loOutcome = traceFromCamera(a, rCam, loPsi, escapeRadius);

    for (int i = 0; i < 70; ++i) {
        const double mid = 0.5 * (loPsi + hiPsi);
        if (traceFromCamera(a, rCam, mid, escapeRadius) == loOutcome) {
            loPsi = mid;
        } else {
            hiPsi = mid;
        }
    }
    return 0.5 * (loPsi + hiPsi);
}

} // NAMESPACE

int runValidation() {
    std::printf("\nKerr geodesic acceptance tests (double precision)\n");
    std::printf("================================================\n");

    // CLOSED-FORM ORBIT CHECKS.
    std::printf("\nOrbit radii from closed form\n");
    {
        KerrBH bh; bh.M = 1.0;

        bh.a = 0.0;
        check("horizon, a=0", kerr_horizon(bh), 2.0, 1e-12, "M");
        check("photon orbit, a=0", kerr_photon_orbit(bh, 1.0), 3.0, 1e-9, "M");
        check("ISCO, a=0", kerr_isco(bh, 1.0), 6.0, 1e-9, "M");

        bh.a = 0.99;
        check("horizon, a=0.99", kerr_horizon(bh),
              1.0 + std::sqrt(1.0 - 0.99 * 0.99), 1e-12, "M");
        check("ISCO prograde, a=0.99", kerr_isco(bh, 1.0), 1.4545, 1e-3, "M");

        bh.a = 1.0;
        check("photon orbit prograde, a=1", kerr_photon_orbit(bh, 1.0), 1.0, 1e-6, "M");
        check("photon orbit retrograde, a=1", kerr_photon_orbit(bh, -1.0), 4.0, 1e-6, "M");
    }

    // INTEGRATOR VS CRITICAL IMPACT PARAMETER.
    std::printf("\nCritical impact parameter, by bisecting traced rays\n");
    {
        // SCHWARZSCHILD SHADOW: EXACTLY 3*SQRT(3) M.
        const double bCrit = criticalImpactParameter(0.0, 4.0, 7.0, 1000.0);
        check("a=0 critical b", bCrit, 3.0 * std::sqrt(3.0), 1e-4, "M");

        // KERR PROGRADE AND RETROGRADE IMPACT LIMITS.
        KerrBH bh; bh.M = 1.0; bh.a = 0.99;
        const double rProg = kerr_photon_orbit(bh, 1.0);
        const double rRetro = kerr_photon_orbit(bh, -1.0);

        const double bProgExpected  = photonOrbitImpactParameter(0.99, rProg);
        const double bRetroExpected = photonOrbitImpactParameter(0.99, rRetro);

        const double bProg  = criticalImpactParameter(0.99,  1.0,  4.0, 1000.0);
        const double bRetro = criticalImpactParameter(0.99, -4.0, -8.0, 1000.0);

        check("a=0.99 critical b, prograde",  bProg,  bProgExpected,  1e-4, "M");
        check("a=0.99 critical b, retrograde", bRetro, bRetroExpected, 1e-4, "M");

        // NEAR EXTREMAL: +2M AND -7M ARE EXACT A=M LIMITS.
        bh.a = 0.9999;
        const double rProgX  = kerr_photon_orbit(bh, 1.0);
        const double rRetroX = kerr_photon_orbit(bh, -1.0);
        const double bProgX  = criticalImpactParameter(0.9999,  1.0,  4.0, 1000.0);
        const double bRetroX = criticalImpactParameter(0.9999, -4.0, -8.0, 1000.0);
        check("a=0.9999 critical b, prograde", bProgX,
              photonOrbitImpactParameter(0.9999, rProgX), 1e-3, "M");
        check("a=0.9999 critical b, retrograde", bRetroX,
              photonOrbitImpactParameter(0.9999, rRetroX), 1e-3, "M");
        std::printf("       extremal limits are +2M and -7M; at a=0.9999 the "
                    "prograde branch is %.4f\n", bProgX);

        checkTrue("no ray stalled during bisection", gExhausted == 0,
                  gExhausted == 0 ? "every ray resolved to captured or escaped"
                                  : "some rays ran out of steps");
    }

    // CAMERA TETRAD AND SHADOW RADIUS.
    std::printf("\nShadow angular radius from a camera at finite distance\n");
    {
        // SHADOW HALF-ANGLE AT RCAM: B = R*SIN(PSI)/SQRT(1-2M/R).
        const double rCam = 30.0;
        const double expected = std::asin(3.0 * std::sqrt(3.0) *
                                          std::sqrt(1.0 - 2.0 / rCam) / rCam);
        const double measured = shadowEdgeAngle(0.0, rCam, 0.02, 0.40);
        check("a=0 shadow half-angle at r=30M", measured, expected, 1e-5, "rad");

        // FRAME DRAGGING SHIFT: PROGRADE AND RETROGRADE EDGES MUST DIFFER.
        const double prograde  = shadowEdgeAngle(0.99, rCam,  0.02, 0.40);
        const double retrograde = shadowEdgeAngle(0.99, rCam, -0.02, -0.40);
        std::printf("       a=0.99 prograde edge %.7f rad, retrograde edge %.7f rad\n",
                    prograde, std::fabs(retrograde));
        checkTrue("a=0.99 shadow is asymmetric",
                  std::fabs(prograde - std::fabs(retrograde)) > 0.02,
                  "prograde and retrograde edges differ, as frame dragging requires");
    }

    // WEAK FIELD DEFLECTION SERIES (KEETON-PETTERS 2005). MOMENTUM NOT POSITION.
    std::printf("\nLight deflection, against the known weak-field series\n");
    {
        KerrBH bh; bh.a = 0.0; bh.M = 1.0;

        const double radius[] = {100.0, 1000.0};
        for (double b : radius) {
            KerrRay ray; ray.b = b; ray.Q = 0.0;

            const double r0 = 1.0e7;
            KerrState s;
            s.r = r0; s.theta = kPi / 2; s.phi = 0; s.t = 0;
            s.pth = 0.0;
            s.pr = -std::sqrt(kerr_R(r0, bh, ray)) / kerr_delta(r0, bh);

            double ix, iy, iz;
            kerr_escape_direction(s, bh, ray, &ix, &iy, &iz);

            KerrTrace out = kerr_trace(s, bh, ray, r0, 1e-12, 400000);
            if (out.outcome != KERR_ESCAPED) {
                checkTrue("deflection ray escapes", false, "ray did not escape");
                continue;
            }

            const double dot = ix * out.dirX + iy * out.dirY + iz * out.dirZ;
            const double measured = std::acos(dot < -1.0 ? -1.0 : (dot > 1.0 ? 1.0 : dot));

            const double x = 1.0 / b;
            const double expected = 4.0 * x
                                  + (15.0 * kPi / 4.0) * x * x
                                  + (128.0 / 3.0) * x * x * x;

            char label[64];
            std::snprintf(label, sizeof(label), "deflection at b=%.0fM (%d steps)",
                          b, out.steps);
            check(label, measured, expected, 1e-5, "rad");
        }
    }

    // STEP CONTROLLER CONVERGENCE.
    std::printf("\nStep controller\n");
    {
        // SAME RAY AT TIGHT AND LOOSE TOLERANCES MUST HIT SAME SKY POINT.
        KerrBH bh; bh.a = 0.9; bh.M = 1.0;
        KerrRay ray;
        KerrState start;
        kerr_ray_from_direction(20.0, kPi / 2 - 0.3, -0.98, 0.14, 0.14,
                                bh, &ray, &start);

        KerrTrace loose = kerr_trace(start, bh, ray, 1000.0, 1e-8,  200000);
        KerrTrace tight = kerr_trace(start, bh, ray, 1000.0, 1e-12, 200000);

        checkTrue("ray escapes at both tolerances",
                  loose.outcome == KERR_ESCAPED && tight.outcome == KERR_ESCAPED,
                  loose.outcome == KERR_ESCAPED ? "escaped" : "did not escape");

        if (loose.outcome == KERR_ESCAPED && tight.outcome == KERR_ESCAPED) {
            const double dot = loose.dirX * tight.dirX + loose.dirY * tight.dirY +
                               loose.dirZ * tight.dirZ;
            const double spread = std::acos(dot > 1.0 ? 1.0 : dot);
            std::printf("       loose %d steps, tight %d steps\n", loose.steps, tight.steps);
            check("sky direction agreement across tolerances", spread, 0.0, 1e-6, "rad");
        }
    }

    // CIRCULAR ORBITS AND REDSHIFT.
    std::printf("\nEquatorial circular orbits\n");
    {
        KerrBH bh; bh.M = 1.0; bh.a = 0.0;

        // SCHWARZSCHILD TIME DILATION COLLAPSES TO 1/SQRT(1-3M/R).
        for (double r : {6.0, 10.0, 30.0}) {
            char label[64];
            std::snprintf(label, sizeof(label), "u^t at a=0, r=%.0fM", r);
            check(label, disk_ut(r, bh), 1.0 / std::sqrt(1.0 - 3.0 / r), 1e-12, "");
        }
        check("Omega at a=0, r=6M", disk_omega(6.0, bh),
              1.0 / std::pow(6.0, 1.5), 1e-14, "1/M");

        // SCHWARZSCHILD CIRCULAR ORBIT: E = (1-2M/R)/SQRT(1-3M/R).
        check("E at a=0, r=1000M", disk_energy(1000.0, bh),
              (1.0 - 2.0 / 1000.0) / std::sqrt(1.0 - 3.0 / 1000.0), 1e-12, "");

        bh.a = 0.99;
        const double isco = kerr_isco(bh, 1.0);
        checkTrue("u^t finite at the ISCO for a=0.99",
                  std::isfinite(disk_ut(isco * 1.0001, bh)), "orbit is well defined there");

        // DOPPLER SIGN CONVENTION: APPROACHING MEANS OMEGA*B > 0.
        const double r = 8.0;
        const double gStill = disk_redshift(r, 0.0, bh);
        const double gApproach = disk_redshift(r, 6.0, bh);
        const double gRecede = disk_redshift(r, -6.0, bh);
        std::printf("       r=8M  g(b=+6, approaching) %.4f   g(b=0) %.4f   "
                    "g(b=-6, receding) %.4f\n", gApproach, gStill, gRecede);
        checkTrue("Doppler shifts the two limbs oppositely",
                  gApproach > gStill && gStill > gRecede,
                  "one limb blueshifted, the other redshifted, static case between");
        checkTrue("gravitational redshift present at b=0", gStill < 1.0,
                  "light from a circular orbit is redshifted even with no Doppler");
    }

    // BLACKBODY CHROMATICITY VS PLANCKIAN LOCUS.
    std::printf("\nBlackbody chromaticity against the Planckian locus\n");
    {
        // CIE 1931 PLANCKIAN LOCUS FIT ACCURACY TEST.
        struct Point { double kelvin, x, y; };
        const Point locus[] = {
            {3000.0,  0.4369, 0.4041},
            {4000.0,  0.3805, 0.3768},
            {5000.0,  0.3451, 0.3516},
            {6500.0,  0.3135, 0.3237},
            {10000.0, 0.2807, 0.2884},
        };

        for (const Point& p : locus) {
            double x = 0.0, y = 0.0;
            blackbodyChromaticity(p.kelvin, x, y);
            char label[64];
            std::snprintf(label, sizeof(label), "%.0fK chromaticity x", p.kelvin);
            check(label, x, p.x, 0.012, "");
            std::snprintf(label, sizeof(label), "%.0fK chromaticity y", p.kelvin);
            check(label, y, p.y, 0.012, "");
        }
    }

    // PAGE-THORNE ACCRETION DISK RADIAL PROFILE.
    std::printf("\nPage-Thorne radial profile\n");
    {
        const DiskProfile p = buildDiskProfile(0.99f, 1.0f, 25.0f);
        KerrBH bh; bh.M = 1.0; bh.a = 0.99;

        check("inner edge sits at the ISCO", p.innerRadius,
              kerr_isco(bh, 1.0), 1e-5, "M");
        checkTrue("flux vanishes at the inner edge", p.temperature.front() < 0.05f,
                  "zero-torque boundary condition holds");
        checkTrue("profile peaks just outside the ISCO",
                  p.peakRadius > p.innerRadius && p.peakRadius < 4.0f * p.innerRadius,
                  "peak radius is where a thin disk should be hottest");
        checkTrue("profile decays outward",
                  p.temperature.back() < 0.4f * p.temperature[kDiskProfileSamples / 8],
                  "outer disk is much cooler than the inner disk");
        std::printf("       a=0.99: ISCO %.4fM, peak at %.4fM, "
                    "T(outer)/T(peak) = %.3f\n",
                    p.innerRadius, p.peakRadius, double(p.temperature.back()));
    }

    // AGX TONE MAPPING CURVE.
    std::printf("\nAgX tone curve\n");
    {
        // NEUTRAL IN MUST STAY NEUTRAL. CATCHES MATRIX TYPOS.
        double worstTint = 0.0;
        for (double v : {0.02, 0.18, 0.5, 1.0, 8.0, 100.0}) {
            const TmRgb out = tm_agx(tm_make(float(v), float(v), float(v)), 1.0f, 1.0f, 1.0f);
            const double tint = std::max(std::fabs(double(out.r) - double(out.g)),
                                        std::fabs(double(out.g) - double(out.b)));
            worstTint = std::max(worstTint, tint);
        }
        check("neutral input stays neutral", worstTint, 0.0, 2e-3, "");

        // MONOTONIC AND BOUNDED IN [0, 1]. NO REVERSALS.
        bool monotonic = true;
        bool bounded = true;
        double previous = -1.0;
        for (int i = 0; i <= 400; ++i) {
            const double v = std::pow(10.0, -4.0 + 6.0 * i / 400.0);
            const TmRgb out = tm_agx(tm_make(float(v), float(v), float(v)), 1.0f, 1.0f, 1.0f);
            if (!std::isfinite(out.r)) { bounded = false; break; }
            if (out.r < -1e-6 || out.r > 1.0 + 1e-6) { bounded = false; }
            if (out.r < previous - 1e-6) { monotonic = false; }
            previous = out.r;
        }
        checkTrue("tone curve is monotonic", monotonic, "no reversals across six decades");
        checkTrue("tone curve stays finite and in [0,1]", bounded,
                  "no NaN and no out-of-range output");

        // MIDDLE GREY (0.18) MAPS TO MIDTONE.
        const TmRgb grey = tm_agx(tm_make(0.18f, 0.18f, 0.18f), 1.0f, 1.0f, 1.0f);
        std::printf("       0.18 linear maps to %.4f linear display\n", double(grey.r));
        checkTrue("middle grey lands in a sane place",
                  grey.r > 0.08 && grey.r < 0.30, "0.18 in gives a plausible mid tone");

        // HIGH HDR VALUES MUST CLAMP AND NOT EXPLODE.
        const TmRgb hot = tm_agx(tm_make(5000.0f, 400.0f, 40.0f), 1.0f, 1.0f, 1.0f);
        checkTrue("extreme HDR input rolls off",
                  hot.r <= 1.0 && hot.g <= 1.0 && hot.b <= 1.0 &&
                  std::isfinite(hot.r) && std::isfinite(hot.g),
                  "a very bright, very saturated colour clamps cleanly");
    }

    // TIMELIKE FREE FALL TRAJECTORY.
    std::printf("\nTimelike free fall\n");
    {
        // SCHWARZSCHILD RADIAL INFALL CYCLOID SOLUTION CHECK.
        const double r0 = 20.0;
        const double target = 3.0;
        const double eta = std::acos(2.0 * target / r0 - 1.0);
        const double expected = std::sqrt(r0 * r0 * r0 / 8.0) * (eta + std::sin(eta));

        FreeFallState s = freeFallFromRest(0.0, 1.0, r0, kPi / 2);
        double tau = 0.0;
        while (s.r > target && tau < 400.0) {
            if (!freeFallAdvance(s, 0.0, 1.0, 1.0e-3)) break;
            tau += 1.0e-3;
        }
        check("proper time, r=20M to r=3M, a=0", tau, expected, 2e-3, "M");

        // ENERGY CONSERVED ALONG TIMELIKE WORLDLINE.
        FreeFallState e = freeFallFromRest(0.0, 1.0, r0, kPi / 2);
        const double e0 = e.energy;
        check("E at release equals sqrt(1-2M/r0)", e0, std::sqrt(1.0 - 2.0 / r0), 1e-12, "");
        for (int i = 0; i < 200; ++i) { freeFallAdvance(e, 0.0, 1.0, 0.01); }
        check("E conserved along the fall", e.energy, e0, 1e-14, "");

        // STATIC OBSERVER IN KERR HAS NEGATIVE ANGULAR MOMENTUM.
        const FreeFallState k = freeFallFromRest(0.99, 1.0, 20.0, kPi / 2);
        checkTrue("static observer has negative L in Kerr", k.angmom < 0.0,
                  "standing still against frame dragging is itself motion");

        // FALL FROM REST HOLDS THETA IN SCHWARZSCHILD.
        FreeFallState flat = freeFallFromRest(0.0, 1.0, 20.0, kPi / 2 - 0.35);
        const double flatTheta = flat.theta;
        for (int i = 0; i < 4000; ++i) { if (!freeFallAdvance(flat, 0.0, 1.0, 0.01)) break; }
        check("a=0 fall from rest holds theta", flat.theta, flatTheta, 1e-12, "rad");

        // CROSSING TRAJECTORY PUNCHES THROUGH DISK MIDPLANE.
        FreeFallState d = freeFallFromVelocity(0.99, 1.0, 30.0, 1.2708,
                                               -0.030, 0.0010, 0.0);
        const double th0 = d.theta;
        double crossRadius = -1.0;
        for (int i = 0; i < 60000 && crossRadius < 0.0; ++i) {
            if (!freeFallAdvance(d, 0.99, 1.0, 5.0e-3)) break;
            if (d.theta >= kPi / 2) crossRadius = d.r;
        }
        std::printf("       released at theta=%.3f, crossed the equator at r=%.2fM\n",
                    th0, crossRadius);
        checkTrue("fall with theta motion crosses the equator", crossRadius > 0.0,
                  "the trajectory passes through the disk plane");
        checkTrue("crossing happens inside the disk", crossRadius > 1.5 && crossRadius < 19.0,
                  "the camera punches through the gas, not outside it");

        // CAMERA LOCAL SPEED CLIMBS RELATIVISTIC BUT STAYS SUBLUMINAL.
        FreeFallState v = freeFallFromRest(0.99, 1.0, 30.0, kPi / 2);
        double maxSpeed = 0.0;
        bool subluminal = true;
        // FULL INFALL INTEGRATION TO HORIZON.
        for (int i = 0; i < 400000; ++i) {
            if (!freeFallAdvance(v, 0.99, 1.0, 1.0e-3)) break;
            const FreeFallFrame f = freeFallFrame(v, 0.99, 1.0);
            maxSpeed = std::max(maxSpeed, f.speed);
            if (!(f.speed < 1.0) || !std::isfinite(f.gamma)) subluminal = false;
        }
        std::printf("       peak speed relative to the local frame: %.4f c\n", maxSpeed);
        checkTrue("camera stays subluminal all the way in", subluminal,
                  "beta < 1 and gamma finite at every step");
        checkTrue("camera actually gets relativistic", maxSpeed > 0.5,
                  "speed climbs past half light speed near the horizon");
    }

    std::printf("\n================================================\n");
    std::printf("%d checks, %d failures\n\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

} // NAMESPACE BHS
