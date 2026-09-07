#include "app/window.hpp"

namespace bhs {

bool Window::init(const char* title, int width, int height) {
    window_ = SDL_CreateWindow(title, width, height,
                               SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                               SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    view_ = SDL_Metal_CreateView(window_);
    if (!view_) {
        SDL_Log("SDL_Metal_CreateView failed: %s", SDL_GetError());
        return false;
    }

    layer_ = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view_));
    if (!layer_) {
        SDL_Log("SDL_Metal_GetLayer returned nothing.");
        return false;
    }

    return true;
}

bool Window::pumpEvents() {
    keys_.clear();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                return false;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    return false;
                }
                if (!event.key.repeat || event.key.key == SDLK_LEFT ||
                    event.key.key == SDLK_RIGHT) {
                    keys_.push_back(static_cast<int>(event.key.key));
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
                resized_ = true;
                break;
            default:
                break;
        }
    }
    return true;
}

void Window::setTitle(const char* title) {
    if (window_) {
        SDL_SetWindowTitle(window_, title);
    }
}

void Window::drawableSize(std::uint32_t& widthPx, std::uint32_t& heightPx) const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    widthPx  = static_cast<std::uint32_t>(w > 0 ? w : 0);
    heightPx = static_cast<std::uint32_t>(h > 0 ? h : 0);
}

void Window::shutdown() {
    if (view_) {
        SDL_Metal_DestroyView(view_);
        view_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    layer_ = nullptr;
}

} // NAMESPACE BHS
