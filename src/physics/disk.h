#pragma once

// KERR EQUATORIAL CIRCULAR ORBITS AND REDSHIFT. BARDEEN 1972, PAGE-THORNE 1974.

#include "physics/kerr.h"

// PROGRADE KEPLERIAN OMEGA D(PHI)/DT.
KERR_FN kreal disk_omega(kreal r, KerrBH bh) {
    return sqrt(bh.M) / (pow(r, kreal(1.5)) + bh.a * sqrt(bh.M));
}

// D(OMEGA)/DR SHEAR GRADIENT. DRIVES DIFFERENTIAL TWIST.
KERR_FN kreal disk_omega_gradient(kreal r, KerrBH bh) {
    const kreal denom = pow(r, kreal(1.5)) + bh.a * sqrt(bh.M);
    return kreal(-1.5) * sqrt(bh.M * r) / (denom * denom);
}

// ORBITING MATTER TIME DILATION U^T.
KERR_FN kreal disk_ut(kreal r, KerrBH bh) {
    const kreal rt = sqrt(r);
    const kreal denom = pow(r, kreal(1.5)) - 3 * bh.M * rt + 2 * bh.a * sqrt(bh.M);
    return (pow(r, kreal(1.5)) + bh.a * sqrt(bh.M)) /
           (pow(r, kreal(0.75)) * sqrt(fmax(denom, kreal(1e-9))));
}

// SPECIFIC ENERGY AND ANGULAR MOMENTUM OF CIRCULAR ORBIT.
KERR_FN kreal disk_energy(kreal r, KerrBH bh) {
    const kreal rt = sqrt(r);
    const kreal denom = pow(r, kreal(1.5)) - 3 * bh.M * rt + 2 * bh.a * sqrt(bh.M);
    return (pow(r, kreal(1.5)) - 2 * bh.M * rt + bh.a * sqrt(bh.M)) /
           (pow(r, kreal(0.75)) * sqrt(fmax(denom, kreal(1e-9))));
}

KERR_FN kreal disk_angmom(kreal r, KerrBH bh) {
    const kreal rt = sqrt(r);
    const kreal denom = pow(r, kreal(1.5)) - 3 * bh.M * rt + 2 * bh.a * sqrt(bh.M);
    return sqrt(bh.M) * (r * r - 2 * bh.a * sqrt(bh.M) * rt + bh.a * bh.a) /
           (pow(r, kreal(0.75)) * sqrt(fmax(denom, kreal(1e-9))));
}

// FREQUENCY RATIO G = 1 / (U^T * (1 - OMEGA*B)). GRAV REDSHIFT + DOPPLER BEAMING.
KERR_FN kreal disk_redshift(kreal r, kreal b, KerrBH bh) {
    const kreal denom = disk_ut(r, bh) * (1 - disk_omega(r, bh) * b);
    return 1 / copysign(fmax(fabs(denom), kreal(1e-6)), denom);
}

// PAGE-THORNE RADIATED FLUX. ZERO TORQUE AT ISCO. NUMERICALLY INTEGRATED INTO LUT.
KERR_FN kreal disk_flux_integrand(kreal r, KerrBH bh) {
    const kreal h = kreal(1e-4) * r;
    const kreal dL = (disk_angmom(r + h, bh) - disk_angmom(r - h, bh)) / (2 * h);
    const kreal E = disk_energy(r, bh);
    const kreal L = disk_angmom(r, bh);
    return (E - disk_omega(r, bh) * L) * dL;
}

KERR_FN kreal disk_flux(kreal r, kreal integral, KerrBH bh) {
    const kreal h = kreal(1e-4) * r;
    const kreal dOmega = (disk_omega(r + h, bh) - disk_omega(r - h, bh)) / (2 * h);
    const kreal E = disk_energy(r, bh);
    const kreal L = disk_angmom(r, bh);
    const kreal denom = E - disk_omega(r, bh) * L;
    return -dOmega / (4 * kreal(3.14159265358979323846) * r * fmax(denom * denom, kreal(1e-12)))
           * integral;
}
