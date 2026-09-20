#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback = std::function<void()>;

	void RegisterPostDeferredPrePass(RenderHookCallback callback);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback);
	void RegisterPostDeferredComposite(RenderHookCallback callback);

	void RegisterPostForward(RenderHookCallback callback);

	void RegisterPostRenderPreUI(RenderHookCallback callback);
}
