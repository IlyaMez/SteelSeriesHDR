#include "color.h"
#include "output_gate.h"
#include "screen_sampler.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace arena;
int assertions = 0;
void check(bool pass, const char* name) { ++assertions; if (!pass) throw std::runtime_error(name); }
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
int main() {
    try {
        check(encode(toneMap({}, 1, 1), 1) == Rgb{}, "Black must remain black");
        auto white = encode(toneMap({1, 1, 1}, 1, 1), 1);
        check(white.r == white.g && white.g == white.b && white.r > 180, "SDR white is neutral and visible");
        auto hdr = toneMap({8, 2, 1}, 1, 1);
        check(near(hdr.r / hdr.g, 4) && near(hdr.g / hdr.b, 2), "HDR ratios must survive shoulder");
        check(hdr.r <= 1 && hdr.r > .99f, "Highlights fit LED range");
        check(encode(toneMap({8, 2, 1}, 1, 1), 1) != encode(toneMap({1, 1, 1}, 1, 1), 1), "HDR cannot clip to white before mapping");
        check(encode(toneMap({-2, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}, 1, 1), 1) == Rgb{}, "Nonfinite and negative values sanitized");
        check(encode({1, 1, 1}, 0) == Rgb{}, "Zero brightness");
        auto grey = toneMap({1, 0, 0}, 1, 0); check(near(grey.r, grey.g) && near(grey.g, grey.b), "Desaturation is neutral");
        auto oneStep = smooth({}, {1, 1, 1}, .1f, .2f);
        auto twoSteps = smooth(smooth({}, {1, 1, 1}, .05f, .2f), {1, 1, 1}, .05f, .2f);
        check(near(oneStep.r, twoSteps.r), "Smoothing independent of frame rate");
        check(smooth({}, {1, 1, 1}, .1f, 0).r == 1, "Smoothing off");
        std::vector<Pixel> pixels(64 * 36);
        for (int y = 0; y < 36; ++y) for (int x = 0; x < 64; ++x)
            pixels[y * 64 + x] = x < 32 ? (y < 18 ? Pixel{0, 2, 0, 1} : Pixel{3, 0, 0, 1}) : (y < 18 ? Pixel{4, 2, 0, 1} : Pixel{0, 0, 5, 1});
        auto zones = sampleZones(pixels, 64, 36, false);
        check(near(zones[0].r, 3) && near(zones[1].g, 2) && near(zones[2].b, 5) && near(zones[3].r, 4), "Physical zone mapping");
        std::fill(pixels.begin(), pixels.end(), Pixel{1, 1, 1, 1});
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 64; ++x) { pixels[y * 64 + x] = {}; pixels[(35 - y) * 64 + x] = {}; }
        check(near(sampleZones(pixels, 64, 36, true)[0].r, 1), "Black borders excluded");
        check(sampleZones(pixels, 64, 36, false)[0].r < .8f, "Border switch respected");
        std::fill(pixels.begin(), pixels.end(), Pixel{});
        check(sampleZones(pixels, 64, 36, true)[0].r == 0, "All-black frame remains black");
        bool threw = false; try { sampleZones({}, 64, 36, true); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Invalid sample size rejected");
        Colors colors{{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}}};
        auto report = lightingReport(colors, 64);
        check(report.size() == 64 && report[0] == 6 && report[1] == 0xA1 && report[26] == 15, "Lighting envelope");
        check(report[2] == 1 && report[8] == 4 && report[14] == 7 && report[20] == 10, "USB zone order");
        check(report[5] == 1 && report[6] == 30 && report[7] == 10, "Lighting effect and brightness bytes");
        check(std::all_of(report.begin() + 27, report.end(), [](auto n) { return n == 0; }), "Zero padding");
        check(lightingReport(colors, 65).size() == 65, "Windows report padding supported");
        threw = false; try { lightingReport(colors, 32); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Unexpected HID descriptor rejected");
        OutputGate gate;
        check(gate.needsSend(colors, 0), "First lighting update always sent");
        gate.sent(colors, 0);
        check(!gate.needsSend(colors, .5), "Duplicate USB updates suppressed");
        check(gate.needsSend(colors, 1), "Idle heartbeat after one second");
        auto changed = colors; changed[0].g++;
        check(!gate.needsSend(changed, .03), "Live output is capped at the tested 10 Hz cadence");
        check(gate.needsSend(changed, .1), "One-byte color changes not lost");
        check(gate.needsSend(changed, .2), "Failed writes must remain eligible for retry");
        check(!gate.needsSend(colors, .03, true) && gate.needsSend(colors, .11, true), "Held colors repeat at the same bounded output cadence");
        gate.sent(changed, .2);
        auto newest = changed; newest[3].b++;
        check(!gate.needsSend(newest, .25) && gate.needsSend(newest, .31), "The newest target becomes eligible without queueing old targets");
        check(!gate.needsSend(changed, .9) && gate.needsSend(changed, 1.21), "Successful output resets the idle heartbeat");
        auto step = smooth({}, {1, .5f, .2f}, 1.f / 30, .18f);
        for (int i = 0; i < 120; ++i) step = smooth(step, {1, .5f, .2f}, 1.f / 30, .18f);
        check(near(step.r, 1) && near(step.g, .5f), "Smoothing converges even when the desktop stops producing frames");
        // Use complete scene fixtures, including competing hues and misleading highlights.
        auto scene = [&](auto pixel) {
            for (int y = 0; y < 36; ++y) for (int x = 0; x < 64; ++x) pixels[y * 64 + x] = pixel(x, y);
        };
        auto sample = [&](ColorMode mode = ColorMode::Vivid, bool trim = false) {
            ScreenSampler sampler; return sampler.sample(pixels, 64, 36, trim, mode);
        };
        scene([](int x, int y) { return x >= 32 ? Pixel{0, 0, 1, 1} : y < 18 ? Pixel{0, 1, 0, 1} : Pixel{1, 0, 0, 1}; });
        zones = sample(ColorMode::Average);
        check(near(zones[1].r, .5f) && near(zones[1].g, .5f) && near(zones[3].b, 1), "Rear lights cover complete independent left and right sides");
        check(near(zones[0].r, 1) && near(zones[0].g, 0) && near(zones[2].b, 1), "Base accents cover bottom third");
        zones = sample();
        check((zones[1].r > .9f && zones[1].g == 0) || (zones[1].g > .9f && zones[1].r == 0), "Competing red and green stay a real scene hue instead of averaging yellow");
        scene([](int, int y) { return y < 18 ? Pixel{} : Pixel{1, .05f, 0, 1}; });
        zones = sample();
        check(zones[1].r > .9f && zones[3].r > .9f, "Colors in lower screen half reach the strong rear lights");
        scene([](int, int) { return Pixel{8, 2, 1, 1}; });
        zones = sample();
        check(std::abs(zones[1].r - 8) < .01f && near(zones[1].r / zones[1].g, 4), "Uniform HDR hue and intensity survive dominant selection");
        scene([](int x, int y) { return x == 0 && y == 0 ? Pixel{1000, 1000, 1000, 1} : Pixel{.3f, 0, 0, 1}; });
        zones = sample();
        check(zones[1].r > .29f && zones[1].g == 0, "A tiny HDR white glint cannot overpower a red scene");
        scene([](int x, int) { return x % 32 < 10 ? Pixel{.4f, 0, 0, 1} : Pixel{.4f, .4f, .4f, 1}; });
        zones = sample();
        check(zones[1].r > .39f && zones[1].g == 0, "Vivid mode favors a substantial saturated area over grey surroundings");
        check(sample(ColorMode::BrightAreas)[1].g > .39f, "Bright areas mode can select bright neutral surroundings");
        scene([](int x, int) { return x % 32 < 3 ? Pixel{4, 4, 4, 1} : Pixel{.1f, 0, 0, 1}; });
        check(sample(ColorMode::BrightAreas)[1].g > 1, "Bright areas follows a visible bright patch in a darker scene");
        scene([](int x, int) { return x % 32 < 24 ? Pixel{0, 0, .001f, 1} : Pixel{.3f, .3f, .3f, 1}; });
        zones = sample();
        check(near(zones[1].r, zones[1].b) && zones[1].r > .29f, "Almost-black saturated noise does not win over visible content");
        scene([](int, int) { return Pixel{.2f, .2f, .2f, 1}; });
        zones = sample();
        check(near(zones[1].r, .2f) && near(zones[1].r, zones[1].g) && near(zones[1].g, zones[1].b), "Neutral scene remains neutral");
        scene([](int x, int y) { return x % 32 < 2 && y < 2 ? Pixel{1, 0, 0, 1} : Pixel{}; });
        zones = sample();
        check(zones[1].r > 0 && zones[1].r < .05f, "Tiny HUD patch on black stays dim");
        scene([](int, int y) { return y < 4 || y >= 32 ? Pixel{} : Pixel{.2f, .2f, .2f, 1}; });
        check(near(sample(ColorMode::Average, true)[1].r, .2f) && sample(ColorMode::Average, false)[1].r < .18f, "Full-side sampling honors black-bar trimming");
        ScreenSampler steady;
        scene([](int x, int) { return x % 32 < 17 ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
        check(steady.sample(pixels, 64, 36, false)[1].r > .9f, "Dominant selection initially picks larger red area");
        scene([](int x, int) { return x % 32 < 15 ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
        check(steady.sample(pixels, 64, 36, false)[1].r > .9f, "Near-tie area changes do not flicker between hues");
        scene([](int x, int) { return x % 32 < 12 ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
        check(steady.sample(pixels, 64, 36, false)[1].b > .9f, "A clearly stronger new color replaces the previous hue");
        scene([](int x, int) { return x % 32 < 17 ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
        steady.reset();
        check(steady.sample(pixels, 64, 36, false)[1].r > .9f, "Capture restart resets color history");
        scene([](int x, int) { return x % 2 ? Pixel{1, .001f, .05f, 1} : Pixel{1, .05f, .001f, 1}; });
        zones = sample();
        check(zones[1].r > .9f && zones[1].g > 0 && zones[1].b > 0 && near(zones[1].g, zones[1].b), "Hue histogram joins reds across the hue wraparound");
        scene([](int, int) { return Pixel{}; });
        for (auto mode : {ColorMode::Vivid, ColorMode::BrightAreas, ColorMode::Average}) {
            zones = steady.sample(pixels, 64, 36, true, mode);
            check(std::all_of(zones.begin(), zones.end(), [](Linear c) { return peak(c) == 0; }), "Every mode releases previous color on a black scene");
        }
        scene([](int, int) { return Pixel{-1, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1}; });
        check(peak(sample()[1]) == 0, "Dominant sampling sanitizes invalid pixels");
        threw = false; try { steady.sample({}, 64, 36, true); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Dominant sampling rejects incomplete frames");
        // Fixed orange-scene regression inputs sampled through
        // the same 64 x 36 Vivid path. The old output included ~40 blue on both rears.
        for (Linear orange : {Linear{.670772f, .113974f, .0428277f}, Linear{.83672f, .186257f, .0453251f}}) {
            auto mapped = toneMap(orange, .59f, 1);
            auto natural = encode(mapped, 1);
            auto rich = encode(mapped, 1, .85f);
            check(natural.b >= 35 && rich.b <= 10 && rich.r == natural.r && rich.g > rich.b * 4,
                  "Orange scene produces rich rear color without lowering peak brightness");
            float beforeHue = float(natural.g - natural.b) / (natural.r - natural.b);
            float afterHue = float(rich.g - rich.b) / (rich.r - rich.b);
            check(std::abs(beforeHue - afterHue) < .02f, "Rear richness preserves orange RGB hue within byte rounding");
        }
        check(encode({.5f, .5f, .5f}, 1, 1) == encode({.5f, .5f, .5f}, 1), "Rear richness leaves neutral grey unchanged");
        check(encode({.5f, .5f, .49f}, 1, 1) == encode({.5f, .5f, .49f}, 1), "Near-neutral noise is not exaggerated");
        check(encode({}, 1, 1) == Rgb{}, "Rich black is still black");
        check(encode({1, 1, 1}, 1, 1) == Rgb{255, 255, 255}, "White scenes remain white at maximum richness");
        check(encode({1, .25f, 0}, 1, 1) == encode({1, .25f, 0}, 1), "Colors without a white component remain unchanged");
        check(encode({.7f, .2f, .05f}, 0, 1) == Rgb{}, "Brightness zero still disables rich output");
        auto restored = encode({.7f, .2f, .05f}, 1, 0);
        check(restored == encode({.7f, .2f, .05f}, 1), "Zero richness restores the original encoding exactly");
        auto full = encode({.7f, .2f, .05f}, 1, 1);
        check(full.b == 0 && full.r == restored.r, "Full richness removes common white while keeping peak output");
        int previousBlue = restored.b;
        for (float amount : {.25f, .5f, .75f, 1.f}) {
            auto current = encode({.7f, .2f, .05f}, 1, amount);
            check(current.b <= previousBlue && current.r == restored.r, "Richness increases smoothly without raising weak channels");
            previousBlue = current.b;
        }
        std::cout << "PASS: " << assertions << " color, HDR, sampling, smoothing, and HID assertions\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
