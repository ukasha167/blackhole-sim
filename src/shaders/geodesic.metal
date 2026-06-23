#include <metal_stdlib>
using namespace metal;

struct FrameUniforms {
    float4x4 invViewProj;
    float4   cameraPos;
    float4   diskParams;     // x=innerRadius, y=outerRadius, z=spin(a), w=unused
    float4   renderParams;   // x=width, y=height, z=time, w=mass(M)
};

constant float kStepCoefficient = 0.15;
constant float kMinStepSize     = 1.0e-4;
constant float kMaxStepSize     = 0.5;
constant int   kMaxSteps        = 512;
constant float kCaptureEpsilon  = 0.01;
constant float kEscapeRadius    = 60.0;

float hash13(float3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float3 starfield(float3 dir) {
    float n = hash13(dir * 400.0);
    float star = step(0.9975, n) * (0.6 + 0.4 * hash13(dir * 91.7));
    float3 ambient = float3(0.01, 0.012, 0.02);
    return ambient + float3(star);
}

struct ConservedQuantities { float E; float L; float Q; };
struct GeodesicDerivative { float rDot; float thetaDot; float phiDot; float tDot; };

float computeR(float r, float a, ConservedQuantities c) {
    float term = c.E * (r * r + a * a) - a * c.L;
    float Delta = r * r - 2.0 * r + a * a;
    return term * term - Delta * (c.Q + (c.L - a * c.E) * (c.L - a * c.E));
}

float computeTheta(float theta, float a, ConservedQuantities c) {
    float sinT2 = max(sin(theta) * sin(theta), 1.0e-5);
    float cosT2 = cos(theta) * cos(theta);
    return c.Q - cosT2 * (c.L * c.L / sinT2 - a * a * c.E * c.E);
}

GeodesicDerivative geodesicDerivative(float r, float theta, float a, ConservedQuantities c, float rSign, float thetaSign) {
    float Sigma = r * r + a * a * cos(theta) * cos(theta);
    float Delta = max(r * r - 2.0 * r + a * a, 1.0e-6);
    float sinT2 = max(sin(theta) * sin(theta), 1.0e-5);

    float Rr     = max(computeR(r, a, c), 0.0);
    float Theta0 = max(computeTheta(theta, a, c), 0.0);
    float bracket = c.E * (r * r + a * a) - a * c.L;

    GeodesicDerivative d;
    d.rDot     = rSign * sqrt(Rr) / Sigma;
    d.thetaDot = thetaSign * sqrt(Theta0) / Sigma;
    d.phiDot   = (-(a * c.E - c.L / sinT2) + (a / Delta) * bracket) / Sigma;
    d.tDot     = (-a * (a * c.E * sinT2 - c.L) + ((r * r + a * a) / Delta) * bracket) / Sigma;
    return d;
}

void rk4Step(thread float& r, thread float& theta, thread float& phi, thread float& t, float a, ConservedQuantities c, float rSign, float thetaSign, float h) {
    GeodesicDerivative k1 = geodesicDerivative(r, theta, a, c, rSign, thetaSign);

    float r2 = r + 0.5 * h * k1.rDot;
    float th2 = theta + 0.5 * h * k1.thetaDot;
    GeodesicDerivative k2 = geodesicDerivative(r2, th2, a, c, rSign, thetaSign);

    float r3 = r + 0.5 * h * k2.rDot;
    float th3 = theta + 0.5 * h * k2.thetaDot;
    GeodesicDerivative k3 = geodesicDerivative(r3, th3, a, c, rSign, thetaSign);

    float r4 = r + h * k3.rDot;
    float th4 = theta + h * k3.thetaDot;
    GeodesicDerivative k4 = geodesicDerivative(r4, th4, a, c, rSign, thetaSign);

    r     += (h / 6.0) * (k1.rDot     + 2.0 * k2.rDot     + 2.0 * k3.rDot     + k4.rDot);
    theta += (h / 6.0) * (k1.thetaDot + 2.0 * k2.thetaDot + 2.0 * k3.thetaDot + k4.thetaDot);
    phi   += (h / 6.0) * (k1.phiDot   + 2.0 * k2.phiDot   + 2.0 * k3.phiDot   + k4.phiDot);
    t     += (h / 6.0) * (k1.tDot     + 2.0 * k2.tDot     + 2.0 * k3.tDot     + k4.tDot);
}

struct RaySpherical { float r, theta, phi; float pr, ptheta, pphi; };

RaySpherical cartesianRayToSpherical(float3 origin, float3 dir) {
    RaySpherical s;
    s.r     = length(origin);
    s.theta = acos(clamp(origin.z / s.r, -1.0, 1.0));
    s.phi   = atan2(origin.y, origin.x);

    float sinTh = sin(s.theta), cosTh = cos(s.theta);
    float sinPh = sin(s.phi),   cosPh = cos(s.phi);

    float3 rHat     = float3(sinTh * cosPh, sinTh * sinPh, cosTh);
    float3 thetaHat = float3(cosTh * cosPh, cosTh * sinPh, -sinTh);
    float3 phiHat   = float3(-sinPh, cosPh, 0.0);

    s.pr     = dot(dir, rHat);
    s.ptheta = dot(dir, thetaHat);
    s.pphi   = dot(dir, phiHat);
    return s;
}

ConservedQuantities computeConservedQuantities(RaySpherical s, float a) {
    ConservedQuantities c;
    c.E = 1.0;
    c.L = s.pphi * s.r * sin(s.theta);

    float sinT2 = max(sin(s.theta) * sin(s.theta), 1.0e-5);
    float cosT2 = cos(s.theta) * cos(s.theta);
    float pTheta = s.r * s.ptheta;
    c.Q = pTheta * pTheta + cosT2 * (c.L * c.L / sinT2 - a * a * c.E * c.E);
    return c;
}

float3 traceGeodesic(float3 origin, float3 dir, float a, float M, float innerR, float outerR) {
    float horizonR = M + sqrt(max(M * M - a * a, 0.0));

    RaySpherical s0 = cartesianRayToSpherical(origin, dir);
    ConservedQuantities c = computeConservedQuantities(s0, a);

    float r = s0.r, theta = s0.theta, phi = s0.phi, t = 0.0;
    float rSign     = sign(s0.pr);
    float thetaSign = sign(s0.ptheta);

    // Optimized Doppler Beaming Proxy
    // Approaching side emits negative L photons, receding side emits positive L.
    float dopplerShift = 1.0 - 0.12 * c.L;
    float beaming = pow(max(dopplerShift, 0.05), 3.0);

    float3 accumColor = float3(0.0);
    float opacity = 0.0;

    for (int i = 0; i < kMaxSteps; ++i) {
        float h = clamp(kStepCoefficient * (r - horizonR), kMinStepSize, kMaxStepSize);
        float rBefore = r, thetaBefore = theta;

        rk4Step(r, theta, phi, t, a, c, rSign, thetaSign, h);

        if (computeR(r, a, c) < 0.0)         rSign = -sign(r - rBefore);
        if (computeTheta(theta, a, c) < 0.0) thetaSign = -sign(theta - thetaBefore);

        // Capture
        if (r <= horizonR + kCaptureEpsilon) {
            return accumColor;
        }

        // Volumetric Accretion Disk
        float currentCosTheta = cos(theta);
        float zApprox = r * currentCosTheta;
        float diskHalfHeight = 0.3 + 0.15 * (r - innerR);

        if (r >= innerR && r <= outerR && abs(zApprox) < diskHalfHeight && opacity < 1.0) {
            float verticalFade = 1.0 - (abs(zApprox) / diskHalfHeight);
            float radialFade = smoothstep(innerR, innerR + 1.5, r) * (1.0 - smoothstep(outerR - 2.0, outerR, r));
            float density = verticalFade * radialFade * 3.5;

            float tempParam = 1.0 - (r - innerR) / (outerR - innerR);
            float3 gasColor = mix(float3(0.8, 0.2, 0.05), float3(1.0, 0.9, 0.6), tempParam);

            float3 emission = gasColor * density * beaming * h;
            accumColor += emission * (1.0 - opacity);
            opacity += density * h * 0.4;
        }

        // Escape
        if (r >= kEscapeRadius) {
            float sinTh = sin(theta);
            float3 rHat = float3(sinTh * cos(phi), sinTh * sin(phi), cos(theta));
            float3 background = starfield(rHat);
            return accumColor + background * (1.0 - opacity);
        }
    }

    return accumColor;
}

kernel void geodesic_main(
    texture2d<float, access::write> outTexture [[texture(0)]],
    constant FrameUniforms& uniforms           [[buffer(0)]],
    uint2 gid                                  [[thread_position_in_grid]])
{
    const uint w = uint(uniforms.renderParams.x);
    const uint h = uint(uniforms.renderParams.y);
    if (gid.x >= w || gid.y >= h) return;

    float ndcX = (float(gid.x) + 0.5) / float(w) * 2.0 - 1.0;
    float ndcY = 1.0 - (float(gid.y) + 0.5) / float(h) * 2.0;

    float4 clipNear = float4(ndcX, ndcY, -1.0, 1.0);
    float4 clipFar  = float4(ndcX, ndcY,  1.0, 1.0);

    float4 worldNear = uniforms.invViewProj * clipNear;
    float4 worldFar  = uniforms.invViewProj * clipFar;
    worldNear /= worldNear.w;
    worldFar  /= worldFar.w;

    float3 rayDir = normalize(worldFar.xyz - worldNear.xyz);
    float3 rayOrigin = uniforms.cameraPos.xyz;

    float a      = uniforms.diskParams.z;
    float innerR = uniforms.diskParams.x;
    float outerR = uniforms.diskParams.y;
    float M      = uniforms.renderParams.w;

    float3 color = traceGeodesic(rayOrigin, rayDir, a, M, innerR, outerR);
    outTexture.write(float4(color, 1.0), gid);
}
