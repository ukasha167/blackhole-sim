#include "gfx/hud.hpp"

#include <CoreText/CoreText.h>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace bhs {

namespace {

constexpr double kFontSize = 20.0;

// SYSTEM MONOSPACE FONT WITH MENLO DEFAULT.
CTFontRef createMonospaceFont() {
    CFStringRef name = CFSTR("Menlo-Regular");
    if (CTFontRef font = CTFontCreateWithName(name, kFontSize, nullptr)) {
        return font;
    }
    return CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, kFontSize, nullptr);
}

} // NAMESPACE

bool Hud::buildAtlas(MTL::Device* device) {
    CTFontRef font = createMonospaceFont();
    if (!font) {
        SDL_Log("HUD: could not create a monospace font.");
        return false;
    }

    const CGFloat ascent  = CTFontGetAscent(font);
    const CGFloat descent = CTFontGetDescent(font);
    const CGFloat leading = CTFontGetLeading(font);

    // MONOSPACE ADVANCE FROM SINGLE GLYPH.
    UniChar sample = 'M';
    CGGlyph sampleGlyph = 0;
    CTFontGetGlyphsForCharacters(font, &sample, &sampleGlyph, 1);
    CGSize advance{};
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &sampleGlyph, &advance, 1);

    const auto cellW = static_cast<std::uint32_t>(std::ceil(advance.width));
    const auto cellH = static_cast<std::uint32_t>(std::ceil(ascent + descent + leading));
    if (cellW == 0 || cellH == 0) {
        SDL_Log("HUD: degenerate font metrics.");
        CFRelease(font);
        return false;
    }

    cellWidth_  = static_cast<float>(cellW);
    cellHeight_ = static_cast<float>(cellH);

    const std::uint32_t atlasW = cellW * BHS_ATLAS_COLS;
    const std::uint32_t atlasH = cellH * BHS_ATLAS_ROWS;

    std::vector<std::uint8_t> pixels(static_cast<size_t>(atlasW) * atlasH, 0);

    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), atlasW, atlasH, 8,
                                             atlasW, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if (!ctx) {
        SDL_Log("HUD: could not create the glyph bitmap context.");
        CFRelease(font);
        return false;
    }

    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextSetShouldAntialias(ctx, true);
    // SINGLE-CHANNEL COVERAGE MASK. NO SUBPIXEL SMOOTHING.
    CGContextSetShouldSmoothFonts(ctx, false);
    CGContextSetShouldSubpixelPositionFonts(ctx, false);
    CGContextSetShouldSubpixelQuantizeFonts(ctx, false);

    for (std::uint32_t i = 0; i < BHS_GLYPH_COUNT; ++i) {
        const auto character = static_cast<UniChar>(BHS_GLYPH_FIRST + i);

        CGGlyph glyph = 0;
        if (!CTFontGetGlyphsForCharacters(font, &character, &glyph, 1)) {
            continue;
        }

        const std::uint32_t col = i % BHS_ATLAS_COLS;
        const std::uint32_t row = i / BHS_ATLAS_COLS;

        // FLIP Y. COREGRAPHICS IS BOTTOM-LEFT ORIGIN.
        const CGFloat originX = static_cast<CGFloat>(col * cellW);
        const CGFloat originY = static_cast<CGFloat>(atlasH - (row + 1) * cellH);

        CGPoint position = CGPointMake(originX, originY + descent);
        CTFontDrawGlyphs(font, &glyph, &position, 1, ctx);
    }

    CGContextRelease(ctx);
    CFRelease(font);

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatR8Unorm);
    desc->setWidth(atlasW);
    desc->setHeight(atlasH);
    desc->setMipmapLevelCount(1);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    atlas_ = device->newTexture(desc);
    desc->release();

    if (!atlas_) {
        SDL_Log("HUD: could not allocate the font atlas texture.");
        return false;
    }

    atlas_->replaceRegion(MTL::Region(0, 0, atlasW, atlasH), 0, pixels.data(), atlasW);
    return true;
}

bool Hud::init(MTL::Device* device) {
    if (!buildAtlas(device)) {
        return false;
    }

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        cells_[i] = device->newBuffer(sizeof(grid_), MTL::ResourceStorageModeShared);
        if (!cells_[i]) {
            SDL_Log("HUD: could not allocate cell buffer %u.", i);
            return false;
        }
    }

    clear();
    return true;
}

void Hud::clear() {
    for (auto& cell : grid_) {
        cell = ' ';
    }
}

void Hud::print(std::uint32_t col, std::uint32_t row, const char* fmt, ...) {
    if (row >= kRows || col >= kColumns) {
        return;
    }

    char line[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    std::uint32_t cursor = col;
    for (const char* c = line; *c && cursor < kColumns; ++c, ++cursor) {
        const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(*c));
        grid_[row * kColumns + cursor] = value;
    }
}

void Hud::upload(std::uint32_t slot) {
    MTL::Buffer* buffer = cells_[slot % kFramesInFlight];
    std::memcpy(buffer->contents(), grid_, sizeof(grid_));
}

void Hud::shutdown() {
    for (auto*& buffer : cells_) {
        if (buffer) {
            buffer->release();
            buffer = nullptr;
        }
    }
    if (atlas_) {
        atlas_->release();
        atlas_ = nullptr;
    }
}

} // NAMESPACE BHS
