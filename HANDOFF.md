# Gargantua renderer: state of the project

Written 2026-09-02. Read this first in a new session, then `src/physics/kerr.h` and
`src/shaders/lens.metal`.

## What this is

A real-time Kerr black hole renderer for an M1 Air, built as a gift. The target is
Interstellar's Gargantua but pushed the other way: where Nolan and Double Negative
deliberately softened the physics for a cinema audience, this turns those same knobs up.
The window title is `"Still Second to HER EYES."` and it stays exactly that.

The end goal is falling through the accretion disk into the horizon and watching spacetime
warp on the way in. That works today. What remains is polish, not architecture.

## Where we are

Everything through M4 of the plan is done and running. `cmake --build build -j` succeeds
from clean, `./build/blackhole --validate` reports **55 checks, 0 failures**, and the app
holds **55 fps at 2560x1600** on the M1.

Current timings, 300 frames:

```
gpu span      18.15 ms   shade 15.33 | bloom 1.58 | composite 1.15
```

That is 55 fps, down from 62, and the reason is framing rather than shading: the camera
came in from 40 M to 34 M and the lens narrowed from 46 to 40 degrees, so the disk covers
many more pixels and more of them run the full disk path. The shade pass is 85 percent of
the frame and is still the only thing worth optimising.

## The one architectural idea

For a fixed camera in a stationary spacetime, the map from pixel to sky direction does not
depend on time. So it gets solved once, expensively, and re-shaded every frame for almost
nothing.

```
   once per camera change              every frame
   lens build (compute)          ->    shade (compute)      -> bloom -> composite
   DOPRI5 Kerr geodesics               sky lookup, disk
   progressive bands                   emission, redshift
```

Everything follows from that split. Accuracy lives in the build pass because it is
amortised; speed lives in the shade pass because it runs 60 times a second. When the
camera falls, the same buffer is rebuilt per frame at reduced resolution instead of cached,
so the code path survives the transition unchanged.

Camera azimuth is free: the spacetime is axisymmetric, so orbiting the camera is a rotation
of the cached direction about the spin axis and costs zero rays.

## Physics actually implemented

Boyer-Lindquist coordinates, geometric units, G = c = 1, M as the length unit.

- **FIDO/ZAMO orthonormal tetrad** at the camera, so it works inside the ergosphere. The
  camera is a boost of that frame, which is why the static camera and the falling camera
  share one code path.
- **Hamiltonian integration** with `p_r` and `p_theta` as state variables. The old code
  integrated `sqrt(R)` with a sign flag and rewound at turning points, which put an O(h)
  error into exactly the photon-ring rays. Momentum passes smoothly through zero and
  nothing special happens.
- **Constraint projection** after each accepted step: re-derive magnitudes from the
  separated potentials R(r) and Theta(theta), keep the integrator's signs. Kills secular
  drift, buys larger steps.
- **Dormand-Prince 5(4)** with FSAL, plus cubic Hermite dense output to land the disk
  crossing on theta = pi/2 exactly.
- **Escape direction from the momentum**, not the position. The original code used the
  ray's position on the escape sphere, which displaced every star by up to 9 degrees.
- **Disk as a volume.** The gas has a Gaussian vertical profile of half-height `H = h*r`,
  and what a ray picks up is the integral of that profile along its path, not a sample at
  one point. Each unbroken passage through the gas becomes one record holding the
  density-weighted mean radius, azimuth and emission time, plus the integrated column. The
  integrator caps its step inside the slab so the quadrature can see what it is
  integrating, and the passage closes only when the ray has gone somewhere in between, in
  radius or azimuth, rather than the first time a step stops contributing.
- **Disk emission**: Page-Thorne flux with a zero-torque ISCO boundary,
  `g = 1/(u^t (1 - Omega b))`, `g^4` bolometric transport. A redshifted blackbody is still
  a blackbody, so one LUT lookup at `g*T` gives observed colour and flux together.
- **Nyquist-aware turbulence.** The footprint pass also measures how far the crossing
  radius and phase move for one pixel step, and the shading fades each noise octave out as
  it drops under Nyquist. The differential rotation adds to that measure and grows with the
  clock, so it is included analytically.
- **Bounded wind-up.** Turbulence frozen into the gas shears without limit, and after a
  minute the pattern is finer than a pixel. Real turbulence re-forms about as fast as the
  shear tears it, so the differential twist saturates: the pattern keeps turning, each
  radius winds against the reference rate by a few turns, and then it holds.
- **The galaxy** is a thin disc with a taper, a bulge, sheared star clouds and a reddening
  dust lane, baked once into a mip-mapped cube and sampled at the LOD the pixel footprint
  asks for.
- **Anisotropic star filtering** from the Jacobian of the pixel-to-sky map. The squared
  pixel distance is `d^T G^-2 d`, not the Mahalanobis `d^T G^-1 d`. Getting that wrong
  makes every star fill its cell as a parallelogram.
- **Timelike free fall** in double precision on the CPU, with relativistic aberration
  applied to the ray directions. Note that `H = -1/2` for a massive particle, so the
  `d(rho^2)` terms that vanish for photons do not vanish here, and they are what makes the
  camera fall. The camera is released from wherever it already is, so pressing `F` pitches
  rather than teleports, and it is aimed through the inverse boost so the hole stays in
  frame as aberration builds.

## Layout

```
src/physics/kerr.h        shared MSL/C++ header, precision-parameterised (622 lines)
src/physics/disk.h        Keplerian orbit quantities, redshift, Page-Thorne flux
src/physics/luts.cpp      blackbody -> linear sRGB, disk temperature profile
src/physics/freefall.cpp  timelike camera geodesic, RK4, double precision
src/physics/tonemap.h     AgX, including the trailing EOTF that is easy to forget
src/shaders/lens.metal    lens_build, lens_footprint, milkyway_bake, shade_main
src/shaders/common.h      uniform structs shared with the host, all 16-byte members
src/gfx/renderer.cpp      pass sequencing, 41 tunables, preset save/load
src/tools/validate.cpp    55 acceptance checks
```

`src/physics/kerr.h` compiles as both MSL (float) and C++ (double with `KERR_USE_DOUBLE`),
which is what lets the validation suite check the GPU against a double-precision oracle
running the identical algorithm.

## Controls

Arrow up/down selects a tunable, left/right adjusts, `[` and `]` adjust by 10x.
`S` saves the preset, `L` loads it, `F` toggles the fall, `D` toggles diagnostics.
A preset next to the binary auto-loads at startup.

```bash
./build/blackhole --validate
./build/blackhole --frames 600
./build/blackhole --screenshot out.ppm
./build/blackhole --fall
```

`--set "name=value"` drives any tunable from the command line, by the name the HUD shows,
and repeats. `--time seconds` starts the clock partway in. Together they make A/B-ing a
look one command instead of one rebuild, which is most of what the last round of work
actually consisted of.

```bash
./build/blackhole --screenshot out.ppm --time 45 --set "disk opacity=0.6" --set "diag=3"
```

`--fall-path` prints where the falling camera actually goes and stops, with no rendering
and no GPU, and marks the step where it meets the disk. Whether the worldline punches
through the gas or skims over it is a property of three numbers, and reading it off a
trajectory beats reading it off a screenshot of a blurred 500x300 frame. `--fall-tau`
advances the fall to a fixed proper time and holds it there, which is the only way to
photograph the same moment of it twice: the fall otherwise advances on the wall clock, so
frame 900 lands somewhere different on every run.

```bash
./build/blackhole --fall-path --set "fall inward=0.05" --set "fall dtheta=0.0008"
./build/blackhole --fall-tau 146 --screenshot crossing.ppm
```

The `diag` tunable false-colours one intermediate field instead of the image: crossing
occupancy, crossing radius, column, redshift, per-pixel sweep, phase, the Nyquist margin
per slot, the raw turbulence, the shaped density. Every artifact in this project was found
by looking at one of those and not by reading code, so they live in the shader rather than
in a branch someone has to write again. `shade_main` lists which number is which.

## Bugs worth remembering

Not a changelog. These are the ones where the wrong answer looked plausible.

- **Capture radius inside the coordinate singularity.** Stopping at `r+ * 1.0001` puts
  Delta at 3e-5 and `p_r` at 1e5, the step controller rejects forever, and rays get
  silently misclassified. Now it stops halfway to the prograde photon orbit, and a stalled
  ray counter makes sure it cannot hide again.
- **The frozen disk.** Baking `phi - Omega*t_emission` into the map has no dependence on
  the observer's clock, so the gas stood still. Store `phi + Omega*t_travel` and subtract
  `Omega*T` at shade time.
- **The plaid artifact.** Two wrong hypotheses (grazing-ray aliasing, then noise axis
  alignment) cost real time. It was the higher-order lensed echoes, where phase and radius
  alias independently and multiply into a lattice. Sampling echoes coarsely fixed it.
- **The two vertical lines.** Near-miss detection fired only at theta turning points, so a
  ray captured before turning recorded nothing while its neighbour recorded everything.
  Replaced with continuous closest-approach tracking.
- **Closest approach is an argmin, and argmin is discontinuous.** A ray that swings past
  the hole has several competing local minima in height above the plane. Two of them trade
  places under an arbitrarily small change in the ray, so the recorded radius jumped by a
  whole M between neighbouring pixels, which drew a sawtooth of hard-edged fingers standing
  off the disk's rim. Three plausible fixes did nothing, in order: gradient noise instead
  of value noise, a Nyquist fade on the octaves, and solving the minimum exactly inside the
  step instead of at its endpoints. What settled it was A/B-ing the integrator tolerance:
  the teeth dithered but never moved, so the discontinuity was geometric and no amount of
  filtering was ever going to touch it. The fix was to stop taking a minimum at all and
  integrate.
- **A whisker of gas costs a slot.** A ray clipping the very top of the disk opened a
  passage worth a fraction of a percent, which took slot zero and pushed the real crossing
  down one. Anything the shading did differently per slot then changed along that boundary,
  and it drew a dead straight line across the disk. Passages under a hundredth of a
  crossing are dropped now, and nothing downstream reads the slot index any more.
- **Merging and splitting the same passage.** A ray that bobs a few scale heights above the
  gas and comes straight back has not been anywhere, but closing the passage the moment a
  step stopped contributing split it in two, and slot zero's column tripled across the line
  where that happened. The criterion is travel now, not height: the disk's own thickness
  has nothing to do with whether two encounters are the same encounter.
- **A translucent disk stops being a disk.** Dropping the opacity to let the hot inner
  region blaze through the thicker gas also let the near side stop eclipsing the far side,
  and the two superimposed into an orange smear that read as a cloud rather than as a black
  hole. The eclipse is most of what makes the shape legible. Thickness and opacity have to
  be tuned against each other, not one at a time.
- **The fall pointed the camera at nothing.** Looking along -e_r is not looking at the hole
  once the camera is doing a third of the speed of light: aberration slides the shadow out
  of frame just as the disk arrives. Aiming through the inverse boost fixes it. A fixed
  narrow lens then hands you several seconds of black screen at the end, because the shadow
  grows past the field of view, so the lens opens on the way down.
- **The step cap costs the fall its resolution.** Resolving the gas properly is the most
  expensive thing the lens pass does, and the falling camera rebuilds the whole map every
  frame. Paying full price for quadrature accuracy at a fifth of the resolution dropped the
  render scale from 0.23 to 0.18, which is a much more visible loss than the ripple it was
  buying. It is relaxed while falling.
- **Wound-up turbulence, both ways.** Left alone, the shear grinds the disk into sand after
  ten minutes. Fading the aliased octaves out instead leaves a smooth sheet after twenty
  seconds. Neither is a rendering bug; the frozen-in model is simply wrong at long times,
  and it needed a physical fix rather than a filter.
- **AgX double encoding.** The contrast curve outputs display-encoded values, so without a
  trailing EOTF middle grey landed at 0.73 instead of 0.50.
- **`check_ipo_supported` without `LANGUAGES CXX`** fails on SDL's C compiler and silently
  disables LTO for the whole project.

The method that kept working: measure and A/B, do not hypothesise. The plaid and the
vertical lines were both found by disabling components one at a time and dumping the raw
intermediate buffer, not by reading code.

## Open items

Nothing is broken. These are the honest next candidates, in the order I would take them.

1. **The command buffer cross-check is lying.** `gpu cmdbuf` reads 47 ms against a 16 ms
   counter span, consistently, across 60/240/600 frame runs. 47 is close to 3x16 and there
   are 3 frames in flight, so `GPUStartTime` is probably being stamped when the buffer is
   enqueued rather than when it starts executing, making this latency and not busy time.
   Either way the number no longer does the job it was added for, and it should be fixed or
   removed before anyone trusts it.
2. **Shade pass optimisation.** 13.6 of 16 ms. Half-precision shading and function-constant
   specialisation were both skipped in M5 as unjustified at the time. They are justified
   now. The star loop is the first place to look.
3. **The fall runs soft**, around 0.20 render scale. `fall budget` trades frame time
   against sharpness. The lens pass now caps its step inside the gas, so the fall costs
   more per frame than it did; more shade headroom is still the real fix, which is item 2
   again.
4. **Star field cells clip at cube face edges.** No neighbourhood wrapping, so filtering is
   slightly wrong along the 12 seams. Nobody has noticed it in an image yet.
5. **The sweep is isotropic.** It takes the largest step over both screen axes, so where
   the disk is compressed along one axis and fine along the other, detail is thrown away in
   the direction that could have carried it. A proper anisotropic footprint would keep it.
   Nothing in the current framing shows this.
6. **The near disk silhouettes hard against the photon ring**, just off the shadow's edge.
   That edge is real geometry, but it lands on one pixel and could use a footprint-aware
   softening if it ever reads as a seam.

## Ground rules

The user has been explicit about these.

- Scary over serene. Violent, huge, dangerous.
- The galaxy comes in from the left, crosses behind the hole, and you watch it bend. The
  camera and the hole travel together; the sky is what moves.
- Fall *through* the disk, not past it or above it.
- Almost accurate, with parameters tuned for the look. The physics stays correct; the
  knobs get pushed.
- No loyalty to any existing code. Rewrite whatever needs it.
- Modern C++, precompute freely, SIMD and data-oriented layout, avoid cache misses.
- The window title does not change.
