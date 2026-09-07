#pragma once

// CAMERA WORLDLINE. TIMELIKE GEODESIC IN KERR. CPU DOUBLE PRECISION.
// MASSIVE PARTICLE HAS H = -1/2. D(RHO^2) TERMS PULL IT IN.

namespace bhs {

struct FreeFallState {
    double r     = 0.0;
    double theta = 0.0;
    double phi   = 0.0;
    double t     = 0.0;   // COORDINATE TIME.
    double pr    = 0.0;
    double ptheta = 0.0;

    // CONSERVED CONSTANTS: ENERGY E AND AXIAL ANGULAR MOMENTUM L.
    double energy = 0.0;
    double angmom = 0.0;

    double properTime = 0.0;
};

// RELEASE FROM REST AT (R, THETA). STATIC OBSERVER HAS NEGATIVE L.
FreeFallState freeFallFromRest(double spin, double mass, double r, double theta);

// GENERAL VELOCITY INITIAL CONDITIONS. SOLVES U.U = -1. THETA KICK CROSSES PLANE.
FreeFallState freeFallFromVelocity(double spin, double mass, double r, double theta,
                                   double ur, double utheta, double uphi);

// ADVANCE DTAU PROPER TIME. FALSE WHEN REACHING HORIZON.
bool freeFallAdvance(FreeFallState& state, double spin, double mass, double dTau);

// CAMERA VELOCITY IN LOCAL ORTHONORMAL FRAME FOR ABERRATION.
struct FreeFallFrame {
    double betaR = 0.0;      // ALONG E_R.
    double betaTheta = 0.0;  // ALONG E_THETA.
    double betaPhi = 0.0;    // ALONG E_PHI.
    double gamma = 1.0;
    double speed = 0.0;      // |BETA|.
};

FreeFallFrame freeFallFrame(const FreeFallState& state, double spin, double mass);

} // NAMESPACE BHS

