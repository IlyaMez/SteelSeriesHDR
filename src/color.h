#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace arena {
struct Linear { float r{}, g{}, b{}; };
struct Rgb { uint8_t r{}, g{}, b{}; bool operator==(const Rgb&) const = default; };
using Colors = std::array<Rgb, 4>;
using LinearColors = std::array<Linear, 4>;
struct Pixel { float r{}, g{}, b{}, a{}; };
inline float clean(float x) { return std::isfinite(x) ? std::max(0.f, x) : 0.f; }
inline float peak(Linear c) { return std::max({c.r, c.g, c.b}); }
inline float srgb(float x) {
    x = std::clamp(clean(x), 0.f, 1.f);
    return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.f / 2.4f) - .055f;
}
inline Linear toneMap(Linear c, float exposure, float saturation) {
    c = {clean(c.r), clean(c.g), clean(c.b)};
    float y = .2126f * c.r + .7152f * c.g + .0722f * c.b;
    c = {clean(y + saturation * (c.r - y)), clean(y + saturation * (c.g - y)), clean(y + saturation * (c.b - y))};
    float p = peak(c);
    // One shared shoulder preserves channel ratios; never clip HDR to [0,1] first.
    float scale = p > 0 ? -std::expm1(-p * exposure) / p : 0;
    return {c.r * scale, c.g * scale, c.b * scale};
}
inline Rgb encode(Linear c, float brightness, float richness = 0) {
    Linear rgb{srgb(c.r), srgb(c.g), srgb(c.b)};
    const float maximum = peak(rgb), minimum = std::min({rgb.r, rgb.g, rgb.b});
    if (maximum > 0 && richness > 0) {
        float saturation = (maximum - minimum) / maximum;
        // Fade in only for chromatic colors. White/grey and tiny RGB noise remain
        // neutral instead of acquiring an exaggerated tint.
        float gate = std::clamp((saturation - .08f) / .22f, 0.f, 1.f);
        gate = gate * gate * (3 - 2 * gate);
        float white = minimum * std::clamp(richness, 0.f, 1.f) * gate;
        // Removing a common component preserves RGB hue; rescaling preserves the
        // peak brightness. Do this after sRGB encoding, which raises weak channels.
        float scale = maximum / (maximum - white);
        rgb = {(rgb.r - white) * scale, (rgb.g - white) * scale, (rgb.b - white) * scale};
    }
    auto channel = [brightness](float x) {
        return static_cast<uint8_t>(std::lround(255.f * std::clamp(x, 0.f, 1.f) * std::clamp(brightness, 0.f, 1.f)));
    };
    return {channel(rgb.r), channel(rgb.g), channel(rgb.b)};
}
inline Linear smooth(Linear prev, Linear next, float dt, float tau) {
    float a = tau <= 0 ? 1.f : -std::expm1(-std::max(0.f, dt) / tau);
    return {prev.r + a * (next.r - prev.r), prev.g + a * (next.g - prev.g), prev.b + a * (next.b - prev.b)};
}
struct SampleBounds { int left, top, right, bottom; };
inline SampleBounds sampleBounds(const std::vector<Pixel>& pixels, int width, int height, bool trimBars) {
    if (width < 2 || height < 2 || pixels.size() != static_cast<size_t>(width) * height)
        throw std::invalid_argument("Invalid sample dimensions");
    auto black = [](const Pixel& p) { return std::max({clean(p.r), clean(p.g), clean(p.b)}) < .003f; };
    auto rowBlack = [&](int y) { for (int x = 0; x < width; ++x) if (!black(pixels[y * width + x])) return false; return true; };
    auto colBlack = [&](int x) { for (int y = 0; y < height; ++y) if (!black(pixels[y * width + x])) return false; return true; };
    int left = 0, top = 0, right = width, bottom = height;
    if (trimBars) {
        while (top < height / 4 && rowBlack(top)) ++top;
        while (bottom > height * 3 / 4 && rowBlack(bottom - 1)) --bottom;
        while (left < width / 4 && colBlack(left)) ++left;
        while (right > width * 3 / 4 && colBlack(right - 1)) --right;
    }
    return {left, top, right, bottom};
}
// Quadrants are also used by the capture diagnostics to verify texture orientation.
inline LinearColors sampleZones(const std::vector<Pixel>& pixels, int width, int height, bool trimBars) {
    auto [left, top, right, bottom] = sampleBounds(pixels, width, height, trimBars);
    LinearColors sums{};
    std::array<int, 4> counts{};
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        // Physical order: left base, left rear, right base, right rear.
        int zone = (x >= (left + right) / 2 ? 2 : 0) + (y < (top + bottom) / 2 ? 1 : 0);
        const auto& p = pixels[y * width + x];
        sums[zone].r += clean(p.r); sums[zone].g += clean(p.g); sums[zone].b += clean(p.b); ++counts[zone];
    }
    for (int i = 0; i < 4; ++i) if (counts[i]) {
        float n = static_cast<float>(counts[i]);
        sums[i] = {sums[i].r / n, sums[i].g / n, sums[i].b / n};
    }
    return sums;
}
// Arena 7 protocol documented by szamski/Prismatic-for-macOS (see THIRD_PARTY.md).
inline std::vector<uint8_t> lightingReport(const Colors& colors, size_t reportLength, int brightness = 10) {
    if (reportLength != 64 && reportLength != 65) throw std::invalid_argument("Unexpected Arena 7 output report length");
    std::vector<uint8_t> report(reportLength, 0);
    report[0] = 0x06; report[1] = 0xA1;
    for (size_t i = 0; i < colors.size(); ++i) {
        size_t p = 2 + i * 6;
        report[p] = colors[i].r; report[p + 1] = colors[i].g; report[p + 2] = colors[i].b;
        report[p + 3] = 0x01; report[p + 4] = 0x1E;
        report[p + 5] = static_cast<uint8_t>(std::clamp(brightness, 0, 10));
    }
    report[26] = 0x0F;
    return report;
}
}
