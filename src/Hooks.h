#pragma once

namespace Hooks
{
	struct BSShader_BeginTechnique
	{

		static bool thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t hullDescriptor, uint32_t domainDescriptor, uint32_t pixelDescriptor, void* outputStruct);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct BSGraphics_SetDirtyStates
	{

		static void thunk(bool isCompute, bool preserveInputLayout);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately1
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Install();
	void InstallEarlyHooks();
	void InstallD3DHooks();  

	void ServiceMirrorToggle() noexcept;

	[[nodiscard]] bool WorldRenderFrameActive() noexcept;

}
