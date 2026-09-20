#include "MirrorPerformanceOverlay.h"
#include "MirrorPerformance.h"
#include "MirrorSettings.h"
#include "MirrorSettingsPolicy.h"
#include "MirrorToggle.h"
#include "MirrorCaptureOptimizations.h"
#include <array>
#include "MirrorMenuRenderTarget.h"
#include <imgui.h>
#include <cstdio>
#include <imgui_impl_dx11.h>
#include <dxgi.h>

namespace MirrorPerformanceOverlay
{
    namespace
    {
        struct Renderer
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
            ImGuiContext* imgui{};
            MirrorMenuRenderTarget::Scope targetScope;
            ~Renderer() {Reset();}
            void Reset() noexcept
            {
                if(imgui) {
                    auto* previous=ImGui::GetCurrentContext();
                    ImGui::SetCurrentContext(imgui);
                    if(ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
                    ImGui::DestroyContext(imgui);
                    ImGui::SetCurrentContext(previous==imgui?nullptr:previous);
                    imgui=nullptr;
                }
                context.Reset();device.Reset();
            }
            bool Prepare(IDXGISwapChain* chain)
            {
                Microsoft::WRL::ComPtr<ID3D11Device> current;
                if(FAILED(chain->GetDevice(IID_PPV_ARGS(&current)))) return false;
                if(current.Get()!=device.Get()) Reset();
                if(imgui) {ImGui::SetCurrentContext(imgui);return true;}
                device=current;device->GetImmediateContext(&context);
                imgui=ImGui::CreateContext();
                if(!imgui) return false;
                ImGui::SetCurrentContext(imgui);
                auto& io=ImGui::GetIO();
                io.IniFilename=nullptr;io.LogFilename=nullptr;
                ImGui::StyleColorsDark();
                if(!context || !ImGui_ImplDX11_Init(device.Get(),context.Get())) {Reset();return false;}
                return true;
            }
        };
    }

    void Draw(IDXGISwapChain* chain, bool visible) noexcept
    {
        if(!chain || !visible) return;

        static Renderer renderer;
        auto* previous=ImGui::GetCurrentContext();
        try {
            if(renderer.Prepare(chain)) {
                Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
                Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
                if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))) &&
                    SUCCEEDED(renderer.device->CreateRenderTargetView(buffer.Get(),nullptr,&target))) {
                    D3D11_TEXTURE2D_DESC description{};buffer->GetDesc(&description);
                    auto& io=ImGui::GetIO();
                    io.DisplaySize=ImVec2(float(description.Width),float(description.Height));
                    io.DisplayFramebufferScale=ImVec2(1,1);
                    const auto stats=MirrorPerformance::GetOverlaySnapshot();
                    io.DeltaTime=stats.valid?float(stats.frameMs*.001):1.f/60.f;
                    if(renderer.targetScope.Begin(renderer.context.Get(),target.Get())) {
                        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
                        ImGui::SetNextWindowPos(ImVec2(18,18),ImGuiCond_Always);
                        ImGui::SetNextWindowBgAlpha(.90f);
                        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
                            ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing;
                        ImGui::Begin("Realistic Reflections - Mirrors performance",nullptr,flags);

                        ImGui::TextUnformatted("REALISTIC REFLECTIONS - MIRRORS - PERFORMANCE");
                        if(stats.valid) ImGui::Text("FPS: %.1f  |  %.2f ms/frame",stats.fps,stats.frameMs);
                        else ImGui::TextUnformatted("Measuring...");
                        ImGui::Text("Mirror rendering: %s",stats.mirrorsOn?"ON":"OFF");
                        for(unsigned i=0;i<2;++i) {
                            const auto average=stats.Mode(i==0);
                            
                            if(average.frames) ImGui::Text("%s average: %6.1f FPS  |  %6.2f ms  |  95%%: %5.1f  99%%: %5.1f ms  (%4.1fs)",
                                i==0?" ON":"OFF",average.FPS(),average.Milliseconds(),average.p95Ms,average.p99Ms,average.Seconds());
                            else ImGui::Text("%s average: waiting for settled frames",i==0?" ON":"OFF");
                        }
                        {
                            const auto on=stats.Mode(true),off=stats.Mode(false);
                            if(on.Seconds()>=2&&off.Seconds()>=2)
                                ImGui::Text("ON minus OFF: %+.2f ms/frame  |  99%%: %+.1f ms  |  %+.1f FPS",
                                    on.Milliseconds()-off.Milliseconds(),on.p99Ms-off.p99Ms,on.FPS()-off.FPS());
                            else ImGui::TextUnformatted("Measure both modes for at least 2 seconds");
                        }
                        ImGui::Separator();

                        const auto work=MirrorPerformance::GetBreakdown();
                        if(!work.valid) ImGui::TextUnformatted("Mirror work: measuring...");
                        else if(work.capturesPerSecond<=0) ImGui::TextUnformatted("Mirror captures: none (reflections off or no mirror in view)");
                        else {
                            char gpu[48];
                            if(work.gpu>=0) std::snprintf(gpu,sizeof(gpu),"%.2f ms (sampled)",work.gpu);
                            else std::snprintf(gpu,sizeof(gpu),"no sample yet");
                            ImGui::Text("Captures: %.1f/s  |  CPU %.2f ms each  |  GPU %s",work.capturesPerSecond,work.capture,gpu);
                            ImGui::Text("  shadows %.2f  |  culling %.2f  |  drawing %.2f  |  other %.2f ms",
                                work.shadows,work.culling,work.drawing,work.other);
                            if(work.lightingGpu>=0) ImGui::Text("  lighting pass: CPU %.2f ms  |  GPU %.2f ms (sampled)",work.lighting,work.lightingGpu);
                            if(work.detailed) ImGui::Text("  our draw hooks: %.2f ms over %.0f draw commits (inside drawing)",
                                work.ownDrawWork,work.drawCommits);
                        }
                        if(work.valid) {
                            ImGui::Text("Per frame: mirror CPU %.2f ms  |  composite %.2f ms  |  our hooks in the main view %.2f ms",
                                work.servicePerFrame,work.compositePerFrame,work.mainViewOwnWorkPerFrame);
                            if(work.movingShare>=0) ImGui::Text("Camera moving: %.0f%% of frames",
                                100.0*work.movingShare);
                        }
                        ImGui::Text("Optimisations (F7): %s",MirrorCaptureOptimizations::master.load()?"ON":"OFF");
                        if(MirrorPerformance::BenchmarkShowing()) {
                            ImGui::Separator();
                            std::array<MirrorPerformance::BenchmarkLine,20> lines{};
                            const auto count=MirrorPerformance::BenchmarkLines(lines.data(),lines.size());
                            for(std::size_t i=0;i<count;++i) ImGui::TextUnformatted(lines[i].data());
                        }
                        ImGui::Separator();
                        ImGui::TextUnformatted("F7 optimisations  |  F8 benchmark  |  F11 overlay  |  F12 reflections ON/OFF");
                        ImGui::TextUnformatted("Keep the same view; switching modes resets that mode's average.");
                        ImGui::TextUnformatted("VSync / FPS caps can hide the rendering cost.");
                        ImGui::End();ImGui::Render();
                        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                        renderer.targetScope.End();
                    }
                }

            }
        } catch(...) {
            renderer.targetScope.End();
            renderer.Reset();
        }
        ImGui::SetCurrentContext(previous);
    }
}
