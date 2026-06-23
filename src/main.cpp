#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>

#include "defines.hpp"
#include "renderer.hpp"
#include "solver.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    bhs::Renderer renderer;
    if (!renderer.init(bhs::kWindowTitle, bhs::kWindowWidth, bhs::kWindowHeight)) {
        SDL_Log("Renderer init failed");
        SDL_Quit();
        return 1;
    }

    bhs::Solver solver;
    solver.init();

    bool running = true;
    uint64_t lastTicks = SDL_GetTicksNS();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }

        const uint64_t nowTicks = SDL_GetTicksNS();
        double dt = static_cast<double>(nowTicks - lastTicks) / 1e9;
        lastTicks = nowTicks;
        dt = std::min(dt, bhs::kMaxFrameTime);

        solver.update(dt);
        renderer.renderFrame(solver.cameraState(), solver.diskState(), solver.elapsedTime());
    }

    renderer.shutdown();
    SDL_Quit();
    return 0;
}
