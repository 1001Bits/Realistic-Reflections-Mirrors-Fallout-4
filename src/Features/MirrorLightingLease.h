#pragma once
#include <d3d11.h>
#include "MirrorNativeLighting.h"

struct MirrorLightingLease
{
	static constexpr unsigned Slot = 13;
	ID3D11DeviceContext* context{};
	ID3D11Buffer* original{};
	ID3D11Buffer* replacement{}; 
	bool active{};
	MirrorNativeLighting::Bindings native{}, originalNative{};
	UINT originalFirst{}, originalCount{};
	bool Apply() const noexcept
	{
		if (!active || !context || !replacement) return false;
		context->PSSetConstantBuffers(Slot, 1, &replacement);
		native.Bind(context,true);
		ID3D11Buffer* observed{};
		context->PSGetConstantBuffers(Slot, 1, &observed);
		const bool exact = observed == replacement;
		if (observed) observed->Release();
		return exact;
	}
	bool Begin(ID3D11DeviceContext* target, ID3D11Buffer* lighting,
		MirrorNativeLighting::Bindings captured={}) noexcept
	{
		if (active || !target || !lighting) return false;
		context = target;
		replacement = lighting;
		native=captured;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
		if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged)))) {
			ranged->PSGetConstantBuffers1(3,2,originalNative.buffers.data(),originalNative.first.data(),originalNative.count.data());
			ranged->PSGetConstantBuffers1(Slot,1,&original,&originalFirst,&originalCount);
		} else {
			context->PSGetConstantBuffers(3,2,originalNative.buffers.data());
			context->PSGetConstantBuffers(Slot,1,&original);
		}
		active = true;
		return Apply();
	}
	bool Restore() noexcept
	{
		if (!active) return true;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
		if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged))))
			ranged->PSSetConstantBuffers1(Slot,1,&original,&originalFirst,&originalCount);
		else context->PSSetConstantBuffers(Slot,1,&original);
		originalNative.Bind(context,true);
		for (auto* buffer:originalNative.buffers) if (buffer) buffer->Release();
		ID3D11Buffer* observed{};
		context->PSGetConstantBuffers(Slot, 1, &observed);
		const bool exact = observed == original;
		if (observed) observed->Release();
		if (original) original->Release();
		*this = {};
		return exact;
	}
};
