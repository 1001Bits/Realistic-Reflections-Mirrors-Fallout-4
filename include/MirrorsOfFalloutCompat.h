#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <REX/W32/D3D11_1.h>

namespace RE
{
	
	struct RENDER_TARGETS
	{
		enum RENDER_TARGET : std::uint32_t
		{
			kFRAMEBUFFER = 0,

			kMOTION_VECTOR = 29,
			kTOTAL = 101,
			kVRTOTAL = 101
		};
	};

	struct RENDER_TARGETS_DEPTHSTENCIL
	{
		enum RENDER_TARGET_DEPTHSTENCIL : std::uint32_t
		{
			kTOTAL = 13,
			kVRTOTAL = 13
		};
	};

	using FormID = std::uint32_t;

	namespace BSShaderManager
	{
		struct State
		{};
	}

	class TES
	{
	public:
		[[nodiscard]] static TES* GetSingleton() noexcept { return nullptr; }
	};

	namespace BSGraphics
	{
		
		enum class ShaderFlags : std::uint32_t
		{};

		struct ShadowStateData
		{
			std::uint32_t renderTargets[8]{};
			std::uint32_t setRenderTargetMode[8]{};
			std::uint32_t cubeMapRenderTarget{};
			std::uint32_t depthStencilDepthMode{};
			std::uint32_t alphaBlendMode{};
			std::uint32_t alphaBlendWriteMode{};
			std::uint32_t rasterStateCullMode{};
			std::uint32_t stateUpdateFlags{};
			PixelShader* currentPixelShader{};
			VertexShader* currentVertexShader{};
		};

		inline ShadowStateData& RendererShadowState::GetRuntimeData()
		{
			return reinterpret_cast<ShadowStateData&>(*this);
		}

		inline const ShadowStateData& RendererShadowState::GetRuntimeData() const
		{
			return reinterpret_cast<const ShadowStateData&>(*this);
		}

		inline ShadowStateData& RendererShadowState::GetVRRuntimeData()
		{
			return GetRuntimeData();
		}

		inline const ShadowStateData& RendererShadowState::GetVRRuntimeData() const
		{
			return GetRuntimeData();
		}
	}

	class NiLight : public NiAVObject
	{};

	class ShadowSceneNode : public NiNode
	{};

	class NiPointLight : public NiLight
	{};

	class NiDirectionalLight : public NiLight
	{};

	class BSLight : public NiRefObject
	{};

	class BSRenderPass
	{
	public:
		BSRenderPass* next{};                   
		BSShader* shader{};                     
		union
		{
			BSShaderProperty* shaderProperty;     
			BSShaderProperty* property;
		};
		NiAVObject* geometry{};                 
		void* pad20[3]{};                       
		BSRenderPass* prevSibling{};            
		BSRenderPass* nextSibling{};            
		std::uint32_t technique{};              
		std::uint16_t pad4C{};                  
		std::uint8_t flags{};                   
		std::uint8_t numLights{};               
		std::uint32_t accumulationHint{};        
		std::uint32_t pad54{};                  
	};
	static_assert(sizeof(BSRenderPass) == 0x58);

	class BSShaderAccumulator;

	namespace BSGraphics
	{
		using BSShaderAccumulator = RE::BSShaderAccumulator;
	}
}
