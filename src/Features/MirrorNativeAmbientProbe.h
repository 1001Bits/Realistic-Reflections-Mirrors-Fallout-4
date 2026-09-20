#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11_1.h>
#include <wrl/client.h>

namespace MirrorNativeAmbientProbe
{
	inline thread_local bool techniqueActive = false;
	inline thread_local std::uint32_t techniqueDescriptor = 0;
	inline void Technique(const char* name, std::uint32_t descriptor, bool accepted) noexcept
	{
		techniqueActive = accepted && name && std::strcmp(name, "DFLight") == 0 &&
			((descriptor & 0x00020000u) || descriptor == 0x04000000u);
		techniqueDescriptor = techniqueActive ? descriptor : 0;
	}
	inline unsigned ExposureBytes(DXGI_FORMAT format) noexcept
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: return 8;
		case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32_FLOAT: return 4;
		case DXGI_FORMAT_R16_FLOAT: return 2;
		default: return 0;
		}
	}

	struct Sample
	{
		std::array<std::array<float, 4>, 23> constants{};
		std::array<std::array<float, 4>, 3> directionalAmbient{};
		std::array<std::byte, 16> exposure{};
		std::uint64_t generation{}, tick{};
		std::uint32_t cell{}, descriptor{}, firstConstant{}, constantCount{}, bufferBytes{};
		DXGI_FORMAT exposureFormat{DXGI_FORMAT_UNKNOWN};
		bool exposureBound{}, exposureAvailable{};
	};

	class Readback
	{
	public:
		bool Pending() const noexcept { return pending_; }
		void Reset() noexcept { *this = {}; }

		bool Queue(ID3D11DeviceContext* context, Sample sample)
		{
			if (!context || pending_) return false;
			Microsoft::WRL::ComPtr<ID3D11Buffer> source;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
			UINT first = 0, count = 0;
			if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))
				context1->PSGetConstantBuffers1(2, 1, &source, &first, &count);
			else context->PSGetConstantBuffers(2, 1, &source);
			if (!source) return false;
			D3D11_BUFFER_DESC description{};
			source->GetDesc(&description);
			if (!context1) count = description.ByteWidth / 16;
			constexpr auto requiredBytes = sizeof(Sample::constants);
			if (description.ByteWidth > 1024u * 1024u || count < 23 ||
				std::uint64_t(first) * 16 + requiredBytes > description.ByteWidth) return false;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			if (device_.Get() != device.Get() || bytes_ != description.ByteWidth) {
				Reset();
				device_ = device;
				bytes_ = description.ByteWidth;
				description.Usage = D3D11_USAGE_STAGING;
				description.BindFlags = description.MiscFlags = description.StructureByteStride = 0;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(device->CreateBuffer(&description, nullptr, &constants_))) { Reset(); return false; }
				D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
				if (FAILED(device->CreateQuery(&query, &done_))) { Reset(); return false; }
			}
			sample.firstConstant = first;
			sample.constantCount = count;
			sample.bufferBytes = bytes_;
			sample.exposureAvailable = false;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> exposure;
			context->PSGetShaderResources(8, 1, &exposure);
			sample.exposureBound = exposure != nullptr;
			if (exposure) {
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				exposure->GetDesc(&view);
				sample.exposureFormat = view.Format;
				Microsoft::WRL::ComPtr<ID3D11Resource> resource;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				exposure->GetResource(&resource);
				if (view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && SUCCEEDED(resource.As(&texture))) {
					D3D11_TEXTURE2D_DESC desc{};
					texture->GetDesc(&desc);
					if (desc.SampleDesc.Count == 1 && ExposureBytes(view.Format)) {
						const auto mip = view.Texture2D.MostDetailedMip;
						desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
						desc.BindFlags = desc.MiscFlags = 0;
						desc.Usage = D3D11_USAGE_STAGING;
						desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						if (!exposure_ || exposureResourceFormat_ != desc.Format) {
							exposure_.Reset();
							exposureResourceFormat_ = desc.Format;
							(void)device->CreateTexture2D(&desc, nullptr, &exposure_);
						}
						if (exposure_) {
							const D3D11_BOX pixel{0, 0, 0, 1, 1, 1};
							context->CopySubresourceRegion(exposure_.Get(), 0, 0, 0, 0, texture.Get(), mip, &pixel);
							sample.exposureAvailable = true;
							sample.exposureFormat = view.Format;
						}
					}
				}
			}
			context->CopyResource(constants_.Get(), source.Get());
			context->End(done_.Get());
			sample_ = sample;
			pending_ = true;
			return true;
		}

		bool Poll(ID3D11DeviceContext* context, Sample& result)
		{
			if (!pending_ || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			if (device != device_) { Reset(); return false; }
			BOOL ready = FALSE;
			const auto status = context->GetData(done_.Get(), &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (FAILED(status)) { Reset(); return false; }
			if (status != S_OK || !ready) return false;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto mappedStatus = context->Map(constants_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (mappedStatus == DXGI_ERROR_WAS_STILL_DRAWING) return false;
			if (FAILED(mappedStatus)) { Reset(); return false; }
			std::memcpy(sample_.constants.data(), static_cast<const std::byte*>(mapped.pData) +
				std::size_t(sample_.firstConstant) * 16, sizeof(sample_.constants));
			context->Unmap(constants_.Get(), 0);
			if (sample_.exposureAvailable) {
				if (SUCCEEDED(context->Map(exposure_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
					std::memcpy(sample_.exposure.data(), mapped.pData, ExposureBytes(sample_.exposureFormat));
					context->Unmap(exposure_.Get(), 0);
				} else sample_.exposureAvailable = false;
			}
			result = sample_;
			pending_ = false;
			return true;
		}
	private:
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> exposure_;
		Microsoft::WRL::ComPtr<ID3D11Query> done_;
		DXGI_FORMAT exposureResourceFormat_{DXGI_FORMAT_UNKNOWN};
		Sample sample_{};
		UINT bytes_{};
		bool pending_{};
	};
}
