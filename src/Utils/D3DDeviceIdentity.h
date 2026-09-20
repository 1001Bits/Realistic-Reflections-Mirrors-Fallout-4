#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace Util
{

	[[nodiscard]] inline Microsoft::WRL::ComPtr<ID3D11Device> CanonicalD3DDevice(
		IUnknown* object) noexcept
	{
		Microsoft::WRL::ComPtr<IUnknown> identity;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&identity))) && identity)
			(void)identity.As(&device);
		return device;
	}

	[[nodiscard]] inline Microsoft::WRL::ComPtr<ID3D11DeviceContext> CanonicalD3DContext(
		IUnknown* object) noexcept
	{
		Microsoft::WRL::ComPtr<IUnknown> identity;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&identity))) && identity)
			(void)identity.As(&context);
		return context;
	}

	namespace DeviceIdentityDetail
	{
		struct Family
		{
			Microsoft::WRL::ComPtr<ID3D11Device> engine;
			std::array<Microsoft::WRL::ComPtr<ID3D11Device>, 12> owners{};
			std::size_t count{};
			bool Contains(ID3D11Device* device) const noexcept
			{
				if (!device) return false;
				for (std::size_t i = 0; i < count; ++i)
					if (owners[i].Get() == device) return true;
				return false;
			}
			void Add(ID3D11Device* device) noexcept
			{
				if (device && !Contains(device) && count < owners.size()) owners[count++] = device;
			}
			void AddChild(ID3D11DeviceChild* child) noexcept
			{
				Microsoft::WRL::ComPtr<ID3D11Device> device;
				if (child) child->GetDevice(&device);
				Add(device.Get());
				Add(CanonicalD3DDevice(device.Get()).Get());
			}
		};
		inline std::atomic<std::shared_ptr<const Family>> current;
		inline std::mutex preparation;
	}

	inline void RegisterD3DDeviceIdentity(ID3D11Device* engine)
	{
		using namespace DeviceIdentityDetail;
		auto family = current.load(std::memory_order_acquire);
		if (family && family->engine.Get() == engine) return;
		std::lock_guard lock(preparation);
		family = current.load(std::memory_order_acquire);
		if (family && family->engine.Get() == engine) return;
		if (!engine) { current.store({}, std::memory_order_release); return; }
		auto next = std::make_shared<Family>();
		next->engine = engine;
		next->Add(engine);
		next->Add(CanonicalD3DDevice(engine).Get());
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
		engine->GetImmediateContext(&immediate);
		next->AddChild(immediate.Get());
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (SUCCEEDED(engine->CreateTexture2D(&desc, nullptr, &texture)) && texture) {
			next->AddChild(texture.Get());
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			if (SUCCEEDED(engine->CreateRenderTargetView(texture.Get(), nullptr, &rtv))) next->AddChild(rtv.Get());
			if (SUCCEEDED(engine->CreateShaderResourceView(texture.Get(), nullptr, &srv))) next->AddChild(srv.Get());
		}
		current.store(std::move(next), std::memory_order_release);
	}

	[[nodiscard]] inline bool D3DDevicesMatch(ID3D11Device* left, ID3D11Device* right) noexcept
	{
		if (!left || !right) return false;
		if (left == right) return true;
		const auto family = DeviceIdentityDetail::current.load(std::memory_order_acquire);
		if (family && family->Contains(left) && family->Contains(right)) return true;
		const auto canonicalLeft = CanonicalD3DDevice(left);
		const auto canonicalRight = CanonicalD3DDevice(right);
		return canonicalLeft && canonicalLeft.Get() == canonicalRight.Get();
	}

	[[nodiscard]] inline bool D3DChildUsesDevice(ID3D11DeviceChild* child, ID3D11Device* device) noexcept
	{
		if (!child || !device) return false;
		Microsoft::WRL::ComPtr<ID3D11Device> owner;
		child->GetDevice(&owner);
		return D3DDevicesMatch(owner.Get(), device);
	}
}
