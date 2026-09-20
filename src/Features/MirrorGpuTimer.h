#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

class MirrorGpuTimer
{
public:
	static constexpr int Capacity = 12;
	struct Result { unsigned tag; bool valid; double milliseconds; };
	int Begin(ID3D11Device* device, ID3D11DeviceContext* context, unsigned tag) noexcept
	{
		if (!device || !context)
			return -1;
		if (device_.Get() != device) {
			slots_ = {};
			device_ = device;
			retryAfter_ = 0;
		}
		if (GetTickCount64() < retryAfter_)
			return -1;
		for (int i = 0; i < Capacity; ++i) {
			auto& slot = slots_[i];
			if (slot.pending || slot.active)
				continue;
			if (!slot.disjoint || !slot.start || !slot.end) {
				slot = {};
				const D3D11_QUERY_DESC disjoint{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
				const D3D11_QUERY_DESC timestamp{ D3D11_QUERY_TIMESTAMP, 0 };
				if (FAILED(device->CreateQuery(&disjoint, slot.disjoint.GetAddressOf())) ||
					FAILED(device->CreateQuery(&timestamp, slot.start.GetAddressOf())) ||
					FAILED(device->CreateQuery(&timestamp, slot.end.GetAddressOf()))) {
					slot = {};
					retryAfter_ = GetTickCount64() + 1000;
					return -1;
				}
			}
			slot.tag = tag;
			slot.discard = false;
			slot.active = true;
			context->Begin(slot.disjoint.Get());
			context->End(slot.start.Get());
			return i;
		}
		return -1;
	}
	void End(ID3D11DeviceContext* context, int token) noexcept
	{
		if (!context || token < 0 || token >= Capacity || !slots_[token].active)
			return;
		auto& slot = slots_[token];
		context->End(slot.end.Get());
		context->End(slot.disjoint.Get());
		slot.active = false;
		slot.pending = true;
	}

	void DiscardResults() noexcept
	{
		for (auto& slot : slots_)
			if (slot.active || slot.pending) slot.discard = true;
	}

	template <class Consumer>
	void Poll(ID3D11DeviceContext* context, Consumer&& consume) noexcept
	{
		if (!context)
			return;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		context->GetDevice(device.GetAddressOf());
		if (device.Get() != device_.Get()) {
			slots_ = {};
			device_.Reset();
			return;
		}
		for (auto& slot : slots_) {
			if (!slot.pending)
				continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
			std::uint64_t start{}, end{};
			const auto a = context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const auto b = context->GetData(slot.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const auto c = context->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const bool failed = FAILED(a) || FAILED(b) || FAILED(c);
			if (!failed && (a == S_FALSE || b == S_FALSE || c == S_FALSE))
				continue;
			slot.pending = false;
			const bool valid = !failed && !disjoint.Disjoint && disjoint.Frequency != 0 && end >= start;
			if (!slot.discard)
				consume(Result{ slot.tag, valid, valid ? 1000.0 * static_cast<double>(end - start) / static_cast<double>(disjoint.Frequency) : 0.0 });
		}
	}
private:
	struct Slot
	{
		Microsoft::WRL::ComPtr<ID3D11Query> disjoint, start, end;
		unsigned tag{};
		bool active{}, pending{}, discard{};
	};
	Microsoft::WRL::ComPtr<ID3D11Device> device_;
	std::array<Slot, Capacity> slots_{};
	std::uint64_t retryAfter_{};
};
