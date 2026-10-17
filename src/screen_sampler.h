#pragma once
#include "color.h"

namespace arena {
enum class ColorMode { Vivid, BrightAreas, Average };

// A small, fixed-size histogram keeps color selection on the 64 x 36 capture grid.
// Rear wall lights cover their entire screen half; bases accent the bottom third.
class ScreenSampler {
    static constexpr int Hues = 24, Neutral = Hues, Bins = Hues + 1;
    struct Bucket {
        float score{}, coverage{}, light{};
        Linear chroma{};
        void add(const Bucket& other, float scale = 1) {
            score += other.score * scale; coverage += other.coverage * scale;
            light += other.light * scale;
            chroma.r += other.chroma.r * scale;
            chroma.g += other.chroma.g * scale;
            chroma.b += other.chroma.b * scale;
        }
    };
    struct Region {
        std::array<Bucket, Bins> bins{};
        Linear sum{};
        int count{};
        void add(Linear c, const Bucket& vote, int bin, float fraction) {
            ++count; sum.r += c.r; sum.g += c.g; sum.b += c.b;
            bins[bin].add(vote, 1 - fraction);
            if (fraction > 0) bins[(bin + 1) % Hues].add(vote, fraction);
        }
        Bucket cluster(int bin) const {
            Bucket result = bins[bin];
            if (bin != Neutral) {
                result.add(bins[(bin + Hues - 1) % Hues], .5f);
                result.add(bins[(bin + 1) % Hues], .5f);
            }
            return result;
        }
    };
    std::array<int, 4> previous_{{-1, -1, -1, -1}};
    ColorMode mode_ = ColorMode::Vivid;
    bool trim_ = true;
public:
    void reset() { previous_.fill(-1); }
    LinearColors sample(const std::vector<Pixel>& pixels, int width, int height, bool trimBars,
                        ColorMode mode = ColorMode::Vivid) {
        auto bounds = sampleBounds(pixels, width, height, trimBars);
        if (mode != mode_ || trimBars != trim_) reset();
        mode_ = mode; trim_ = trimBars;
        const int middle = (bounds.left + bounds.right) / 2;
        const int baseTop = bounds.top + (bounds.bottom - bounds.top) * 2 / 3;
        std::array<Region, 4> regions{};
        for (int y = bounds.top; y < bounds.bottom; ++y) for (int x = bounds.left; x < bounds.right; ++x) {
            const auto& p = pixels[y * width + x];
            Linear c{clean(p.r), clean(p.g), clean(p.b)};
            float maximum = peak(c), minimum = std::min({c.r, c.g, c.b});
            float saturation = maximum > 0 ? (maximum - minimum) / maximum : 0;
            int bin = Neutral; float fraction = 0;
            Bucket vote{};
            if (mode != ColorMode::Average && maximum > 0) {
                // Bound each pixel's influence: a tiny HDR glint must not outweigh a large area.
                float intensity = mode == ColorMode::BrightAreas ? .2126f * c.r + .7152f * c.g + .0722f * c.b : maximum;
                float visible = intensity / (intensity + (mode == ColorMode::BrightAreas ? .35f : .15f));
                float score = mode == ColorMode::BrightAreas ? visible * visible * visible
                    : visible * visible * (.35f + 1.65f * saturation * saturation);
                vote = {score, 1, score * (maximum / (1 + maximum)),
                        {score * (c.r / maximum), score * (c.g / maximum), score * (c.b / maximum)}};
                if (saturation >= .08f) {
                    float delta = maximum - minimum;
                    float hue = maximum == c.r ? (c.g - c.b) / delta
                        : maximum == c.g ? 2 + (c.b - c.r) / delta : 4 + (c.r - c.g) / delta;
                    if (hue < 0) hue += 6;
                    float position = hue * (Hues / 6.f);
                    int whole = static_cast<int>(position);
                    bin = whole % Hues; fraction = position - whole;
                }
            }
            int side = x < middle ? 0 : 2;
            regions[side + 1].add(c, vote, bin, fraction);
            if (y >= baseTop) regions[side].add(c, vote, bin, fraction);
        }
        LinearColors result{};
        for (int z = 0; z < 4; ++z) {
            const auto& region = regions[z];
            if (!region.count) continue;
            if (mode == ColorMode::Average) {
                float n = static_cast<float>(region.count);
                result[z] = {region.sum.r / n, region.sum.g / n, region.sum.b / n};
                continue;
            }
            int best = 0; Bucket selected = region.cluster(0);
            for (int i = 1; i < Bins; ++i) {
                auto candidate = region.cluster(i);
                if (candidate.score > selected.score) { best = i; selected = candidate; }
            }
            // Keep the current hue through near-ties. A clear scene change switches immediately.
            if (previous_[z] >= 0) {
                auto previous = region.cluster(previous_[z]);
                if (previous.score > 0 && selected.score < previous.score * 1.2f) {
                    best = previous_[z]; selected = previous;
                }
            }
            if (selected.score <= 0) { previous_[z] = -1; continue; }
            previous_[z] = best;
            // Recover representative HDR intensity after averaging in a bounded space.
            float light = std::min(selected.light / selected.score, .9999f);
            float strength = light / (1 - light);
            // Very small patches on black should not turn into a full-strength wall wash.
            strength *= std::min(1.f, selected.coverage / (region.count * .12f));
            float scale = strength / selected.score;
            result[z] = {selected.chroma.r * scale, selected.chroma.g * scale, selected.chroma.b * scale};
        }
        return result;
    }
};
}
