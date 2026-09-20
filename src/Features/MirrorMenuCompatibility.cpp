#include "PCH.h"
#include "MirrorMenuCompatibility.h"
#include "MirrorMenuRenderTarget.h"
#include "MirrorSettings.h"
#include "MirrorFrameworkMenu.h"
#include "Globals.h"

namespace
{
    
    enum class FrameworkEvent : int { None = 0, Open = 1, Close = 2, BeforeRender = 3, AfterRender = 4 };
    using FrameworkCallback = void (__stdcall*)(FrameworkEvent);
    using RegisterEvent = std::int64_t (*)(FrameworkCallback);
    using IsBlockingWindowOpen = bool (*)();
    IsBlockingWindowOpen isBlockingWindowOpen{};

    void __stdcall OnFrameworkEvent(FrameworkEvent event)
    {
        if (event == FrameworkEvent::Open) {
            MirrorFrameworkMenu::Open();
            return;
        }
        if (event == FrameworkEvent::Close) {
            MirrorFrameworkMenu::Close();
            
            if (const auto* tasks = F4SE::GetTaskInterface()) tasks->AddTask([]() { MirrorSettings::Reload(); });
            return;
        }

        if (event != FrameworkEvent::BeforeRender && event != FrameworkEvent::AfterRender) return;
        thread_local MirrorMenuRenderTarget::Scope scope;
        if (event == FrameworkEvent::AfterRender) { scope.End(); return; }
        if (!scope.Active() && (!isBlockingWindowOpen || !isBlockingWindowOpen())) return;

        auto* renderer = globals::game::renderer;

        auto* screen = renderer ? reinterpret_cast<ID3D11RenderTargetView*>(
            renderer->data.renderWindow[0].swapChainRenderTarget.rtView) : nullptr;
        const bool active = scope.Begin(globals::d3d::context, screen);
        static bool reportedActive = false, reportedUnavailable = false;
        if (active && !reportedActive) {
            reportedActive = true;
            logger::info("[Mirrors Menu Compatibility] framework menu draw uses native screen target {}; full D3D state restored after draw",
                static_cast<void*>(screen));
        } else if (!active && !reportedUnavailable) {
            reportedUnavailable = true;
            logger::warn("[Mirrors Menu Compatibility] screen target or D3D11.1 context state unavailable; framework draw left unchanged");
        }
    }
}

void MirrorMenuCompatibility::Install()
{
    static bool attempted = false;
    if (attempted || REL::Module::IsVR()) return;
    attempted = true;
    const auto framework = GetModuleHandleW(L"F4SEMenuFramework.dll");
    if (!framework) return;
    const auto registerEvent = reinterpret_cast<RegisterEvent>(GetProcAddress(framework, "RegisterEvent"));
    isBlockingWindowOpen = reinterpret_cast<IsBlockingWindowOpen>(GetProcAddress(framework, "IsAnyBlockingWindowOpened"));
    if (!registerEvent || !isBlockingWindowOpen) {
        logger::warn("[Mirrors Menu Compatibility] framework render-event API unavailable; no menu adapter installed");
        return;
    }
    const auto listener = registerEvent(OnFrameworkEvent);
    if (listener <= 0) {
        logger::warn("[Mirrors Menu Compatibility] framework rejected render-event registration");
        return;
    }
    MirrorFrameworkMenu::Install(framework);
    logger::info("[Mirrors Menu Compatibility] framework render callbacks registered ({}); scoped screen-target correction enabled", listener);
}
