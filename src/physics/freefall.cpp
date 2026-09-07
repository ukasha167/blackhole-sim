#include "physics/freefall.hpp"

#include <algorithm>
#include <cmath>

namespace bhs {

namespace {

struct Metric {
    double delta;
    double rho2;
    double sigma2;
};

Metric metricAt(double r, double theta, double a, double M) {
    const double cosTheta = std::cos(theta);
    const double sinTheta = std::sin(theta);
    Metric m;
    m.delta = r * r - 2.0 * M * r + a * a;
    m.rho2 = r * r + a * a * cosTheta * cosTheta;
    const double r2a2 = r * r + a * a;
    m.sigma2 = r2a2 * r2a2 - a * a * m.delta * sinTheta * sinTheta;
    return m;
}

// HAMILTONIAN POTENTIALS VR(R) AND VTHETA(THETA). ANALYTIC DERIVATIVES.

struct Deriv {
    double dr, dtheta, dphi, dt, dpr, dptheta;
};

Deriv derivative(const FreeFallState& s, double a, double M) {
    const Metric m = metricAt(s.r, s.theta, a, M);
    const double delta = (std::fabs(m.delta) < 1e-12) ? 1e-12 : m.delta;
    const double invRho2 = 1.0 / m.rho2;

    const double sinTheta = std::sin(s.theta);
    const double cosTheta = std::cos(s.theta);
    const double sin2 = std::max(sinTheta * sinTheta, 1e-12);

    const double E = s.energy;
    const double L = s.angmom;
    const double W = E * (s.r * s.r + a * a) - a * L;

    // POTENTIAL DERIVATIVES W.R.T L.
    const double dVrdL = 2.0 * a * W / delta - 2.0 * a * E;
    const double dVthdL = 2.0 * L / sin2;

    // POTENTIAL DERIVATIVES W.R.T E.
    const double dVrdE = -2.0 * W * (s.r * s.r + a * a) / delta - 2.0 * a * L;
    const double dVthdE = 2.0 * a * a * E * sinTheta * sinTheta;

    const double dDelta = 2.0 * (s.r - M);
    const double dVrdr = -(4.0 * E * s.r * W * delta - W * W * dDelta) / (delta * delta);
    const double dVthdth = a * a * E * E * std::sin(2.0 * s.theta)
                         - 2.0 * L * L * cosTheta / (sin2 * sinTheta);

    Deriv d;
    d.dr     = delta * s.pr * invRho2;
    d.dtheta = s.ptheta * invRho2;
    d.dphi   = 0.5 * invRho2 * (dVrdL + dVthdL);
    // P_T = -E SO D/DP_T = -D/DE.
    d.dt     = -0.5 * invRho2 * (dVrdE + dVthdE);

    // MASSIVE PARTICLE HAS H = -1/2. RHO^2 DERIVATIVE PRODUCES INWARD PULL.
    d.dpr = -0.5 * invRho2 * (dDelta * s.pr * s.pr + dVrdr) - s.r * invRho2;
    d.dptheta = -0.5 * invRho2 * (dVthdth - a * a * std::sin(2.0 * s.theta));

    return d;
}

FreeFallState step(const FreeFallState& s, const Deriv& d, double h) {
    FreeFallState out = s;
    out.r      = s.r + h * d.dr;
    out.theta  = s.theta + h * d.dtheta;
    out.phi    = s.phi + h * d.dphi;
    out.t      = s.t + h * d.dt;
    out.pr     = s.pr + h * d.dpr;
    out.ptheta = s.ptheta + h * d.dptheta;
    return out;
}

} // NAMESPACE

FreeFallState freeFallFromRest(double spin, double mass, double r, double theta) {
    const double a = spin * mass;
    const Metric m = metricAt(r, theta, a, mass);
    const double sinTheta = std::sin(theta);

    // BOYER-LINDQUIST REST: U^T FROM U.U = -1.
    const double gtt = -(1.0 - 2.0 * mass * r / m.rho2);
    const double ut = 1.0 / std::sqrt(std::max(-gtt, 1e-12));

    FreeFallState s;
    s.r = r;
    s.theta = theta;
    s.phi = 0.0;
    s.t = 0.0;
    s.pr = 0.0;
    s.ptheta = 0.0;

    // E = -P_T, L = P_PHI. STATIC OBSERVER HAS NEGATIVE L AGAINST DRAGGING.
    s.energy = -gtt * ut;
    s.angmom = -(2.0 * a * mass * r * sinTheta * sinTheta / m.rho2) * ut;
    s.properTime = 0.0;
    return s;
}

FreeFallState freeFallFromVelocity(double spin, double mass, double r, double theta,
                                   double ur, double utheta, double uphi) {
    const double a = spin * mass;
    const Metric m = metricAt(r, theta, a, mass);
    const double sinTheta = std::sin(theta);
    const double sin2 = sinTheta * sinTheta;

    const double gtt = -(1.0 - 2.0 * mass * r / m.rho2);
    const double gtphi = -2.0 * a * mass * r * sin2 / m.rho2;
    const double gphiphi = (m.sigma2 / m.rho2) * sin2;
    const double grr = m.rho2 / m.delta;
    const double gthth = m.rho2;

    // SOLVE G_UV U^U U^V = -1 FOR U^T. FUTURE-DIRECTED ROOT.
    const double A = gtt;
    const double B = 2.0 * gtphi * uphi;
    const double C = gphiphi * uphi * uphi + grr * ur * ur + gthth * utheta * utheta + 1.0;

    const double disc = std::max(B * B - 4.0 * A * C, 0.0);
    const double ut = (-B - std::sqrt(disc)) / (2.0 * A);

    FreeFallState s;
    s.r = r;
    s.theta = theta;
    s.phi = 0.0;
    s.t = 0.0;
    s.pr = grr * ur;
    s.ptheta = gthth * utheta;
    s.energy = -(gtt * ut + gtphi * uphi);
    s.angmom = gtphi * ut + gphiphi * uphi;
    s.properTime = 0.0;
    return s;
}

bool freeFallAdvance(FreeFallState& state, double spin, double mass, double dTau) {
    const double a = spin * mass;
    const double horizon = mass + std::sqrt(std::max(mass * mass - a * a, 0.0));

    // FIXED STEP RK4 SUBDIVIDED FOR SMOOTH MOTION.
    constexpr int kSubSteps = 32;
    const double h = dTau / kSubSteps;

    // HORIZON CUTOFF TESTED ON CANDIDATE STEP TO PREVENT NAN BLOWUP.
    const double floorRadius = horizon * 1.0005;

    for (int i = 0; i < kSubSteps; ++i) {
        if (state.r <= floorRadius) {
            return false;
        }

        const Deriv k1 = derivative(state, a, mass);
        const Deriv k2 = derivative(step(state, k1, 0.5 * h), a, mass);
        const Deriv k3 = derivative(step(state, k2, 0.5 * h), a, mass);
        const Deriv k4 = derivative(step(state, k3, h), a, mass);

        FreeFallState next = state;
        next.r      += h / 6.0 * (k1.dr + 2 * k2.dr + 2 * k3.dr + k4.dr);
        next.theta  += h / 6.0 * (k1.dtheta + 2 * k2.dtheta + 2 * k3.dtheta + k4.dtheta);
        next.phi    += h / 6.0 * (k1.dphi + 2 * k2.dphi + 2 * k3.dphi + k4.dphi);
        next.t      += h / 6.0 * (k1.dt + 2 * k2.dt + 2 * k3.dt + k4.dt);
        next.pr     += h / 6.0 * (k1.dpr + 2 * k2.dpr + 2 * k3.dpr + k4.dpr);
        next.ptheta += h / 6.0 * (k1.dptheta + 2 * k2.dptheta + 2 * k3.dptheta + k4.dptheta);
        next.properTime += h;

        // REJECT STEP IF NAN OR INSIDE HORIZON.
        if (!std::isfinite(next.r) || !std::isfinite(next.theta) ||
            !std::isfinite(next.pr) || !std::isfinite(next.ptheta) ||
            next.r <= floorRadius) {
            return false;
        }

        state = next;
    }

    return state.r > floorRadius;
}

FreeFallFrame freeFallFrame(const FreeFallState& s, double spin, double mass) {
    const double a = spin * mass;
    const Metric m = metricAt(s.r, s.theta, a, mass);
    const double sinTheta = std::sin(s.theta);

    const double rho = std::sqrt(m.rho2);
    const double sigma = std::sqrt(m.sigma2);
    const double sqrtDelta = std::sqrt(std::max(m.delta, 1e-12));

    const double alpha = rho * sqrtDelta / sigma;          // LAPSE.
    const double omega = 2.0 * a * mass * s.r / m.sigma2;  // FRAME DRAGGING.
    const double varpi = sigma * sinTheta / rho;           // CYL RADIUS.

    const Deriv d = derivative(s, a, mass);
    const double ut = d.dt;

    // 4-VELOCITY PROJECTED ONTO LOCAL ORTHONORMAL TETRAD.
    const double utHat = alpha * ut;
    const double urHat = (rho / sqrtDelta) * d.dr;
    const double uthHat = rho * d.dtheta;
    const double uphHat = varpi * (d.dphi - omega * ut);

    FreeFallFrame f;
    f.gamma = utHat;
    if (utHat > 1e-9) {
        f.betaR = urHat / utHat;
        f.betaTheta = uthHat / utHat;
        f.betaPhi = uphHat / utHat;
    }
    f.speed = std::sqrt(f.betaR * f.betaR + f.betaTheta * f.betaTheta +
                        f.betaPhi * f.betaPhi);
    return f;
}

} // NAMESPACE BHS

