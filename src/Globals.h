#pragma once

#include <atomic>
#include <d3d11.h>
#include <dxgi.h>

namespace globals
{
	namespace d3d
	{
		extern ID3D11Device* device;
		extern ID3D11DeviceContext* context;
		extern IDXGISwapChain* swapChain;
	}

	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	struct FrameBufferVR
	{
		Matrix CameraView[2];
		Matrix CameraProj[2];
		Matrix CameraViewProj[2];
		Matrix CameraViewProjUnjittered[2];
		Matrix CameraPreviousViewProjUnjittered[2];
		Matrix CameraProjUnjittered[2];
		Matrix CameraProjUnjitteredInverse[2];
		Matrix CameraViewInverse[2];
		Matrix CameraViewProjInverse[2];
		Matrix CameraProjInverse[2];
		float4 CameraPosAdjust[2];
		float4 CameraPreviousPosAdjust[2];
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	union FrameBufferCache
	{
		FrameBuffer nonVR;
		FrameBufferVR vr;

		const Matrix& GetCameraView(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraView[eyeIndex] : nonVR.CameraView;
		}
		const Matrix& GetCameraProj(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProj[eyeIndex] : nonVR.CameraProj;
		}
		const Matrix& GetCameraViewProj(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProj[eyeIndex] : nonVR.CameraViewProj;
		}
		const Matrix& GetCameraViewProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProjUnjittered[eyeIndex] : nonVR.CameraViewProjUnjittered;
		}
		const Matrix& GetCameraPreviousViewProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPreviousViewProjUnjittered[eyeIndex] : nonVR.CameraPreviousViewProjUnjittered;
		}
		const Matrix& GetCameraProjUnjittered(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjUnjittered[eyeIndex] : nonVR.CameraProjUnjittered;
		}
		const Matrix& GetCameraProjUnjitteredInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjUnjitteredInverse[eyeIndex] : nonVR.CameraProjUnjitteredInverse;
		}
		const Matrix& GetCameraViewInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewInverse[eyeIndex] : nonVR.CameraViewInverse;
		}
		const Matrix& GetCameraViewProjInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraViewProjInverse[eyeIndex] : nonVR.CameraViewProjInverse;
		}
		const Matrix& GetCameraProjInverse(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraProjInverse[eyeIndex] : nonVR.CameraProjInverse;
		}
		const float4& GetCameraPosAdjust(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPosAdjust[eyeIndex] : nonVR.CameraPosAdjust;
		}
		const float4& GetCameraPreviousPosAdjust(uint32_t eyeIndex = 0) const
		{
			return REL::Module::IsVR() ? vr.CameraPreviousPosAdjust[eyeIndex] : nonVR.CameraPreviousPosAdjust;
		}
		const float4& GetFrameParams() const
		{
			return REL::Module::IsVR() ? vr.FrameParams : nonVR.FrameParams;
		}
		const float4& GetDynamicResolutionParams1() const
		{
			return REL::Module::IsVR() ? vr.DynamicResolutionParams1 : nonVR.DynamicResolutionParams1;
		}
		const float4& GetDynamicResolutionParams2() const
		{
			return REL::Module::IsVR() ? vr.DynamicResolutionParams2 : nonVR.DynamicResolutionParams2;
		}
	};

	namespace game
	{
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern float* cameraNear;
		extern float* cameraFar;
		extern RE::Sky* sky;
		extern ID3D11Buffer* identifiedPerFrameBuffer;
		extern FrameBufferCache frameBufferCached;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
	}

	extern std::atomic<bool> gameDataReadyComplete;

	ID3D11PixelShader* GetAlbedoOverridePS(int kind);
	ID3D11PixelShader* GetProductionEyeAlbedoPS();
	ID3D11PixelShader* GetProductionFaceSkinTintPS();
	ID3D11PixelShader* GetProductionMirrorEyeAuthoredColorPS();
	ID3D11BlendState* GetProductionMirrorEyeAuthoredAlphaBlend();

	void OnInit();
	void ReInit();
	void OnDataLoaded();
	void UpdatePerFrame();
	void InstallD3DHooks(ID3D11DeviceContext* a_context);
}
