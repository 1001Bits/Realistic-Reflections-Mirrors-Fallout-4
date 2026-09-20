#include "RenderHooks.h"

#include <vector>

#include "Features/ReflectionRuntime.h"

namespace cs::engine
{
	namespace
	{
		std::vector<RenderHookCallback> g_postDeferredPrePass;
		std::vector<RenderHookCallback> g_preDeferredLightsImpl;
		std::vector<RenderHookCallback> g_postDeferredLightsImpl;
		std::vector<RenderHookCallback> g_postDeferredComposite;
		std::vector<RenderHookCallback> g_postForward;
		std::vector<RenderHookCallback> g_postRenderPreUI;
		bool g_prePassInstalled = false;
		bool g_lightsImplInstalled = false;
		bool g_compositeInstalled = false;
		bool g_forwardInstalled = false;
		bool g_renderPreUIInstalled = false;

		std::uintptr_t ResolveDrawWorldAddress(
			std::uintptr_t ReflectionRuntime::Contract::* member,
			std::uint64_t flatID,
			std::uint64_t nextGenID) noexcept
		{
			const auto* runtime = ReflectionRuntime::Get();
			if (runtime)
				return ReflectionRuntime::Address(runtime->*member);
			if (REL::Module::IsNG()) {
				try {
					return REL::RelocationID(flatID, nextGenID).address();
				} catch (...) {
					return 0;
				}
			}
			return 0;
		}

		struct DeferredPrePass_Hook
		{
			static void thunk()
			{
				func();
				for (auto& cb : g_postDeferredPrePass) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredLightsImpl_Hook
		{
			static void thunk()
			{
				for (auto& cb : g_preDeferredLightsImpl) {
					cb();
				}
				func();
				for (auto& cb : g_postDeferredLightsImpl) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredComposite_Hook
		{
			static void thunk()
			{
				func();
				for (auto& cb : g_postDeferredComposite) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Forward_Hook
		{
			static void thunk()
			{
				func();  
				for (auto& cb : g_postForward) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RenderPreUI_Hook
		{
			static void thunk()
			{
				func();
				for (auto& cb : g_postRenderPreUI) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void RegisterPostDeferredPrePass(RenderHookCallback callback)
	{
		g_postDeferredPrePass.push_back(std::move(callback));
		if (!g_prePassInstalled) {
			const auto address = ResolveDrawWorldAddress(
				&ReflectionRuntime::Contract::drawWorldDeferredPrePass, 56596, 2318301);
			if (!address) {
				logger::error("[RenderHooks] DrawWorld::DeferredPrePass unsupported on this runtime");
				return;
			}
			if (!stl::detour_thunk<DeferredPrePass_Hook>(address))
				return;
			g_prePassInstalled = true;
			logger::info("[RenderHooks] Hook installed on DrawWorld::DeferredPrePass @ 0x{:X}", address);
		}
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback)
	{
		g_preDeferredLightsImpl.push_back(std::move(callback));
		if (!g_lightsImplInstalled) {
			const auto address = ResolveDrawWorldAddress(
				&ReflectionRuntime::Contract::drawWorldDeferredLightsImpl, 1108521, 2318312);
			if (!address) {
				logger::error("[RenderHooks] DrawWorld::DeferredLightsImpl unsupported on this runtime");
				return;
			}
			if (!stl::detour_thunk<DeferredLightsImpl_Hook>(address))
				return;
			g_lightsImplInstalled = true;
			logger::info("[RenderHooks] Hook installed on DrawWorld::DeferredLightsImpl @ 0x{:X}", address);
		}
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback)
	{
		g_postDeferredLightsImpl.push_back(std::move(callback));
		if (!g_lightsImplInstalled) {
			const auto address = ResolveDrawWorldAddress(
				&ReflectionRuntime::Contract::drawWorldDeferredLightsImpl, 1108521, 2318312);
			if (!address) {
				logger::error("[RenderHooks] DrawWorld::DeferredLightsImpl unsupported on this runtime");
				return;
			}
			if (!stl::detour_thunk<DeferredLightsImpl_Hook>(address))
				return;
			g_lightsImplInstalled = true;
			logger::info("[RenderHooks] Hook installed on DrawWorld::DeferredLightsImpl @ 0x{:X}", address);
		}
	}

	void RegisterPostDeferredComposite(RenderHookCallback callback)
	{
		g_postDeferredComposite.push_back(std::move(callback));
		if (!g_compositeInstalled) {
			const auto address = ResolveDrawWorldAddress(
				&ReflectionRuntime::Contract::drawWorldDeferredComposite, 728427, 2318313);
			if (!address) {
				logger::error("[RenderHooks] DrawWorld::DeferredComposite unsupported on this runtime");
				return;
			}
			if (!stl::detour_thunk<DeferredComposite_Hook>(address))
				return;
			g_compositeInstalled = true;
			logger::info("[RenderHooks] Hook installed on DrawWorld::DeferredComposite @ 0x{:X}", address);
		}
	}

	void RegisterPostForward(RenderHookCallback callback)
	{
		g_postForward.push_back(std::move(callback));
		if (!g_forwardInstalled) {
			const auto addr = ResolveDrawWorldAddress(
				&ReflectionRuntime::Contract::drawWorldForward, 656535, 0);
			if (addr) {
				if (!stl::detour_thunk<Forward_Hook>(addr))
					return;
				g_forwardInstalled = true;
				logger::info("[RenderHooks] Hook installed on DrawWorld::Forward @ 0x{:X}", addr);
			} else {
				logger::warn("[RenderHooks] DrawWorld::Forward address unresolved — real-sky capture unavailable");
			}
		}
	}

	void RegisterPostRenderPreUI(RenderHookCallback callback)
	{
		g_postRenderPreUI.push_back(std::move(callback));
		if (g_renderPreUIInstalled)
			return;
		const auto address = ResolveDrawWorldAddress(
			&ReflectionRuntime::Contract::drawWorldRenderPreUI, 0, 0);
		if (!address) {
			logger::error("[RenderHooks] DrawWorld::Render_PreUI unsupported on this runtime");
			stl::hookInstallationFailed.store(true, std::memory_order_release);
			return;
		}
		if (!stl::detour_thunk<RenderPreUI_Hook>(address))
			return;
		g_renderPreUIInstalled = true;
		logger::info("[RenderHooks] Hook installed on DrawWorld::Render_PreUI @ 0x{:X}", address);
	}
}
