#include <metal_stdlib>

#include "common.h"
#include "physics/kerr.h"
#include "physics/disk.h"

using namespace metal;

// CACHED LENS MAP.
// STATIONARY CAMERA IN STATIONARY SPACETIME MEANS PIXEL SKY MAP NEVER CHANGES.
// BUILD ONCE EXPENSIVELY. RESHADE CHEAPLY EVERY FRAME.
// LENS_BUILD: RAY TRACE PER PIXEL ONCE.
// LENS_FOOTPRINT: PIXEL ANGULAR SPREAD ONCE.
// SHADE_MAIN: SAMPLE DISK AND SKY HDR EVERY FRAME.

// BUILD KERNEL

kernel void lens_build(texture2d<float, access::write>       lens      [[texture(0)]],
                       texture2d_array<float, access::write> crossings [[texture(1)]],
                       constant SceneUniforms&               uniforms  [[buffer(0)]],
                       constant DiskUniforms&                disk      [[buffer(1)]],
                       uint2                                 tid       [[thread_position_in_grid]])
{
    const float2 size = uniforms.resolution.xy;
    const uint rowOffset = uint(uniforms.integrator.z);
    const uint2 gid = uint2(tid.x, tid.y + rowOffset);

    if (gid.x >= uint(size.x) || gid.y >= uint(size.y)) {
        return;
    }

    KerrBH bh;
    bh.a = uniforms.blackHole.x;
    bh.M = uniforms.blackHole.y;

    const float escapeRadius = uniforms.blackHole.z;
    const float tolerance    = uniforms.blackHole.w;
    const int   maxSteps     = int(uniforms.integrator.x);
    const float aspect       = uniforms.integrator.y;

    float2 ndc = (float2(gid) + 0.5f) / size * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    // CAMERA LOOK DIRECTION. AT REST LOOK ALONG -E_R WITH SPIN AXIS UP.
    // IN FREE FALL, ABERRATION SHIFTS THE HOLE. RUN BOOST BACKWARD TO KEEP HOLE CENTERED.
    float3 forward = float3(-1.0f, 0.0f, 0.0f);
    if (uniforms.boost.w > 1.0001f) {
        float tR, tTh, tPh, ignored;
        kerr_aberrate(-1.0f, 0.0f, 0.0f,
                      -uniforms.boost.x, -uniforms.boost.y, -uniforms.boost.z,
                      uniforms.boost.w, &tR, &tTh, &tPh, &ignored);
        forward = float3(tR, tTh, tPh);
    }

    const float3 upHint = float3(0.0f, -1.0f, 0.0f);
    float3 right = cross(forward, upHint);
    const float rightLen = length(right);
    right = rightLen > 1e-4f ? right / rightLen : float3(0.0f, 0.0f, 1.0f);
    const float3 up = cross(right, forward);

    const float3 N = normalize(forward
                             + up    * (-ndc.y * uniforms.camera.w)
                             + right * ( ndc.x * uniforms.camera.w * aspect));

    // BOOST FROM CAMERA FRAME TO FIDO FRAME.
    float aR, aTh, aPh, doppler;
    kerr_aberrate(N.x, N.y, N.z,
                  uniforms.boost.x, uniforms.boost.y, uniforms.boost.z,
                  uniforms.boost.w, &aR, &aTh, &aPh, &doppler);

    KerrRay ray;
    KerrState state;
    float energyFactor;
    kerr_ray_from_direction(uniforms.camera.x, uniforms.camera.y,
                            aR, aTh, aPh, bh, &ray, &state, &energyFactor);

    // CONSERVED PHOTON ENERGY AT CAMERA.
    const float cameraEnergy = max(doppler * energyFactor, 1e-6f);
    // AXISYMMETRY ALLOWS BAKING AT PHI = 0. ORBITING IS FREE SKY ROTATION LATER.
    state.phi = 0.0f;

    const float rIn  = disk.radii.x;
    const float rOut = disk.radii.y;

    KerrCrossings hits;
    const KerrTrace trace = kerr_trace_full(state, bh, ray, escapeRadius, tolerance,
                                            maxSteps, rIn, rOut,
                                            disk.radii.z, uniforms.integrator.w, &hits);

    lens.write(float4(trace.dirX, trace.dirY, trace.dirZ, float(trace.outcome)), gid);

    // PACK CROSSING RECORD FOR HALF FLOAT TEXTURE.
    constexpr float kHalfPi = 1.5707963267948966f;
    constexpr float kTwoPi  = 6.283185307179586f;

    for (uint i = 0; i < BHS_MAX_CROSSINGS; ++i) {
        float4 record = float4(-1.0f, 0.0f, 0.0f, 0.0f);

        if (int(i) < hits.count) {
            const float r = hits.hit[i].r;

            // ORBITAL REDSHIFT DIVIDED BY CAMERA ENERGY. BLUE BEAMS ON FALL.
            const float g = disk_redshift(r, ray.b, bh) / cameraEnergy;

            // CO-ROTATING EMISSION PHASE: PHI + OMEGA * T_TRAVEL.
            // DELAYS RETARDED TIME FOR LENSED ECHOES.
            const float omega = disk_omega(r, bh);
            const float phase = hits.hit[i].phi + omega * hits.hit[i].t;

            // AFFINE TO PROPER LENGTH VIA LOCAL PHOTON ENERGY.
            const KerrTetrad tet = kerr_tetrad(r, kHalfPi, bh);
            const float localEnergy = (1.0f - tet.omega * ray.b) / max(tet.alpha, 1e-6f);

            // CONVERT GAUSSIAN COLUMN DENSITY TO OPTICAL DEPTH WITH SMOOTH KNEE.
            constexpr float kInvRoot2Pi = 0.3989422804014327f;
            const float raw = hits.hit[i].column * localEnergy * kInvRoot2Pi;
            const float column = 8.0f * raw / (8.0f + raw);

            record = float4((r - rIn) / max(rOut - rIn, 1e-6f),
                            fract(phase / kTwoPi + 1.0f),
                            g,
                            column);
        }

        crossings.write(record, gid, i);
    }
}

// FOOTPRINT KERNEL.
// DIFFERENCE NEIGHBOUR RAYS TO ESTIMATE PIXEL SKY AND DISK FOOTPRINT.
// PREVENTS STAR BOILING AND DISK NOISE ALIASING.
kernel void lens_footprint(texture2d<float, access::read>        lens      [[texture(0)]],
                           texture2d_array<float, access::write> jacobian  [[texture(1)]],
                           texture2d_array<float, access::read>  crossings [[texture(2)]],
                           texture2d_array<float, access::write> sweep     [[texture(3)]],
                           constant SceneUniforms&               uniforms  [[buffer(0)]],
                           constant DiskUniforms&                disk      [[buffer(1)]],
                           uint2                                 gid       [[thread_position_in_grid]])
{
    const uint2 size = uint2(uniforms.resolution.xy);
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    const float span = max(disk.radii.y - disk.radii.x, 1e-6f);

    for (uint i = 0; i < BHS_MAX_CROSSINGS; ++i) {
        const float4 centre = crossings.read(gid, i);
        float2 rate = float2(0.0f);

        if (centre.x >= 0.0f) {
            // MAXIMUM RADIAL AND PHASE STEP OVER NEIGHBOURS.
            float2 widest = float2(0.0f);
            bool found = false;

            for (int axis = 0; axis < 2; ++axis) {
                const int2 unit = (axis == 0) ? int2(1, 0) : int2(0, 1);
                for (int side = 0; side < 2; ++side) {
                    const int2 p = int2(gid) + (side == 0 ? unit : -unit);
                    if (p.x < 0 || p.y < 0 || p.x >= int(size.x) || p.y >= int(size.y)) {
                        continue;
                    }
                    const float4 n = crossings.read(uint2(p), i);
                    if (n.x < 0.0f) {
                        continue;
                    }
                    // UNWRAP PERIODIC PHASE DIFFERENCE.
                    const float dPhase = fabs(fract(n.y - centre.y + 1.5f) - 0.5f);
                    widest = max(widest, float2(fabs(n.x - centre.x) * span,
                                                dPhase * 6.283185307179586f));
                    found = true;
                }
            }

            // ISOLATED HIT. MARK AS FULL SPAN TO PREVENT SPECKS.
            rate = found ? widest : float2(span, 3.14159265f);
        }

        sweep.write(float4(rate, 0.0f, 0.0f), gid, i);
    }

    const float4 centre = lens.read(gid);
    if (centre.w != float(KERR_ESCAPED)) {
        jacobian.write(float4(0.0f), gid, 0);
        jacobian.write(float4(0.0f), gid, 1);
        return;
    }

    const float3 dir = centre.xyz;

    // CENTRAL DIFFERENCES FOR ESCAPED RAYS. ONE-SIDED NEAR SHADOW BOUNDARY.
    float3 columns[2] = {float3(0.0f), float3(0.0f)};

    for (int axis = 0; axis < 2; ++axis) {
        const int2 step = (axis == 0) ? int2(1, 0) : int2(0, 1);

        float3 forward = dir;
        float3 backward = dir;
        bool haveForward = false;
        bool haveBackward = false;

        const int2 pf = int2(gid) + step;
        if (pf.x >= 0 && pf.y >= 0 && pf.x < int(size.x) && pf.y < int(size.y)) {
            const float4 n = lens.read(uint2(pf));
            if (n.w == float(KERR_ESCAPED)) { forward = n.xyz; haveForward = true; }
        }
        const int2 pb = int2(gid) - step;
        if (pb.x >= 0 && pb.y >= 0 && pb.x < int(size.x) && pb.y < int(size.y)) {
            const float4 n = lens.read(uint2(pb));
            if (n.w == float(KERR_ESCAPED)) { backward = n.xyz; haveBackward = true; }
        }

        if (haveForward && haveBackward) {
            columns[axis] = (forward - backward) * 0.5f;
        } else if (haveForward) {
            columns[axis] = forward - dir;
        } else if (haveBackward) {
            columns[axis] = dir - backward;
        }
    }

    jacobian.write(float4(columns[0], 0.0f), gid, 0);
    jacobian.write(float4(columns[1], 0.0f), gid, 1);
}

// SHADING PASS

// ANALYTIC STAR FIELD.
// POINT SOURCES FILTERED BY FOOTPRINT ELLIPSE.
// STARS BRIGHTEN AND FORM ARCS UNDER LENSING MAGNIFICATION WITHOUT ALIASING.

static uint hash_u32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// SINGLE-ROUND HASH FOR SPATIAL GRID CELLS.
static uint hash_cell(int face, int cx, int cy, uint salt) {
    return hash_u32((uint(face) + salt) * 0x9e3779b9u ^
                    uint(cx) * 73856093u ^
                    uint(cy) * 19349663u);
}

static float unit_from(uint h) {
    return float(h & 0x00ffffffu) / float(0x01000000u);
}

// MAP 3D DIRECTION TO CUBE FACE AND UV.
static void dir_to_face(float3 d, thread int& face, thread float2& uv) {
    const float3 a = fabs(d);
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0.0f ? 0 : 1;
        uv = float2(d.y, d.z) / a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0.0f ? 2 : 3;
        uv = float2(d.x, d.z) / a.y;
    } else {
        face = d.z > 0.0f ? 4 : 5;
        uv = float2(d.x, d.y) / a.z;
    }
}

static float3 face_to_dir(int face, float2 uv) {
    switch (face) {
        case 0:  return normalize(float3( 1.0f, uv.x, uv.y));
        case 1:  return normalize(float3(-1.0f, uv.x, uv.y));
        case 2:  return normalize(float3(uv.x,  1.0f, uv.y));
        case 3:  return normalize(float3(uv.x, -1.0f, uv.y));
        case 4:  return normalize(float3(uv.x, uv.y,  1.0f));
        default: return normalize(float3(uv.x, uv.y, -1.0f));
    }
}

// BLACKBODY STAR COLOR NORMALIZED TO UNIT LUMINANCE.
static float3 star_color(float t) {
    const float3 cool = float3(1.00f, 0.55f, 0.28f);   // ~3000 K
    const float3 mid  = float3(1.00f, 0.94f, 0.86f);   // ~6000 K
    const float3 hot  = float3(0.72f, 0.80f, 1.00f);   // ~15000 K
    float3 c = (t < 0.5f) ? mix(cool, mid, t * 2.0f) : mix(mid, hot, (t - 0.5f) * 2.0f);
    return c / dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

static float hash_point(int3 c) {
    uint h = hash_u32(uint(c.x * 73856093));
    h = hash_u32(h ^ uint(c.y * 19349663));
    h = hash_u32(h ^ uint(c.z * 83492791));
    return unit_from(h);
}

// TRILINEAR VALUE NOISE.
static float value_noise(float3 p) {
    const float3 base = floor(p);
    const float3 f = p - base;
    const float3 w = f * f * (3.0f - 2.0f * f);
    const int3 i = int3(base);

    float sum = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const int3 o = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        const float3 t = mix(1.0f - w, w, float3(o));
        sum += hash_point(i + o) * t.x * t.y * t.z;
    }
    return sum;
}

// FRACTAL BROWNIAN MOTION FOR BACKGROUND CLOUDS.
static float sky_fbm(float3 p, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += value_noise(p) * amp;
        total += amp;
        amp *= 0.5f;
        p = p * 2.17f + 19.0f;
    }
    return sum / total;
}

// PROCEDURAL MILKY WAY BACKGROUND.
// THIN DISK, CENTRAL BULGE, AND DUST EXTINCTION LANE.
static float3 milky_way(float3 dir, float4 sky, float4 galaxy) {
    const float tilt = sky.x * 0.017453292f;
    const float yaw  = sky.y * 0.017453292f;

    // GALACTIC COORDINATE FRAME.
    const float3 pole = float3(sin(tilt) * cos(yaw), sin(tilt) * sin(yaw), cos(tilt));
    const float3 seed = (fabs(pole.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f)
                                              : float3(1.0f, 0.0f, 0.0f);
    const float3 e1 = normalize(cross(seed, pole));
    const float3 e2 = cross(pole, e1);
    const float core = sky.z * 0.017453292f;
    const float3 gx = e1 * cos(core) + e2 * sin(core);
    const float3 gy = cross(pole, gx);

    const float sinB = dot(dir, pole);                       // SINE OF LATITUDE
    const float2 inPlane = float2(dot(dir, gx), dot(dir, gy));
    const float toCore = acos(clamp(dot(dir, gx), -1.0f, 1.0f));
    const float longitude = atan2(inPlane.y, inPlane.x);

    // DISK TAPER.
    const float width = max(galaxy.x, 0.5f) * 0.017453292f;
    const float taper = width * (1.0f + 1.9f * smoothstep(0.0f, 2.4f, toCore));
    const float lat = fabs(sinB) / taper;

    // THIN DISK AND DIFFUSE HALO.
    const float disc = exp(-lat * lat * 0.9f);
    const float halo = exp(-fabs(sinB) / (taper * 4.0f));

    // CENTRAL BULGE.
    const float bulgeSize = max(galaxy.y, 0.05f);
    const float bulgeR = sqrt(toCore * toCore + sinB * sinB * 6.0f) / bulgeSize;
    const float bulge = exp(-bulgeR * bulgeR);

    // STAR CLOUDS EMBEDDED PERIODICALLY IN LONGITUDE.
    const float cl = cos(longitude), sl = sin(longitude);
    const float3 band3 = float3(cl * 17.0f, sl * 17.0f, sinB * 44.0f);
    const float clouds = sky_fbm(band3 + 3.0f, 5);
    const float fine   = sky_fbm(band3 * 3.1f + 51.0f, 3);
    const float contrast = clamp(galaxy.z, 0.0f, 2.0f);
    const float lumpy = mix(1.0f, 0.20f + 2.0f * clouds * clouds * (0.55f + 0.9f * fine),
                            contrast);

    // DUST EXTINCTION LANE.
    const float3 dust3 = float3(cl * 11.0f, sl * 11.0f, sinB * 70.0f) + 7.0f;
    const float ragged = sky_fbm(dust3, 4);
    const float offset = (sinB - 0.20f * taper) / (taper * 0.62f);
    const float lane = exp(-offset * offset);
    const float tau = max(sky.w, 0.0f) * lane * (0.30f + 1.5f * ragged * ragged) *
                      (0.30f + 1.0f * exp(-toCore * toCore * 0.55f));
    const float3 extinction = exp(-tau * float3(1.0f, 1.55f, 2.30f));

    // DISK AND BULGE COLORS.
    const float3 discColor  = float3(0.66f, 0.74f, 1.00f);
    const float3 bulgeColor = float3(1.00f, 0.80f, 0.52f);

    // ACCUMULATE LIGHT COMPONENTS.
    float3 light = discColor * disc * lumpy * 0.085f;
    light += discColor * halo * 0.0045f;
    light += bulgeColor * bulge * 0.115f;
    light += bulgeColor * disc * lumpy * exp(-toCore * toCore * 0.9f) * 0.075f;

    return light * extinction;
}

// BAKE MILKY WAY ONCE INTO CUBEMAP. SAVES FRAME TIME.
// USES STANDARD CUBE FACE MAPPING FOR SEAMLESS HARDWARE SAMPLING.
static float3 cube_face_to_dir(uint face, float2 uv) {
    switch (face) {
        case 0:  return normalize(float3( 1.0f,   -uv.y, -uv.x));  // +X
        case 1:  return normalize(float3(-1.0f,   -uv.y,  uv.x));  // -X
        case 2:  return normalize(float3( uv.x,    1.0f,  uv.y));  // +Y
        case 3:  return normalize(float3( uv.x,   -1.0f, -uv.y));  // -Y
        case 4:  return normalize(float3( uv.x,   -uv.y,  1.0f));  // +Z
        default: return normalize(float3(-uv.x,   -uv.y, -1.0f));  // -Z
    }
}

kernel void milkyway_bake(texture2d_array<float, access::write> faces [[texture(0)]],
                          constant DiskUniforms&                disk  [[buffer(0)]],
                          uint3                                 gid   [[thread_position_in_grid]])
{
    const uint size = faces.get_width();
    if (gid.x >= size || gid.y >= size) {
        return;
    }

    const float2 uv = ((float2(gid.xy) + 0.5f) / float(size) - 0.5f) * 2.0f;
    const float3 dir = cube_face_to_dir(gid.z, uv);

    faces.write(float4(milky_way(dir, disk.sky, disk.galaxy), 1.0f), gid.xy, gid.z);
}

// ANISOTROPIC POINT SOURCE FILTERING.
// USES SCREEN JACOBIAN COLUMNS TX AND TY FOR ELLIPTICAL FOOTPRINT.
static float3 star_sky(float3 dir, float3 tx, float3 ty,
                       texturecube<float> milkyCube, float skyGlow) {
    constexpr sampler cubeSampler(filter::linear, mip_filter::linear,
                                  address::clamp_to_edge);

    constexpr int   kCells       = 512;
    constexpr float kDensity     = 0.050f;
    constexpr float kCellRadians = 1.5707963f / float(kCells);
    constexpr float kFilterPx    = 0.72f;   // PSF WIDTH IN PIXELS

    // PIXEL SKY METRIC G = J^T J.
    const float gxx = dot(tx, tx);
    const float gxy = dot(tx, ty);
    const float gyy = dot(ty, ty);
    // DET G IS SOLID ANGLE PER PIXEL.
    const float detG = max(gxx * gyy - gxy * gxy, 1e-18f);

    // FILTER BACKGROUND CUBEMAP WITH MIP LEVEL FROM DET G.
    const float texelAngle = 1.5707963f / float(max(milkyCube.get_width(), 1u));
    const float lod = max(0.5f * log2(detG / (texelAngle * texelAngle)), 0.0f);
    const float3 background = milkyCube.sample(cubeSampler, dir, level(lod)).rgb;

    if (gxx < 1e-24f || gyy < 1e-24f) {
        return background;
    }
    const float invDet = 1.0f / detG;   // INV DET SQUARED FOR G^-2 METRIC

    // BOUND SEARCH RADIUS BY MAX EIGENVALUE.
    const float trace = gxx + gyy;
    const float disc = sqrt(max(trace * trace - 4.0f * detG, 0.0f));
    const float reach = kFilterPx * 2.5f * sqrt(max(0.5f * (trace + disc), 0.0f));
    // CAP CELL SPAN TO BOUND LOOP COST.
    const int span = clamp(int(ceil(reach / kCellRadians)), 1, 2);

    int face;
    float2 uv;
    dir_to_face(dir, face, uv);
    const float2 grid = (uv * 0.5f + 0.5f) * float(kCells);
    const int2 base = int2(floor(grid));

    const float norm = invDet == 0.0f ? 0.0f
                     : 1.0f / (6.2831853f * kFilterPx * kFilterPx * sqrt(detG));

    float3 total = float3(0.0f);

    for (int dy = -span; dy <= span; ++dy) {
        for (int dx = -span; dx <= span; ++dx) {
            const int cx = base.x + dx;
            const int cy = base.y + dy;
            if (cx < 0 || cy < 0 || cx >= kCells || cy >= kCells) {
                continue;
            }

            const uint h = hash_cell(face, cx, cy, 0x2bu);
            if (unit_from(h) > kDensity) {
                continue;
            }

            const uint h1 = hash_u32(h ^ 0x68bc21ebu);
            const uint h2 = hash_u32(h1 ^ 0x02e5be93u);
            const uint h3 = hash_u32(h2 ^ 0x7feb352du);

            const float2 jitter = float2(unit_from(h1), unit_from(h2));
            const float2 starUv = ((float2(cx, cy) + jitter) / float(kCells) - 0.5f) * 2.0f;
            const float3 delta = face_to_dir(face, starUv) - dir;

            // MAHALANOBIS PIXEL DISTANCE D^T G^-2 D.
            const float a = dot(tx, delta);
            const float b = dot(ty, delta);
            const float u = gyy * a - gxy * b;
            const float v = gxx * b - gxy * a;
            const float pixels2 = (u * u + v * v) * invDet * invDet;

            // FLUX WEIGHTED BY GAUSSIAN PSF.
            const float m = unit_from(h3);
            const float flux = 7.0e-7f * pow(m, 3.5f) + 9.0e-10f;

            total += star_color(unit_from(hash_u32(h3 ^ 0x9e3779b9u))) *
                     (flux * norm * exp(-0.5f * pixels2 / (kFilterPx * kFilterPx)));
        }
    }

    return total + background * skyGlow;
}

// DISK EMISSION.
// REDSHIFTED BLACKBODY RADIATES AT G*T WITH FLUX (G*T)^4.
// ANISOTROPIC DISK TURBULENCE AND FAST PERLIN GRADIENT NOISE.

static float grad_dot(int3 c, float3 d) {
    const uint h = hash_u32(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^
                            uint(c.z) * 83492791u) & 15u;
    const float u = (h < 8u) ? d.x : d.y;
    const float v = (h < 4u) ? d.y : ((h == 12u || h == 14u) ? d.x : d.z);
    return ((h & 1u) == 0u ? u : -u) + ((h & 2u) == 0u ? v : -v);
}

static float gradient_noise(float3 p) {
    const float3 base = floor(p);
    const float3 f = p - base;
    // QUINTIC SMOOTHING.
    const float3 w = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const int3 i = int3(base);

    float sum = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const int3 o = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        const float3 t = mix(1.0f - w, w, float3(o));
        sum += grad_dot(i + o, f - float3(o)) * t.x * t.y * t.z;
    }
    return clamp(sum * 1.1f + 0.5f, 0.0f, 1.0f);
}

// RIDGED FBM WITH NYQUIST CUTOFF AND ROTATED OCTAVES.
static float disk_fbm(float3 q, int octaves, float ridge, float step) {
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        const float weight = 1.0f - smoothstep(0.20f, 0.55f, step);
        if (weight <= 0.002f) {
            break;
        }
        const float n = gradient_noise(q);
        const float ridged = 1.0f - fabs(2.0f * n - 1.0f);
        sum += mix(n, ridged * ridged, ridge) * amp * weight;
        total += amp * weight;

        amp *= 0.52f;
        step *= 2.03f;
        q = float3(q.x * 0.80f - q.z * 0.60f,
                   q.y,
                   q.x * 0.60f + q.z * 0.80f);
        q = float3(q.x,
                   q.y * 0.87f - q.z * 0.49f,
                   q.y * 0.49f + q.z * 0.87f) * 2.03f + 3.1f;
    }

    // PAST NYQUIST RETURN MEAN VALUE.
    return total > 1e-4f ? sum / total : 0.5f;
}

// DISK TURBULENCE WITH LOW FREQUENCY DOMAIN WARP.
static float disk_turbulence(float radius, float phase, float radialScale,
                             float azimuthScale, float ridge, float drift,
                             float warp, int octaves, float step) {
    float3 q = float3(cos(phase) * azimuthScale,
                      sin(phase) * azimuthScale,
                      radius * radialScale + drift);

    if (warp > 0.0f) {
        const float3 wp = q * 0.6f;
        const float w1 = gradient_noise(wp + 11.0f);
        const float w2 = gradient_noise(wp + 41.0f);
        q.xy += (w1 - 0.5f) * warp;
        q.z  += (w2 - 0.5f) * warp * 1.6f;
    }

    return disk_fbm(q, octaves, ridge, step);
}

kernel void shade_main(texture2d<float, access::read>       lens        [[texture(0)]],
                       texture2d_array<float, access::read> jacobian    [[texture(1)]],
                       texture2d_array<float, access::read> crossings   [[texture(2)]],
                       texture1d<float>                     profileLut  [[texture(3)]],
                       texture1d<float>                     blackbodyLut[[texture(4)]],
                       texturecube<float>                   milkyCube   [[texture(6)]],
                       texture2d_array<float, access::read> sweep       [[texture(7)]],
                       texture2d<float, access::write>      out         [[texture(5)]],
                       constant SceneUniforms&              uniforms    [[buffer(0)]],
                       constant DiskUniforms&               disk        [[buffer(1)]],
                       uint2                                gid         [[thread_position_in_grid]])
{
    const uint2 size = uint2(uniforms.resolution.xy);
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    constexpr sampler lutSampler(filter::linear, address::clamp_to_edge);

    const float4 entry = lens.read(gid);
    const int outcome = int(entry.w + 0.5f);

    KerrBH bh;
    bh.a = uniforms.blackHole.x;
    bh.M = uniforms.blackHole.y;

    // CAMERA AZIMUTH AND OBSERVER TIME.
    const float phiCam = uniforms.camera.z;
    const float observerTime = disk.emission.w * disk.radii.w;

    float3 color = float3(0.0f);

    // COMPOSITE PASSAGES FRONT TO BACK.
    // REFERENCE PATTERN ROTATION SPEED.
    const float omegaRef = disk_omega(mix(disk.radii.x, disk.radii.y, 0.35f), bh);
    // SATURATION LIMIT FOR SHEAR WIND-UP.
    constexpr float kTwistLimit = 10.0f;

    const float peakTemperature = disk.emission.x;
    const float brightness      = disk.emission.y;
    const float opacity         = disk.emission.z;
    const float time            = disk.emission.w;

    float transmittance = 1.0f;
    float2 probe[BHS_MAX_CROSSINGS] = {float2(0.0f), float2(0.0f), float2(0.0f)};
    float3 field = float3(0.0f);   // DIAGNOSTIC FIELD SAMPLES

    for (uint i = 0; i < BHS_MAX_CROSSINGS; ++i) {
        const float4 record = crossings.read(gid, i);
        if (record.x < 0.0f || transmittance < 0.004f) {
            continue;
        }

        const float rNorm = record.x;
        const float g     = record.z;
        const float column = record.w;

        const float radius = mix(disk.radii.x, disk.radii.y, rNorm);

        // SATURATE DIFFERENTIAL SHEAR TWIST TO PREVENT INFINITE WINDUP.
        const float twist = (disk_omega(radius, bh) - omegaRef) * observerTime;
        const float phase = record.y * 6.283185307179586f
                          + phiCam
                          - omegaRef * observerTime
                          - kTwistLimit * tanh(twist / kTwistLimit);

        // PAGE-THORNE TEMPERATURE PROFILE.
        const float emittedT = profileLut.sample(lutSampler, rNorm).r * peakTemperature;
        const float observedT = max(g * emittedT, 1.0f);

        // LOGARITHMIC BLACKBODY LUT COORDINATE.
        const float lutCoord = clamp((log(observedT) - log(700.0f)) /
                                     (log(40000.0f) - log(700.0f)), 0.0f, 1.0f);
        const float3 chroma = blackbodyLut.sample(lutSampler, lutCoord).rgb;

        // BOLOMETRIC BEAMING (G*T)^4.
        const float ratio = observedT / max(peakTemperature, 1.0f);
        float flux = ratio * ratio * ratio * ratio;

        // RADIAL BOUNDARIES.
        const float inner = smoothstep(0.0f, 0.025f, rNorm);
        const float outer = 1.0f - smoothstep(0.72f, 1.0f, rNorm);

        // INNER PLUNGE LIP.
        const float lip = 1.0f + disk.detail.w * exp(-rNorm * 26.0f);

        // TURBULENCE SPATIAL SCALES.
        const float radialScale  = disk.noise.x;
        const float azimuthScale = disk.detail.x;

        // ESTIMATE SAMPLING STEP IN NOISE DOMAIN. ADD SHEAR GRADIENT TO PHASE STEP.
        const float2 rate = sweep.read(gid, i).xy;
        const float relax = 1.0f / cosh(twist / kTwistLimit);
        const float shear = fabs(disk_omega_gradient(radius, bh) * observerTime) *
                            relax * relax;
        const float step = length(float2(rate.x * radialScale,
                                         (rate.y + shear * rate.x) * azimuthScale));

        // GRAZING AND NYQUIST RESOLUTION FADE.
        const float graze   = smoothstep(0.05f, 0.26f, 1.0f / max(column, 1e-3f));
        const float resolve = graze * (1.0f - smoothstep(0.30f, 1.10f, step));
        probe[i] = float2(step, resolve);

        const float drift = time * disk.noise.z + radius * disk.flow.z;
        const float turb = disk_turbulence(radius, phase, radialScale, azimuthScale,
                                           disk.detail.y, drift,
                                           disk.flow.x * resolve, 4, step);

        // SHARPEN TURBULENCE AND FADE TO RESOLVED MEAN.
        const float sharpened = smoothstep(disk.flow.y, 1.0f, turb);
        const float shaped = mix(0.5f * (1.0f - disk.flow.y), sharpened, resolve);

        // SECONDARY CLUMPY HOTSPOTS.
        float hotspot = 0.0f;
        if (disk.detail.z > 0.0f && resolve > 0.02f) {
            const float3 hp = float3(cos(phase) * disk.detail.x * 2.7f,
                                     sin(phase) * disk.detail.x * 2.7f,
                                     radius * disk.noise.x * 3.1f + time * disk.noise.z * 1.7f);
            hotspot = smoothstep(0.60f, 0.90f, gradient_noise(hp)) *
                      disk.detail.z * resolve;
        }

        const float density = mix(1.0f, shaped * 2.4f, disk.noise.y) * inner * outer
                            + hotspot * inner * outer;
        if (i == 0u) { field = float3(turb, shaped, density * 0.5f); }

        flux *= density * column * brightness * lip;

        color += chroma * flux * transmittance;
        transmittance *= exp(-opacity * density * column);
    }

    if (outcome == KERR_ESCAPED) {
        // ESCAPED RAYS: ADD BACKGROUND SKY.
        if (transmittance > 0.004f) {
            // ROTATE ASYMPTOTIC DIRECTION BY CAMERA PHI.
            const float cs = cos(phiCam);
            const float sn = sin(phiCam);
            const float3 raw = entry.xyz;
            const float3 tx0 = jacobian.read(gid, 0).xyz;
            const float3 ty0 = jacobian.read(gid, 1).xyz;

            // ROTATE JACOBIAN WITH SKY DIRECTION.
            const float3 dir = float3(raw.x * cs - raw.y * sn, raw.x * sn + raw.y * cs, raw.z);
            const float3 tx  = float3(tx0.x * cs - tx0.y * sn, tx0.x * sn + tx0.y * cs, tx0.z);
            const float3 ty  = float3(ty0.x * cs - ty0.y * sn, ty0.x * sn + ty0.y * cs, ty0.z);

            color += star_sky(dir, tx, ty, milkyCube, disk.flow.w) * transmittance;
        }
    } else if (outcome != KERR_CAPTURED && int(disk.noise.w) == 1) {
        // DIAGNOSTIC COLOR FOR EXHAUSTED RAYS.
        color += float3(1.0f, 0.0f, 1.0f);
    }

    // INTERMEDIATE DIAGNOSTIC VIEWS.
    const int diag = int(disk.noise.w);
    if (diag >= 2) {
        const float4 r0 = crossings.read(gid, 0);
        const float4 r1 = crossings.read(gid, 1);
        const float2 rate = sweep.read(gid, 0).xy;
        const float span = max(disk.radii.y - disk.radii.x, 1e-6f);

        if (diag == 2) {          // RECORDED SLOTS
            color = float3(r0.x >= 0.0f ? 1.0f : 0.0f,
                           r1.x >= 0.0f ? 1.0f : 0.0f,
                           crossings.read(gid, 2).x >= 0.0f ? 1.0f : 0.0f);
        } else if (diag == 3) {   // NORMALIZED CROSSING RADIUS
            color = r0.x >= 0.0f ? float3(r0.x) : float3(0.0f, 0.0f, 0.15f);
        } else if (diag == 4) {   // COLUMN DENSITY
            color = r0.x >= 0.0f ? float3(r0.w * 0.15f) : float3(0.0f, 0.0f, 0.15f);
        } else if (diag == 5) {   // REDSHIFT FACTOR
            color = r0.x >= 0.0f ? float3(r0.z * 0.5f) : float3(0.0f, 0.0f, 0.15f);
        } else if (diag == 6) {   // PIXEL DISK SWEEP
            color = float3(rate.x / span * 4.0f, rate.y * 1.2f, 0.0f);
        } else if (diag == 7) {   // ORBITAL PHASE
            color = r0.x >= 0.0f ? float3(r0.y) : float3(0.0f, 0.0f, 0.15f);
        } else if (diag <= 10) {  // NYQUIST SAMPLING RATE
            const uint slot = min(uint(diag) - 8u, uint(BHS_MAX_CROSSINGS - 1));
            color = float3(probe[slot].x * 4.0f, probe[slot].y, 0.0f);
        } else if (diag == 11) {  // RAW TURBULENCE
            color = float3(field.x);
        } else if (diag == 12) {  // SHAPED DENSITY
            color = float3(field.z);
        }
    }

    out.write(float4(color, 1.0f), gid);
}
