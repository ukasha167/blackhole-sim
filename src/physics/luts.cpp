#define KERR_USE_DOUBLE
#include "physics/disk.h"

#include "physics/luts.hpp"

#include <algorithm>
#include <cmath>

namespace bhs {

namespace {

// ANALYTIC CIE XYZ COLOR MATCHING (WYMAN, SLOAN, SHIRLEY 2013).

double piecewiseGaussian(double x, double alpha, double mu,
                         double sigmaLeft, double sigmaRight) {
    const double t = (x - mu) / (x < mu ? sigmaLeft : sigmaRight);
    return alpha * std::exp(-0.5 * t * t);
}

double cieX(double nm) {
    return piecewiseGaussian(nm,  1.056, 599.8, 37.9, 31.0)
         + piecewiseGaussian(nm,  0.362, 442.0, 16.0, 26.7)
         + piecewiseGaussian(nm, -0.065, 501.1, 20.4, 26.2);
}

double cieY(double nm) {
    return piecewiseGaussian(nm, 0.821, 568.8, 46.9, 40.5)
         + piecewiseGaussian(nm, 0.286, 530.9, 16.3, 31.1);
}

double cieZ(double nm) {
    return piecewiseGaussian(nm, 1.217, 437.0, 11.8, 36.0)
         + piecewiseGaussian(nm, 0.681, 459.0, 26.0, 13.8);
}

// PLANCK SPECTRAL RADIANCE PER NM.
double planck(double nm, double kelvin) {
    const double lambda = nm * 1.0e-9;
    const double h = 6.62607015e-34;
    const double c = 2.99792458e8;
    const double kB = 1.380649e-23;

    const double exponent = h * c / (lambda * kB * kelvin);
    if (exponent > 700.0) {
        return 0.0;   // OVERFLOW GUARD.
    }
    return (2.0 * h * c * c) / std::pow(lambda, 5.0) / (std::exp(exponent) - 1.0);
}

void blackbodyXYZ(double kelvin, double& X, double& Y, double& Z) {
    X = Y = Z = 0.0;
    // INTEGRATE 1 NM STEPS ACROSS 360-830 NM.
    for (double nm = 360.0; nm <= 830.0; nm += 1.0) {
        const double radiance = planck(nm, kelvin);
        X += radiance * cieX(nm);
        Y += radiance * cieY(nm);
        Z += radiance * cieZ(nm);
    }
}

} // NAMESPACE

void blackbodyChromaticity(double kelvin, double& x, double& y) {
    double X, Y, Z;
    blackbodyXYZ(kelvin, X, Y, Z);
    const double sum = X + Y + Z;
    if (sum <= 0.0) {
        x = y = 0.0;
        return;
    }
    x = X / sum;
    y = Y / sum;
}

std::vector<float> buildBlackbodyTable() {
    std::vector<float> table(kBlackbodySamples * 4, 0.0f);

    const double logMin = std::log(static_cast<double>(kBlackbodyMinK));
    const double logMax = std::log(static_cast<double>(kBlackbodyMaxK));

    for (std::uint32_t i = 0; i < kBlackbodySamples; ++i) {
        const double t = static_cast<double>(i) / (kBlackbodySamples - 1);
        const double kelvin = std::exp(logMin + t * (logMax - logMin));

        double X, Y, Z;
        blackbodyXYZ(kelvin, X, Y, Z);

        // CIE XYZ TO LINEAR SRGB CONVERSION (D65 WHITE POINT).
        double r =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
        double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
        double b =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;

        // CLAMP OUT-OF-GAMUT PRIMARIES.
        r = std::max(0.0, r);
        g = std::max(0.0, g);
        b = std::max(0.0, b);

        const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (luminance > 0.0) {
            r /= luminance;
            g /= luminance;
            b /= luminance;
        }

        table[i * 4 + 0] = static_cast<float>(r);
        table[i * 4 + 1] = static_cast<float>(g);
        table[i * 4 + 2] = static_cast<float>(b);
        table[i * 4 + 3] = 1.0f;
    }

    return table;
}

DiskProfile buildDiskProfile(float spin, float mass, float outerRadius) {
    KerrBH bh;
    bh.a = static_cast<double>(spin) * static_cast<double>(mass);
    bh.M = static_cast<double>(mass);

    DiskProfile profile;
    profile.innerRadius = static_cast<float>(kerr_isco(bh, 1.0));
    profile.outerRadius = outerRadius;
    profile.temperature.assign(kDiskProfileSamples, 0.0f);

    const double rIn = profile.innerRadius;
    const double rOut = static_cast<double>(outerRadius);
    if (rOut <= rIn) {
        return profile;
    }

    // TRAPEZOID INTEGRAL OF PAGE-THORNE FLUX OUTWARD FROM ISCO.
    constexpr int kFine = 8192;
    const double r0 = rIn * (1.0 + 1.0e-6);

    std::vector<double> flux(kFine, 0.0);
    double integral = 0.0;
    double previous = disk_flux_integrand(r0, bh);

    double peak = 0.0;
    for (int i = 0; i < kFine; ++i) {
        const double t = static_cast<double>(i) / (kFine - 1);
        const double r = r0 + t * (rOut - r0);
        if (i > 0) {
            const double step = (rOut - r0) / (kFine - 1);
            const double current = disk_flux_integrand(r, bh);
            integral += 0.5 * (previous + current) * step;
            previous = current;
        }
        flux[i] = std::max(0.0, disk_flux(r, integral, bh));
        peak = std::max(peak, flux[i]);
    }

    if (peak <= 0.0) {
        return profile;
    }

    // TEMPERATURE GOES AS FLUX^(1/4) (STEFAN-BOLTZMANN).
    double peakTemp = 0.0;
    int peakIndex = 0;
    std::vector<double> temp(kFine);
    for (int i = 0; i < kFine; ++i) {
        temp[i] = std::pow(flux[i] / peak, 0.25);
        if (temp[i] > peakTemp) {
            peakTemp = temp[i];
            peakIndex = i;
        }
    }
    profile.peakRadius = static_cast<float>(
        r0 + (static_cast<double>(peakIndex) / (kFine - 1)) * (rOut - r0));

    for (std::uint32_t i = 0; i < kDiskProfileSamples; ++i) {
        const double t = static_cast<double>(i) / (kDiskProfileSamples - 1);
        const int src = std::min(kFine - 1, static_cast<int>(t * (kFine - 1)));
        profile.temperature[i] = static_cast<float>(temp[src]);
    }

    return profile;
}

} // NAMESPACE BHS

