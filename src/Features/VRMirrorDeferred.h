#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

namespace VRMirrorDeferred
{
	inline constexpr std::uint32_t kMaterialMode = 0x19;
	inline constexpr std::size_t kMaximumLights = 16;
	struct alignas(16) Light
	{
		DirectX::XMFLOAT4 positionRadius{}; 
		DirectX::XMFLOAT4 color{};          
	};
	struct alignas(16) Constants
	{
		DirectX::XMFLOAT4X4 inverseViewProjection[2]{};
		DirectX::XMFLOAT4X4 normalToWorld{}; 
		DirectX::XMFLOAT4 eyeOrigin[2]{};
		DirectX::XMFLOAT4 ambientRows[3]{};  
		DirectX::XMUINT4 extentAndLights{};  
		std::array<Light, kMaximumLights> lights{};
	};
	static_assert(sizeof(Constants) == 800);

	class Resources
	{
		template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	public:
		bool Ensure(ID3D11Device* device, std::uint32_t width, std::uint32_t height) noexcept
		{
			if (device_.Get() == device && width_ == width && height_ == height && constants_ &&
				rtvs_[0] && rtvs_[1] && srvs_[0] && srvs_[1])
				return true;
			Reset();
			if (!device || !width || !height)
				return false;
			device_ = device;
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = width; texture.Height = height;
			texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			for (std::size_t index = 0; index < rtvs_.size(); ++index) {
				ComPtr<ID3D11Texture2D> resource;
				if (FAILED(device->CreateTexture2D(&texture, nullptr, resource.GetAddressOf())) ||
					FAILED(device->CreateRenderTargetView(resource.Get(), nullptr, rtvs_[index].GetAddressOf())) ||
					FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, srvs_[index].GetAddressOf()))) {
					Reset(); return false;
				}
			}
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = sizeof(Constants);
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf()))) {
				Reset(); return false;
			}
			width_ = width; height_ = height;
			return true;
		}
		void Reset() noexcept { rtvs_ = {}; srvs_ = {}; constants_.Reset(); device_.Reset(); width_ = height_ = 0; }
		void Clear(ID3D11DeviceContext* context) noexcept
		{
			constexpr float black[4]{};
			for (const auto& view : rtvs_)
				context->ClearRenderTargetView(view.Get(), black);
		}
		void Bind(ID3D11DeviceContext* context, ID3D11RenderTargetView* diffuse,
			ID3D11RenderTargetView* normal, ID3D11RenderTargetView* flags, ID3D11DepthStencilView* depth) noexcept
		{
			ID3D11RenderTargetView* views[]{ diffuse, normal, flags, rtvs_[0].Get(), rtvs_[1].Get() };
			context->OMSetRenderTargets(5, views, depth);
		}
		bool Resolve(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, const Constants& data,
			ID3D11ShaderResourceView* diffuse, ID3D11ShaderResourceView* normal,
			ID3D11ShaderResourceView* flags, ID3D11ShaderResourceView* depth,
			ID3D11UnorderedAccessView* output) noexcept
		{
			if (!context || !shader || !constants_ || !diffuse || !normal || !flags || !depth || !output ||
				data.extentAndLights.x * 2u != width_ || data.extentAndLights.y != height_ ||
				data.extentAndLights.z > kMaximumLights)
				return false;

			ComPtr<ID3D11ComputeShader> previousShader;
			std::array<ID3D11ClassInstance*, 256> instances{};
			UINT instanceCount = static_cast<UINT>(instances.size());
			context->CSGetShader(previousShader.GetAddressOf(), instances.data(), &instanceCount);
			std::array<ID3D11ShaderResourceView*, 6> previousResources{};
			context->CSGetShaderResources(0, 6, previousResources.data());
			ComPtr<ID3D11Buffer> previousConstants;
			ComPtr<ID3D11UnorderedAccessView> previousOutput;
			context->CSGetConstantBuffers(0, 1, previousConstants.GetAddressOf());
			context->CSGetUnorderedAccessViews(0, 1, previousOutput.GetAddressOf());
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
			ID3D11ShaderResourceView* inputs[]{ diffuse, normal, flags, depth, srvs_[0].Get(), srvs_[1].Get() };
			auto* cb = constants_.Get();
			context->CSSetShader(shader, nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, 6, inputs);
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch((width_ + 7u) / 8u, (height_ + 7u) / 8u, 1);
			ID3D11UnorderedAccessView* nullOutput = nullptr;
			ID3D11ShaderResourceView* nullResources[6]{};
			context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
			context->CSSetShaderResources(0, 6, nullResources);
			context->CSSetShader(previousShader.Get(), instances.data(), instanceCount);
			context->CSSetConstantBuffers(0, 1, previousConstants.GetAddressOf());
			context->CSSetShaderResources(0, 6, previousResources.data());
			context->CSSetUnorderedAccessViews(0, 1, previousOutput.GetAddressOf(), nullptr);
			for (auto* view : previousResources) if (view) view->Release();
			for (UINT index = 0; index < instanceCount; ++index) if (instances[index]) instances[index]->Release();
			return true;
		}
	private:
		ComPtr<ID3D11Device> device_;
		std::array<ComPtr<ID3D11RenderTargetView>, 2> rtvs_{};
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> srvs_{};
		ComPtr<ID3D11Buffer> constants_;
		std::uint32_t width_{}, height_{};
	};
}
