#pragma once

#include <QuartzCore/QuartzCore.hpp>
#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace bhs {

// SDL3 MAKES WINDOW, PUMPS EVENTS, HANDS OVER CAMETALLAYER. GETS OUT OF WAY.
class Window {
public:
    bool init(const char* title, int width, int height);
    void shutdown();

    // FALSE ON QUIT.
    bool pumpEvents();

    void setTitle(const char* title);
    void drawableSize(std::uint32_t& widthPx, std::uint32_t& heightPx) const;

    CA::MetalLayer* metalLayer() const { return layer_; }

    // KEYSTROKES THIS FRAME. CLEARED ON PUMP.
    const std::vector<int>& keysPressed() const { return keys_; }
    bool            resized()          { const bool r = resized_; resized_ = false; return r; }

private:
    SDL_Window*     window_  = nullptr;
    SDL_MetalView   view_    = nullptr;
    CA::MetalLayer* layer_   = nullptr;
    bool            resized_ = false;
    std::vector<int> keys_;
};

} // NAMESPACE BHS

