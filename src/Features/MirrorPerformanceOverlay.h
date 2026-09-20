#pragma once
struct IDXGISwapChain;

namespace MirrorPerformanceOverlay
{
    
    void Draw(IDXGISwapChain* chain, bool visible) noexcept;
}
