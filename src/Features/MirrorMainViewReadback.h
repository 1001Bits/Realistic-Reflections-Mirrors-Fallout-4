#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

class MirrorMainViewReadback
{
public:
	void Reset() noexcept { *this = {}; }

	void Capture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* source,
		std::uint64_t generation, std::uint64_t loadGeneration) noexcept
	{
		if (!device || !context || !source)
			return;
		D3D11_BUFFER_DESC description{};
		source->GetDesc(&description);
		if (description.ByteWidth < 36u * sizeof(DirectX::XMFLOAT4))
			return;
		if (device != deviceIdentity || loadGeneration != loadIdentity || description.ByteWidth != byteWidth) {
			Reset();
			deviceIdentity = device;
			loadIdentity = loadGeneration;
			byteWidth = description.ByteWidth;
		}
		for (auto& slot : slots) {
			if (!slot.pending)
				continue;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (context->Map(slot.buffer.Get(), 0, D3D11_MAP_READ,
					D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) != S_OK)
				continue;
			if (slot.generation > readyGeneration) {
				DirectX::XMFLOAT4X4 shaderRows{};
				const auto* bytes = static_cast<const std::byte*>(mapped.pData);
				std::memcpy(&shaderRows, bytes + 8u * sizeof(DirectX::XMFLOAT4), sizeof(shaderRows));
				std::memcpy(&eye, bytes + 35u * sizeof(DirectX::XMFLOAT4), sizeof(eye));
				
				DirectX::XMStoreFloat4x4(&viewProjection,
					DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&shaderRows)));
				readyGeneration = slot.generation;
			}
			context->Unmap(slot.buffer.Get(), 0);
			slot.pending = false;
		}
		for (auto& slot : slots) {
			if (slot.pending)
				continue;
			if (!slot.buffer) {
				D3D11_BUFFER_DESC staging{};
				staging.ByteWidth = byteWidth;
				staging.Usage = D3D11_USAGE_STAGING;
				staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(device->CreateBuffer(&staging, nullptr, slot.buffer.GetAddressOf())))
					return;
			}
			context->CopyResource(slot.buffer.Get(), source);
			slot.generation = generation;
			slot.pending = true;
			break;
		}
	}

	bool Read(std::uint64_t generation, std::uint64_t loadGeneration,
		DirectX::XMFLOAT4X4& matrix, DirectX::XMFLOAT3& origin) const noexcept
	{
		
		if (readyGeneration == 0 || loadGeneration != loadIdentity ||
			generation < readyGeneration || generation - readyGeneration > 4u)
			return false;
		matrix = viewProjection;
		origin = eye;
		return true;
	}

private:
	struct Slot
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		std::uint64_t generation{};
		bool pending{};
	};
	std::array<Slot, 3> slots{};
	ID3D11Device* deviceIdentity{};
	UINT byteWidth{};
	std::uint64_t loadIdentity{}, readyGeneration{};
	DirectX::XMFLOAT4X4 viewProjection{};
	DirectX::XMFLOAT3 eye{};
};
