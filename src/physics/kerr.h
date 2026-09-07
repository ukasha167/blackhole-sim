#pragma once

// KERR NULL GEODESICS IN BOYER-LINDQUIST. G = C = 1.
// COMPILES FOR MSL FLOAT AND CPP DOUBLE. SAME SOURCE PREVENTS DRIFT.
// DEFINE KERR_USE_DOUBLE FOR DOUBLE PRECISION.

#ifdef __METAL_VERSION__
    #include <metal_stdlib>
    using namespace metal;
    typedef float kreal;
    #define KERR_FN static inline
    #define KERR_THREAD thread
#else
    #include <cmath>
    using std::sqrt; using std::sin; using std::cos; using std::fabs;
    using std::copysign; using std::fmin; using std::fmax; using std::pow;
    using std::acos;
    #ifdef KERR_USE_DOUBLE
        typedef double kreal;
    #else
        typedef float kreal;
    #endif
    #define KERR_FN static inline
    #define KERR_THREAD
#endif

// PARAMETERS AND STATE

struct KerrBH {
    kreal a;   // SPIN, |A| <= M
    kreal M;   // MASS AND LENGTH UNIT
};

// B AND Q ARE MOTION CONSTANTS FIXED PER RAY.
// ENERGY IS 1. P_T = -1 AND P_PHI = B.
struct KerrRay {
    kreal b;
    kreal Q;
};

// STATE AND MOMENTA. TRACK T FOR DISK EMISSION DELAY.
struct KerrState {
    kreal r, theta, phi, t, pr, pth;
};

// METRIC QUANTITIES

KERR_FN kreal kerr_delta(kreal r, KerrBH bh) {
    return r * r - 2 * bh.M * r + bh.a * bh.a;
}

KERR_FN kreal kerr_rho2(kreal r, kreal cosTheta, KerrBH bh) {
    return r * r + bh.a * bh.a * cosTheta * cosTheta;
}

KERR_FN kreal kerr_sigma2(kreal r, kreal sinTheta, KerrBH bh) {
    const kreal r2a2 = r * r + bh.a * bh.a;
    return r2a2 * r2a2 - bh.a * bh.a * kerr_delta(r, bh) * sinTheta * sinTheta;
}

KERR_FN kreal kerr_horizon(KerrBH bh) {
    return bh.M + sqrt(fmax(bh.M * bh.M - bh.a * bh.a, kreal(0)));
}

// EQUATORIAL PHOTON ORBIT RADIUS. DIRECTION +1 PROGRADE, -1 RETROGRADE.
KERR_FN kreal kerr_photon_orbit(KerrBH bh, kreal direction) {
    const kreal x = fmax(kreal(-1), fmin(kreal(1), -direction * bh.a / bh.M));
    return 2 * bh.M * (1 + cos(kreal(2.0 / 3.0) * acos(x)));
}

// ISCO RADIUS. DIRECTION +1 PROGRADE, -1 RETROGRADE.
KERR_FN kreal kerr_isco(KerrBH bh, kreal direction) {
    const kreal chi = bh.a / bh.M;
    const kreal chi2 = chi * chi;
    const kreal z1 = 1 + pow(fmax(1 - chi2, kreal(0)), kreal(1.0 / 3.0)) *
                         (pow(1 + chi, kreal(1.0 / 3.0)) + pow(fmax(1 - chi, kreal(0)), kreal(1.0 / 3.0)));
    const kreal z2 = sqrt(fmax(3 * chi2 + z1 * z1, kreal(0)));
    const kreal root = sqrt(fmax((3 - z1) * (3 + z1 + 2 * z2), kreal(0)));
    return bh.M * (3 + z2 - direction * root);
}

// CARTER SEPARATED POTENTIALS.
// (DELTA * P_R)^2 = R(R) AND P_THETA^2 = THETA(THETA).

KERR_FN kreal kerr_R(kreal r, KerrBH bh, KerrRay ray) {
    const kreal W = r * r + bh.a * bh.a - bh.a * ray.b;
    const kreal K = (ray.b - bh.a) * (ray.b - bh.a) + ray.Q;
    return W * W - kerr_delta(r, bh) * K;
}

KERR_FN kreal kerr_Theta(kreal cosTheta, kreal sinTheta, KerrBH bh, KerrRay ray) {
    const kreal s2 = fmax(sinTheta * sinTheta, kreal(1e-12));
    const kreal c2 = cosTheta * cosTheta;
    return ray.Q + bh.a * bh.a * c2 - ray.b * ray.b * c2 / s2;
}

// HAMILTONIAN EQUATIONS OF MOTION.
// CARRY P_R AND P_THETA TO PASS TURNING POINTS SMOOTHLY. NO SIGN FLIP HACKS.
// D(RHO^2) DROPPED BECAUSE NULL HAMILTONIAN IS IDENTICALLY ZERO.

struct KerrDeriv {
    kreal dr, dtheta, dphi, dt, dpr, dpth;
};

KERR_FN KerrDeriv kerr_derivative(KerrState s, KerrBH bh, KerrRay ray) {
    const kreal sinTheta = sin(s.theta);
    const kreal cosTheta = cos(s.theta);
    const kreal s2 = fmax(sinTheta * sinTheta, kreal(1e-12));

    const kreal delta = kerr_delta(s.r, bh);
    const kreal safeDelta = copysign(fmax(fabs(delta), kreal(1e-9)), delta);
    const kreal rho2 = fmax(kerr_rho2(s.r, cosTheta, bh), kreal(1e-9));
    const kreal invRho2 = 1 / rho2;

    const kreal W = s.r * s.r + bh.a * bh.a - bh.a * ray.b;
    const kreal K = (ray.b - bh.a) * (ray.b - bh.a) + ray.Q;
    const kreal R = W * W - delta * K;
    const kreal dDelta = 2 * (s.r - bh.M);
    const kreal dR = 4 * s.r * W - dDelta * K;

    KerrDeriv d;
    d.dr     = delta * s.pr * invRho2;
    d.dtheta = s.pth * invRho2;

    // REWRITTEN TO AVOID CANCELLATION.
    d.dphi = (2 * bh.a * bh.M * s.r / safeDelta +
              ray.b * (1 / s2 - bh.a * bh.a / safeDelta)) * invRho2;

    d.dt = (kerr_sigma2(s.r, sinTheta, bh) / safeDelta -
            2 * bh.a * bh.M * s.r * ray.b / safeDelta) * invRho2;

    d.dpr = (dR * safeDelta - 2 * R * dDelta) * invRho2 /
            (2 * safeDelta * safeDelta);

    // THETA'(THETA) / (2 RHO^2).
    const kreal dTheta = -bh.a * bh.a * 2 * sinTheta * cosTheta +
                          2 * ray.b * ray.b * cosTheta / (s2 * sinTheta);
    d.dpth = dTheta * invRho2 / 2;

    return d;
}

// PROJECT MOMENTA TO EXACT POTENTIAL VALUES. KEEPS INTEGRATOR SIGNS.
// KILLS SECULAR DRIFT AND ALLOWS LARGER STEPS.
KERR_FN KerrState kerr_project(KerrState s, KerrBH bh, KerrRay ray) {
    const kreal sinTheta = sin(s.theta);
    const kreal cosTheta = cos(s.theta);

    const kreal delta = kerr_delta(s.r, bh);
    const kreal safeDelta = copysign(fmax(fabs(delta), kreal(1e-9)), delta);

    const kreal R = fmax(kerr_R(s.r, bh, ray), kreal(0));
    const kreal T = fmax(kerr_Theta(cosTheta, sinTheta, bh, ray), kreal(0));

    s.pr  = copysign(sqrt(R) / fabs(safeDelta), s.pr);
    s.pth = copysign(sqrt(T), s.pth);
    return s;
}

// CAMERA SETUP.
// FIDO/ZAMO LOCAL FRAME. VALID EVERYWHERE OUTSIDE HORIZON INCLUDING ERGOSPHERE.

struct KerrTetrad {
    kreal alpha;   // LAPSE
    kreal omega;   // FRAME DRAGGING ANGULAR VELOCITY
    kreal varpi;   // CYLINDRICAL RADIUS
    kreal rho;
    kreal sqrtDelta;
};

KERR_FN KerrTetrad kerr_tetrad(kreal r, kreal theta, KerrBH bh) {
    const kreal sinTheta = sin(theta);
    const kreal cosTheta = cos(theta);
    const kreal delta = kerr_delta(r, bh);
    const kreal rho2 = kerr_rho2(r, cosTheta, bh);
    const kreal sigma2 = kerr_sigma2(r, sinTheta, bh);

    KerrTetrad tet;
    tet.rho       = sqrt(fmax(rho2, kreal(1e-12)));
    tet.sqrtDelta = sqrt(fmax(delta, kreal(1e-12)));
    const kreal sigma = sqrt(fmax(sigma2, kreal(1e-12)));

    tet.alpha = tet.rho * tet.sqrtDelta / sigma;
    tet.omega = 2 * bh.a * bh.M * r / fmax(sigma2, kreal(1e-12));
    tet.varpi = sigma * sinTheta / tet.rho;
    return tet;
}

// BUILD BACKWARD RAY FROM FIDO SKY DIRECTION.
KERR_FN void kerr_ray_from_direction(kreal r, kreal theta,
                                     kreal Nr, kreal Nth, kreal Nph,
                                     KerrBH bh,
                                     KERR_THREAD KerrRay* outRay,
                                     KERR_THREAD KerrState* outState,
                                     KERR_THREAD kreal* outEnergyFactor) {
    const KerrTetrad tet = kerr_tetrad(r, theta, bh);

    // CONSERVED ENERGY FOR UNIT FIDO ENERGY PHOTON.
    const kreal E = tet.alpha + tet.omega * tet.varpi * Nph;
    const kreal invE = 1 / E;

    // RETURN ENERGY FACTOR TO CORRECT REDSHIFT AT FINITE RADIUS.
    *outEnergyFactor = E;

    const kreal b   = tet.varpi * Nph * invE;
    const kreal pr  = tet.rho * Nr * invE / tet.sqrtDelta;
    const kreal pth = tet.rho * Nth * invE;

    const kreal sinTheta = sin(theta);
    const kreal cosTheta = cos(theta);
    const kreal s2 = fmax(sinTheta * sinTheta, kreal(1e-12));

    outRay->b = b;
    outRay->Q = pth * pth + cosTheta * cosTheta * (b * b / s2 - bh.a * bh.a);

    outState->r     = r;
    outState->theta = theta;
    outState->phi   = 0;
    outState->t     = 0;
    outState->pr    = pr;
    outState->pth   = pth;
}

// ASYMPTOTIC SKY DIRECTION FROM RAY 3-VELOCITY IN FLAT LIMIT.
KERR_FN void kerr_escape_direction(KerrState s, KerrBH bh, KerrRay ray,
                                   KERR_THREAD kreal* outX,
                                   KERR_THREAD kreal* outY,
                                   KERR_THREAD kreal* outZ) {
    const KerrDeriv d = kerr_derivative(s, bh, ray);

    const kreal sinTheta = sin(s.theta);
    const kreal cosTheta = cos(s.theta);
    const kreal sinPhi   = sin(s.phi);
    const kreal cosPhi   = cos(s.phi);

    // ORTHONORMAL COMPONENTS IN ASYMPTOTIC LIMIT.
    const kreal vr  = d.dr;
    const kreal vth = s.r * d.dtheta;
    const kreal vph = s.r * sinTheta * d.dphi;

    kreal x = vr * sinTheta * cosPhi + vth * cosTheta * cosPhi - vph * sinPhi;
    kreal y = vr * sinTheta * sinPhi + vth * cosTheta * sinPhi + vph * cosPhi;
    kreal z = vr * cosTheta          - vth * sinTheta;

    const kreal len = fmax(sqrt(x * x + y * y + z * z), kreal(1e-20));
    *outX = x / len;
    *outY = y / len;
    *outZ = z / len;
}

// INTEGRATOR.
// DORMAND-PRINCE 5(4) WITH FSAL. SIX EVALS PER ACCEPTED STEP.
// ERROR CONTROL ON R, THETA, P_R, P_THETA ONLY. PHI AND T DO NOT FEED BACK.

KERR_FN void kerr_state_to_array(KerrState s, KERR_THREAD kreal* out) {
    out[0] = s.r;  out[1] = s.theta; out[2] = s.phi;
    out[3] = s.t;  out[4] = s.pr;    out[5] = s.pth;
}

KERR_FN KerrState kerr_state_from_array(const KERR_THREAD kreal* in) {
    KerrState s;
    s.r   = in[0]; s.theta = in[1]; s.phi = in[2];
    s.t   = in[3]; s.pr    = in[4]; s.pth = in[5];
    return s;
}

KERR_FN void kerr_deriv_to_array(KerrDeriv d, KERR_THREAD kreal* out) {
    out[0] = d.dr; out[1] = d.dtheta; out[2] = d.dphi;
    out[3] = d.dt; out[4] = d.dpr;    out[5] = d.dpth;
}

// EQUATORIAL PLANE CROSSINGS. DIRECT IMAGE PLUS LENSED ECHOES.
#define KERR_MAX_CROSSINGS 3

// MIDPOINT SAMPLES PER STEP NEAR GAS.
#define KERR_DISK_SUBSTEPS 8

// INTEGRATED GAS PASSAGE. R, PHI, T ARE DENSITY-WEIGHTED MEANS.
struct KerrCrossing {
    kreal r;
    kreal phi;
    kreal t;
    kreal column;  // INTEGRATED VERTICAL GAUSSIAN COLUMN DENSITY
};

struct KerrCrossings {
    int count;
    KerrCrossing hit[KERR_MAX_CROSSINGS];
};

// CUBIC HERMITE DENSE OUTPUT USING EXISTING DERIVATIVES.
KERR_FN kreal kerr_hermite(kreal y0, kreal y1, kreal d0, kreal d1, kreal h, kreal u) {
    const kreal u2 = u * u;
    const kreal u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * y0
         + (u3 - 2 * u2 + u) * h * d0
         + (-2 * u3 + 3 * u2) * y1
         + (u3 - u2) * h * d1;
}

struct KerrTrace {
    int   outcome;             // 0 CAPTURED, 1 ESCAPED, 2 EXHAUSTED
    int   steps;
    kreal dirX, dirY, dirZ;    // ASYMPTOTIC SKY DIRECTION
    kreal minRadius;           // CLOSEST APPROACH
};

#define KERR_CAPTURED 0
#define KERR_ESCAPED  1
#define KERR_EXHAUSTED 2

// CLOSE OPEN PASSAGE. CALLED ON LEAVING GAS OR TRACE TERMINATION.
KERR_FN void kerr_close_passage(KERR_THREAD KerrCrossings* crossings,
                                KERR_THREAD kreal* acc, kreal scaleHeight) {
    if (acc[0] <= kreal(0)) {
        return;
    }
    const kreal inv = kreal(1) / acc[0];
    const kreal meanR = acc[1] * inv;
    // NORMALIZE BY SCALE HEIGHT TO GET OPTICAL DEPTH.
    const kreal column = acc[0] / fmax(scaleHeight * meanR, kreal(1e-6));

    // DROP GRAZING WHISKERS TO PREVENT STEALING SLOTS FROM REAL CROSSINGS.
    if (column > kreal(0.02) && crossings->count < KERR_MAX_CROSSINGS) {
        const int slot = crossings->count++;
        crossings->hit[slot].r      = meanR;
        crossings->hit[slot].phi    = acc[2] * inv;
        crossings->hit[slot].t      = acc[3] * inv;
        crossings->hit[slot].column = column;
    }
    acc[0] = 0; acc[1] = 0; acc[2] = 0; acc[3] = 0;
}

KERR_FN KerrTrace kerr_trace_full(KerrState y, KerrBH bh, KerrRay ray,
                                  kreal escapeRadius, kreal tolerance,
                                  int maxSteps,
                                  kreal diskInner, kreal diskOuter,
                                  kreal diskScaleHeight, kreal diskStepScale,
                                  KERR_THREAD KerrCrossings* crossings) {
    crossings->count = 0;
    const kreal horizon = kerr_horizon(bh);

    // STOP MIDWAY BETWEEN HORIZON AND INNER PHOTON ORBIT.
    // INGOING RAY HAS NO TURNING POINTS LEFT AND MUST ENTER HORIZON.
    // AVOIDS BOYER-LINDQUIST COORDINATE SINGULARITY AT DELTA = 0.
    const kreal photonOrbit = kerr_photon_orbit(bh, kreal(1));
    const kreal captureRadius = horizon + kreal(0.5) * (photonOrbit - horizon);

    KerrTrace result;
    result.outcome = KERR_EXHAUSTED;
    result.steps = 0;
    result.dirX = 0; result.dirY = 0; result.dirZ = 0;
    result.minRadius = y.r;

    kreal h = kreal(0.05) * fmax(y.r - horizon, kreal(0.1));

    // ACCUMULATE WEIGHT, R, PHI, T, AND LAST SAMPLE POSITIONS.
    kreal acc[6] = {0, 0, 0, 0, 0, 0};

    kreal ys[6], k1a[6], k2a[6], k3a[6], k4a[6], k5a[6], k6a[6], k7a[6], tmp[6];
    kerr_state_to_array(y, ys);
    kerr_deriv_to_array(kerr_derivative(y, bh, ray), k1a);

    for (int step = 0; step < maxSteps; ++step) {
        result.steps = step + 1;

        // LIMIT STEP NEAR HORIZON.
        const kreal radial = fmax(fabs(ys[0] - horizon), kreal(1e-3));
        h = fmin(h, kreal(0.35) * radial);

        // LIMIT STEP SIZE NEAR DISK TO RESOLVE GAUSSIAN VOLUME WITHOUT ARTIFACTS.
        if (diskOuter > diskInner) {
            const kreal dr = fabs(k1a[0]);
            if (ys[0] + dr * h >= diskInner && ys[0] - dr * h <= diskOuter * kreal(1.05)) {
                const kreal cosTh = cos(ys[1]);
                const kreal z     = ys[0] * cosTh;
                const kreal scale = diskScaleHeight * ys[0];
                const kreal dz    = fabs(k1a[0] * cosTh - ys[0] * sin(ys[1]) * k1a[1]);
                if (dz > kreal(1e-12)) {
                    // DISK STEPSCALE RELAXES STEP LIMIT FOR REALTIME INCOMING CAM.
                    h = fmin(h, diskStepScale *
                                (kreal(0.5) * scale + kreal(0.25) * fabs(z)) / dz);
                }
            }
        }

        h = fmax(h, kreal(1e-6));

        for (int i = 0; i < 6; ++i) tmp[i] = ys[i] + h * (kreal(1.0/5.0) * k1a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k2a);

        for (int i = 0; i < 6; ++i)
            tmp[i] = ys[i] + h * (kreal(3.0/40.0) * k1a[i] + kreal(9.0/40.0) * k2a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k3a);

        for (int i = 0; i < 6; ++i)
            tmp[i] = ys[i] + h * (kreal(44.0/45.0) * k1a[i] - kreal(56.0/15.0) * k2a[i]
                                + kreal(32.0/9.0) * k3a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k4a);

        for (int i = 0; i < 6; ++i)
            tmp[i] = ys[i] + h * (kreal(19372.0/6561.0) * k1a[i] - kreal(25360.0/2187.0) * k2a[i]
                                + kreal(64448.0/6561.0) * k3a[i] - kreal(212.0/729.0) * k4a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k5a);

        for (int i = 0; i < 6; ++i)
            tmp[i] = ys[i] + h * (kreal(9017.0/3168.0) * k1a[i] - kreal(355.0/33.0) * k2a[i]
                                + kreal(46732.0/5247.0) * k3a[i] + kreal(49.0/176.0) * k4a[i]
                                - kreal(5103.0/18656.0) * k5a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k6a);

        // ORDER 5 SOLUTION. STAGE 7 EVALUATED HERE.
        for (int i = 0; i < 6; ++i)
            tmp[i] = ys[i] + h * (kreal(35.0/384.0) * k1a[i]
                                + kreal(500.0/1113.0) * k3a[i] + kreal(125.0/192.0) * k4a[i]
                                - kreal(2187.0/6784.0) * k5a[i] + kreal(11.0/84.0) * k6a[i]);
        kerr_deriv_to_array(kerr_derivative(kerr_state_from_array(tmp), bh, ray), k7a);

        kreal errNorm = 0;
        for (int j = 0; j < 4; ++j) {
            const int i = (j < 2) ? j : j + 2;   // R, THETA, P_R, P_THETA
            const kreal e = h * (kreal(71.0/57600.0) * k1a[i] - kreal(71.0/16695.0) * k3a[i]
                               + kreal(71.0/1920.0) * k4a[i] - kreal(17253.0/339200.0) * k5a[i]
                               + kreal(22.0/525.0) * k6a[i] - kreal(1.0/40.0) * k7a[i]);
            const kreal scale = tolerance * (1 + fmax(fabs(ys[i]), fabs(tmp[i])));
            const kreal ratio = e / scale;
            errNorm += ratio * ratio;
        }
        errNorm = sqrt(errNorm / 4);

        if (errNorm <= 1) {
            // SAVE PRE-STEP STATE AND SLOPE FOR DENSE OUTPUT INTERPOLATION.
            kreal prev[6], prevSlope[6];
            for (int i = 0; i < 6; ++i) { prev[i] = ys[i]; prevSlope[i] = k1a[i]; }

            for (int i = 0; i < 6; ++i) ys[i] = tmp[i];
            for (int i = 0; i < 6; ++i) k1a[i] = k7a[i];   // FSAL

            KerrState s = kerr_state_from_array(ys);
            s = kerr_project(s, bh, ray);
            kerr_state_to_array(s, ys);
            // RECOMPUTE DERIVATIVE AFTER CONSTRAINT PROJECTION.
            kerr_deriv_to_array(kerr_derivative(s, bh, ray), k1a);

            // INTEGRATE DISK AS SMOOTH VOLUME ALONG RAY. AVOIDS DISCONTINUITIES.
            if (diskOuter > diskInner) {
                const kreal rA = prev[0], thA = prev[1];
                const kreal rB = ys[0],   thB = ys[1];

                // FIND MINIMUM Z ALONG STEP VIA CUBIC HERMITE ROOTS.
                const kreal cA = cos(thA), cB = cos(thB);
                const kreal zA = rA * cA;
                const kreal zB = rB * cB;
                const kreal dA = h * (prevSlope[0] * cA - rA * sin(thA) * prevSlope[1]);
                const kreal dB = h * (k1a[0] * cB - rB * sin(thB) * k1a[1]);

                const kreal c3 =  2 * zA + dA - 2 * zB + dB;
                const kreal c2 = -3 * zA - 2 * dA + 3 * zB - dB;
                const kreal c1 =  dA;
                const kreal c0 =  zA;

                kreal lowest = fmin(fabs(zA), fabs(zB));
                const kreal qa = 3 * c3, qb = 2 * c2, qc = c1;
                const kreal disc = qb * qb - 4 * qa * qc;
                if (fabs(qa) > kreal(1e-14) && disc >= 0) {
                    const kreal root = sqrt(disc);
                    for (int sign = 0; sign < 2; ++sign) {
                        const kreal u = ((sign == 0 ? -qb + root : -qb - root)) / (2 * qa);
                        if (u > 0 && u < 1) {
                            lowest = fmin(lowest, fabs(((c3 * u + c2) * u + c1) * u + c0));
                        }
                    }
                }

                // SKIP IF STEP DOES NOT REACH 4 SCALE HEIGHTS.
                const kreal rHigh = fmax(rA, rB);
                const kreal rLow  = fmin(rA, rB);
                const bool worth = lowest < 4 * diskScaleHeight * rHigh &&
                                   rHigh >= diskInner && rLow <= diskOuter;

                if (worth) {
                    // MIDPOINT QUADRATURE ON DENSE OUTPUT.
                    const kreal dl = h / kreal(KERR_DISK_SUBSTEPS);
                    for (int sub = 0; sub < KERR_DISK_SUBSTEPS; ++sub) {
                        const kreal u = (kreal(sub) + kreal(0.5)) / kreal(KERR_DISK_SUBSTEPS);
                        const kreal rU = kerr_hermite(rA, rB, prevSlope[0], k1a[0], h, u);
                        if (rU < diskInner || rU > diskOuter) {
                            continue;
                        }
                        const kreal z = ((c3 * u + c2) * u + c1) * u + c0;
                        const kreal scale = diskScaleHeight * rU;
                        const kreal q = z / scale;
                        if (q * q > 32) {
                            continue;
                        }
                        const kreal w = exp(kreal(-0.5) * q * q) * dl;
                        acc[0] += w;
                        acc[1] += w * rU;
                        acc[2] += w * kerr_hermite(prev[2], ys[2], prevSlope[2], k1a[2], h, u);
                        acc[3] += w * kerr_hermite(prev[3], ys[3], prevSlope[3], k1a[3], h, u);
                        acc[4] = rU;
                        acc[5] = kerr_hermite(prev[2], ys[2], prevSlope[2], k1a[2], h, u);
                    }
                }

                // CLOSE PASSAGE ONLY IF RAY TRAVELED SIGNIFICANT DELTA R OR DELTA PHI.
                // PREVENTS MERGING DISTINCT LENSED ECHOES OR SPLITTING LOCAL BOBS.
                if (acc[0] > 0) {
                    const kreal gapR   = fabs(ys[0] - acc[4]);
                    const kreal gapPhi = fabs(ys[2] - acc[5]);
                    if (gapR > kreal(0.30) * fmax(acc[4], kreal(1)) ||
                        gapPhi > kreal(2) ||
                        ys[0] < diskInner || ys[0] > diskOuter) {
                        kerr_close_passage(crossings, acc, diskScaleHeight);
                    }
                }
            }

            result.minRadius = fmin(result.minRadius, s.r);

            if (s.r <= captureRadius) {
                result.outcome = KERR_CAPTURED;
                kerr_close_passage(crossings, acc, diskScaleHeight);
                return result;
            }
            if (s.r >= escapeRadius) {
                result.outcome = KERR_ESCAPED;
                kerr_escape_direction(s, bh, ray, &result.dirX, &result.dirY, &result.dirZ);
                kerr_close_passage(crossings, acc, diskScaleHeight);
                return result;
            }
        }

        // ADAPTIVE STEP CONTROLLER WITH SAFETY CLAMPS.
        const kreal factor = fmin(kreal(5), fmax(kreal(0.2),
                                  kreal(0.9) * pow(fmax(errNorm, kreal(1e-10)), kreal(-0.2))));
        h *= factor;
    }

    kerr_close_passage(crossings, acc, diskScaleHeight);
    return result;
}

// LENSING TRACE WITHOUT DISK. ACCEPTANCE TEST OVERLOAD.
KERR_FN void kerr_ray_from_direction(kreal r, kreal theta,
                                     kreal Nr, kreal Nth, kreal Nph,
                                     KerrBH bh,
                                     KERR_THREAD KerrRay* outRay,
                                     KERR_THREAD KerrState* outState) {
    kreal ignored;
    kerr_ray_from_direction(r, theta, Nr, Nth, Nph, bh, outRay, outState, &ignored);
}

// RELATIVISTIC ABERRATION. BOOST FROM CAMERA FRAME TO FIDO FRAME.
KERR_FN void kerr_aberrate(kreal Nr, kreal Nth, kreal Nph,
                           kreal bR, kreal bTh, kreal bPh, kreal gamma,
                           KERR_THREAD kreal* outR,
                           KERR_THREAD kreal* outTh,
                           KERR_THREAD kreal* outPh,
                           KERR_THREAD kreal* outDoppler) {
    const kreal speed = sqrt(bR * bR + bTh * bTh + bPh * bPh);
    if (speed < kreal(1e-9)) {
        *outR = Nr; *outTh = Nth; *outPh = Nph; *outDoppler = 1;
        return;
    }

    const kreal inv = 1 / speed;
    const kreal hR = bR * inv, hTh = bTh * inv, hPh = bPh * inv;

    // SPLIT ALONG AND PERP TO BOOST.
    const kreal along = Nr * hR + Nth * hTh + Nph * hPh;
    const kreal perpR = Nr - along * hR;
    const kreal perpTh = Nth - along * hTh;
    const kreal perpPh = Nph - along * hPh;

    const kreal denom = 1 + speed * along;
    const kreal newAlong = (along + speed) / denom;
    const kreal perpScale = 1 / (gamma * denom);

    kreal r = newAlong * hR + perpR * perpScale;
    kreal t = newAlong * hTh + perpTh * perpScale;
    kreal p = newAlong * hPh + perpPh * perpScale;

    const kreal len = fmax(sqrt(r * r + t * t + p * p), kreal(1e-12));
    *outR = r / len; *outTh = t / len; *outPh = p / len;

    // DOPPLER FACTOR IN FIDO FRAME.
    *outDoppler = gamma * (1 + speed * along);
}

KERR_FN KerrTrace kerr_trace(KerrState y, KerrBH bh, KerrRay ray,
                             kreal escapeRadius, kreal tolerance, int maxSteps) {
    KerrCrossings ignored;
    return kerr_trace_full(y, bh, ray, escapeRadius, tolerance, maxSteps,
                           kreal(0), kreal(0), kreal(0), kreal(1), &ignored);
}
