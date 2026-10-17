#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "color.h"

namespace arena {
struct Monitor { HMONITOR handle{}; std::wstring name; bool primary{}; };
std::vector<Monitor> monitors();
enum class CaptureBackend { Borderless, WindowsGraphicsCapture };
struct CaptureStats {
    uint64_t submitted{}, readbacks{}, directFrames{}, copiedBytes{}, busyReadbacks{}, gpuSamples{};
    double gpuMilliseconds{};
};
class Capture {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    static constexpr int Width = 64, Height = 36;
    explicit Capture(HMONITOR monitor, CaptureBackend backend = CaptureBackend::Borderless);
    ~Capture();
    bool read(std::vector<Pixel>& pixels);
    std::wstring description() const;
    CaptureStats stats() const;
    static void testGpuPipeline();
};
}
