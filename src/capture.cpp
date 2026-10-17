#include "capture.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <cstring>

namespace arena {
using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

std::vector<Monitor> monitors() {
    std::vector<Monitor> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM param) -> BOOL {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(h, &info)) {
            std::wstring name = info.szDevice;
            name += L"  (" + std::to_wstring(info.rcMonitor.right - info.rcMonitor.left) + L" x " + std::to_wstring(info.rcMonitor.bottom - info.rcMonitor.top) + L")";
            bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            if (primary) name += L"  Primary";
            reinterpret_cast<std::vector<Monitor>*>(param)->push_back({h, name, primary});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

struct Capture::Impl {
    CaptureStats counters{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<IDXGIOutputDuplication> duplication;
    bool ownsDesktopFrame = false, pending = false, hdrDisplay = false;
    bool borderlessWgc = false;
    std::wstring borderStatus;
    UINT rotation = DXGI_MODE_ROTATION_IDENTITY;
    IDirect3DDevice runtimeDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    Direct3D11CaptureFrame heldFrame{nullptr};
    SizeInt32 size{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    com_ptr<ID3D11ComputeShader> shader;
    com_ptr<ID3D11Buffer> constants;
    com_ptr<ID3D11Texture2D> sampled, staging, fallback;
    com_ptr<ID3D11UnorderedAccessView> uav;
    com_ptr<ID3D11ShaderResourceView> fallbackView;
    struct CachedView { com_ptr<ID3D11Texture2D> texture; com_ptr<ID3D11ShaderResourceView> view; };
    std::vector<CachedView> views;
    com_ptr<ID3D11Query> gpuDisjoint, gpuStart, gpuEnd;

    ~Impl() {
        if (ownsDesktopFrame) duplication->ReleaseFrame();
        try { if (heldFrame) heldFrame.Close(); if (session) session.Close(); if (pool) pool.Close(); } catch (...) {}
    }
    void createDevice(IDXGIAdapter* adapter) {
        check_hresult(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
    }
    void initDesktop(HMONITOR monitor) {
        com_ptr<IDXGIFactory1> factory; check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()));
        for (UINT a = 0; ; ++a) {
            com_ptr<IDXGIAdapter1> adapter;
            HRESULT hr = factory->EnumAdapters1(a, adapter.put());
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            check_hresult(hr);
            for (UINT o = 0; ; ++o) {
                com_ptr<IDXGIOutput> output;
                hr = adapter->EnumOutputs(o, output.put());
                if (hr == DXGI_ERROR_NOT_FOUND) break;
                check_hresult(hr);
                DXGI_OUTPUT_DESC desc{}; check_hresult(output->GetDesc(&desc));
                if (desc.Monitor != monitor || !desc.AttachedToDesktop) continue;
                createDevice(adapter.get()); // Capture on the GPU driving this monitor.
                auto output5 = output.as<IDXGIOutput5>();
                constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
                check_hresult(output5->DuplicateOutput1(device.get(), 0, 2, formats, duplication.put()));
                if (auto output6 = output.try_as<IDXGIOutput6>()) {
                    DXGI_OUTPUT_DESC1 advanced{};
                    if (SUCCEEDED(output6->GetDesc1(&advanced)))
                        hdrDisplay = advanced.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                }
                rotation = desc.Rotation;
                size = {desc.DesktopCoordinates.right - desc.DesktopCoordinates.left, desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top};
                return;
            }
        }
        throw std::runtime_error("Selected monitor is disconnected. Stop and select a connected display.");
    }
    void initWgc(HMONITOR monitor, bool requestBorderless) {
        if (!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable.");
        createDevice(nullptr);
        auto dxgi = device.as<IDXGIDevice>(); com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        runtimeDevice = inspectable.as<IDirect3DDevice>();
        auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        check_hresult(factory->CreateForMonitor(monitor, guid_of<GraphicsCaptureItem>(), put_abi(item)));
        size = item.Size();
        pool = Direct3D11CaptureFramePool::CreateFreeThreaded(runtimeDevice, DirectXPixelFormat::R16G16B16A16Float, 2, size);
        session = pool.CreateCaptureSession(item); session.IsCursorCaptureEnabled(false);
        if (requestBorderless) {
            if (auto border = session.try_as<IGraphicsCaptureSession3>()) {
                try {
                    auto access = GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
                    using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
                    border.IsBorderRequired(false);
                    borderlessWgc = access == AppCapabilityAccessStatus::Allowed;
                    if (!borderlessWgc) borderStatus = L" | border permission not granted";
                } catch (const hresult_error&) { borderStatus = L" | border permission unavailable"; }
            } else borderStatus = L" | Windows version requires border";
        }
        if (auto rate = session.try_as<IGraphicsCaptureSession5>()) rate.MinUpdateInterval(std::chrono::milliseconds(33));
    }
    explicit Impl(HMONITOR monitor, CaptureBackend backend) {
        if (!monitor) createDevice(nullptr); // Off-screen deterministic GPU tests.
        else if (backend == CaptureBackend::Borderless) {
            try { initDesktop(monitor); }
            catch (const hresult_error&) {
                duplication = nullptr; context = nullptr; device = nullptr;
                rotation = DXGI_MODE_ROTATION_IDENTITY; hdrDisplay = false;
                initWgc(monitor, true);
            }
        } else initWgc(monitor, false);
        constexpr char source[] = R"(
Texture2D<float4> screen : register(t0);
RWTexture2D<float4> small : register(u0);
cbuffer Settings : register(b0) { uint width; uint height; uint rotation; uint decodeSrgb; };
float3 linearize(float3 v) {
    return float3(v.r <= 0.04045 ? v.r / 12.92 : pow((v.r + 0.055) / 1.055, 2.4),
                  v.g <= 0.04045 ? v.g / 12.92 : pow((v.g + 0.055) / 1.055, 2.4),
                  v.b <= 0.04045 ? v.b / 12.92 : pow((v.b + 0.055) / 1.055, 2.4));
}
[numthreads(8, 4, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 64 || id.y >= 36) return;
    float3 sum = 0;
    for (uint y = 0; y < 2; ++y) for (uint x = 0; x < 2; ++x) {
        float2 uv = (float2(id.xy) + (float2(x,y) + 0.5) / 2.0) / float2(64,36);
        if (rotation == 2) uv = float2(uv.y, 1.0 - uv.x);
        else if (rotation == 3) uv = 1.0 - uv;
        else if (rotation == 4) uv = float2(1.0 - uv.y, uv.x);
        uint2 p = min(uint2(uv * float2(width,height)), uint2(width-1,height-1));
        float3 color = screen.Load(int3(p,0)).rgb;
        sum += decodeSrgb ? linearize(color) : color;
    }
    small[id.xy] = float4(sum * 0.25, 1);
})";
        com_ptr<ID3DBlob> code, errors;
        check_hresult(D3DCompile(source, sizeof(source), nullptr, nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put()));
        check_hresult(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 16; buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check_hresult(device->CreateBuffer(&buffer, nullptr, constants.put()));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = Width; desc.Height = Height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        check_hresult(device->CreateTexture2D(&desc, nullptr, sampled.put()));
        check_hresult(device->CreateUnorderedAccessView(sampled.get(), nullptr, uav.put()));
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        check_hresult(device->CreateQuery(&query, gpuDisjoint.put()));
        query.Query = D3D11_QUERY_TIMESTAMP;
        check_hresult(device->CreateQuery(&query, gpuStart.put())); check_hresult(device->CreateQuery(&query, gpuEnd.put()));
        if (session) session.StartCapture();
    }
    bool receive(std::vector<Pixel>& pixels) {
        if (!pending) return false;
        pixels.resize(Width * Height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { ++counters.busyReadbacks; return false; }
        check_hresult(hr);
        for (int y = 0; y < Height; ++y)
            std::memcpy(pixels.data() + y * Width, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, Width * sizeof(Pixel));
        context->Unmap(staging.get(), 0);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{}; UINT64 start{}, end{};
        if (context->GetData(gpuDisjoint.get(), &timing, sizeof(timing), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !timing.Disjoint && timing.Frequency &&
            context->GetData(gpuStart.get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(gpuEnd.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            counters.gpuMilliseconds += 1000.0 * static_cast<double>(end - start) / timing.Frequency; ++counters.gpuSamples;
        }
        ++counters.readbacks; pending = false;
        if (heldFrame) { heldFrame.Close(); heldFrame = nullptr; }
        return true;
    }
    com_ptr<ID3D11Texture2D> acquire() {
        if (duplication) {
            // Keep the previous frame until just before acquisition, as DXGI recommends.
            if (ownsDesktopFrame) { check_hresult(duplication->ReleaseFrame()); ownsDesktopFrame = false; }
            DXGI_OUTDUPL_FRAME_INFO info{}; com_ptr<IDXGIResource> resource;
            HRESULT hr = duplication->AcquireNextFrame(0, &info, resource.put());
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) return nullptr;
            check_hresult(hr); ownsDesktopFrame = true;
            if (!info.LastPresentTime.QuadPart && counters.submitted) return nullptr; // mouse-only
            return resource.as<ID3D11Texture2D>();
        }
        auto frame = pool.TryGetNextFrame(); if (!frame) return nullptr;
        if (auto newer = pool.TryGetNextFrame()) { frame.Close(); frame = std::move(newer); }
        auto content = frame.ContentSize();
        if (content.Width < 1 || content.Height < 1) { frame.Close(); return nullptr; }
        if (content.Width != size.Width || content.Height != size.Height) {
            frame.Close(); size = content; views.clear();
            pool.Recreate(runtimeDevice, DirectXPixelFormat::R16G16B16A16Float, 2, size); return nullptr;
        }
        auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> texture; check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        heldFrame = std::move(frame); return texture;
    }
    com_ptr<ID3D11ShaderResourceView> viewFor(com_ptr<ID3D11Texture2D> texture, D3D11_TEXTURE2D_DESC desc) {
        if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
            for (const auto& v : views) if (v.texture.get() == texture.get()) { ++counters.directFrames; return v.view; }
            com_ptr<ID3D11ShaderResourceView> view;
            if (SUCCEEDED(device->CreateShaderResourceView(texture.get(), nullptr, view.put()))) {
                if (views.size() >= 4) views.clear();
                views.push_back({texture, view}); ++counters.directFrames; return view;
            }
        }
        D3D11_TEXTURE2D_DESC old{}; if (fallback) fallback->GetDesc(&old);
        if (!fallback || old.Width != desc.Width || old.Height != desc.Height || old.Format != desc.Format) {
            fallbackView = nullptr; fallback = nullptr;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.CPUAccessFlags = 0; desc.MiscFlags = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.ArraySize = 1; desc.MipLevels = 1;
            check_hresult(device->CreateTexture2D(&desc, nullptr, fallback.put()));
            check_hresult(device->CreateShaderResourceView(fallback.get(), nullptr, fallbackView.put()));
        }
        context->CopyResource(fallback.get(), texture.get());
        counters.copiedBytes += static_cast<uint64_t>(desc.Width) * desc.Height * (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4);
        return fallbackView;
    }
    bool read(std::vector<Pixel>& pixels) {
        bool ready = receive(pixels);
        if (pending) return ready; // Never wait for or overwrite in-flight GPU work.
        auto texture = acquire(); if (!texture) return ready;
        submit(texture);
        return ready;
    }
    void submit(com_ptr<ID3D11Texture2D> texture) {
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc); format = desc.Format;
        bool fp16 = format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!fp16 && format != DXGI_FORMAT_B8G8R8A8_UNORM) throw std::runtime_error("Unexpected capture pixel format.");
        if (hdrDisplay && !fp16) throw std::runtime_error("Driver returned SDR for an HDR display. Select compatibility capture.");
        UINT sampleWidth = pool ? static_cast<UINT>(size.Width) : desc.Width;
        UINT sampleHeight = pool ? static_cast<UINT>(size.Height) : desc.Height;
        if (sampleWidth > desc.Width || sampleHeight > desc.Height) throw std::runtime_error("Capture dimensions changed; restart capture.");
        context->Begin(gpuDisjoint.get()); context->End(gpuStart.get());
        auto view = viewFor(texture, desc);
        const UINT values[]{sampleWidth, sampleHeight, rotation, fp16 ? 0u : 1u};
        context->UpdateSubresource(constants.get(), 0, nullptr, values, 0, 0);
        auto s = view.get(); auto u = uav.get(); auto b = constants.get();
        context->CSSetShader(shader.get(), nullptr, 0); context->CSSetConstantBuffers(0, 1, &b);
        context->CSSetShaderResources(0, 1, &s); context->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
        context->Dispatch(8, 9, 1);
        ID3D11ShaderResourceView* noSrv = nullptr; ID3D11UnorderedAccessView* noUav = nullptr;
        context->CSSetShaderResources(0, 1, &noSrv); context->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
        context->CopyResource(staging.get(), sampled.get());
        context->End(gpuEnd.get()); context->End(gpuDisjoint.get()); context->Flush();
        pending = true; ++counters.submitted;
        // Some drivers defer staging preparation until the first Map. Start it now,
        // without waiting, so the next 30 Hz tick can normally consume the result.
        D3D11_MAPPED_SUBRESOURCE preflight{};
        HRESULT hr = context->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &preflight);
        if (SUCCEEDED(hr)) context->Unmap(staging.get(), 0);
        else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) check_hresult(hr);
    }
};
Capture::Capture(HMONITOR monitor, CaptureBackend backend) : impl_(std::make_unique<Impl>(monitor, backend)) {}
Capture::~Capture() = default;
bool Capture::read(std::vector<Pixel>& pixels) { return impl_->read(pixels); }
CaptureStats Capture::stats() const { return impl_->counters; }
void Capture::testGpuPipeline() {
    Impl gpu(nullptr, CaptureBackend::Borderless);
    auto run = [&](DXGI_FORMAT format, const void* data, UINT stride) {
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = Width; desc.Height = Height;
        desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{data, stride, 0}; com_ptr<ID3D11Texture2D> texture;
        check_hresult(gpu.device->CreateTexture2D(&desc, &initial, texture.put()));
        gpu.submit(texture);
        std::vector<Pixel> pixels;
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!gpu.receive(pixels)) {
            if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("GPU test timed out");
            Sleep(1);
        }
        return sampleZones(pixels, Width, Height, false);
    };
    std::vector<uint16_t> hdr(Width * Height * 4);
    for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
        int i = (y * Width + x) * 4;
        // Actual FP16 source: bottom-left 8 red, top-left 2 green,
        // bottom-right 4 blue, top-right (1,1,1).
        if (x < Width / 2) hdr[i + (y < Height / 2 ? 1 : 0)] = y < Height / 2 ? 0x4000 : 0x4800;
        else if (y >= Height / 2) hdr[i + 2] = 0x4400;
        else hdr[i] = hdr[i + 1] = hdr[i + 2] = 0x3c00;
        hdr[i + 3] = 0x3c00;
    }
    auto zones = run(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr.data(), Width * 8);
    auto approximately = [](float a, float b) { return std::abs(a - b) < .002f; };
    if (!approximately(zones[0].r, 8) || !approximately(zones[1].g, 2) || !approximately(zones[2].b, 4) || !approximately(zones[3].r, 1))
        throw std::runtime_error("FP16 GPU capture lost HDR values or quadrant mapping");
    std::vector<uint32_t> sdr(Width * Height, 0xff808080);
    zones = run(DXGI_FORMAT_B8G8R8A8_UNORM, sdr.data(), Width * 4);
    if (!approximately(zones[0].r, .21586f) || !approximately(zones[0].g, .21586f) || !approximately(zones[0].b, .21586f))
        throw std::runtime_error("SDR GPU samples were not decoded to linear light");
    // Non-square fixtures verify all four orientation branches of the real shader.
    gpu.rotation = DXGI_MODE_ROTATION_ROTATE180;
    zones = run(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr.data(), Width * 8);
    if (!approximately(zones[0].r, 1) || !approximately(zones[3].r, 8)) throw std::runtime_error("180-degree GPU rotation failed");
    gpu.rotation = DXGI_MODE_ROTATION_ROTATE90;
    zones = run(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr.data(), Width * 8);
    if (!approximately(zones[0].b, 4) || !approximately(zones[1].r, 8)) throw std::runtime_error("90-degree GPU rotation failed");
    gpu.rotation = DXGI_MODE_ROTATION_ROTATE270;
    zones = run(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr.data(), Width * 8);
    if (!approximately(zones[0].g, 2) || !approximately(zones[3].b, 4)) throw std::runtime_error("270-degree GPU rotation failed");
}
std::wstring Capture::description() const {
    return std::wstring(impl_->duplication ? L"Borderless DXGI | " : impl_->borderlessWgc ? L"Borderless WGC | " : L"Compatibility WGC | ") +
        std::to_wstring(impl_->size.Width) + L" x " + std::to_wstring(impl_->size.Height) +
        (impl_->format == DXGI_FORMAT_B8G8R8A8_UNORM ? L" | SDR to linear" : L" | FP16 scRGB") + impl_->borderStatus;
}
}
