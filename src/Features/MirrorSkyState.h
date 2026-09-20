#pragma once
#include <array>
#include <cstring>
#include <cmath>
#include <d3d11.h>
#include <wrl/client.h>

namespace MirrorSky
{

	struct TranslationLease
	{
		void* current{};
		void* previous{};
		std::array<float,3> savedCurrent{}, savedPrevious{};
		bool active{};
		bool Begin(void* world, void* previousWorld, const void* anchor, const void* previousAnchor,
			const void* eye) noexcept
		{
			if(active || !world || !previousWorld || !anchor || !previousAnchor || !eye) return false;
			std::array<float,3> origin{}, oldOrigin{}, reflected{}, next{}, oldNext{};
			std::memcpy(origin.data(),anchor,sizeof(origin));
			std::memcpy(oldOrigin.data(),previousAnchor,sizeof(oldOrigin));
			std::memcpy(reflected.data(),eye,sizeof(reflected));
			std::memcpy(savedCurrent.data(),world,sizeof(savedCurrent));
			std::memcpy(savedPrevious.data(),previousWorld,sizeof(savedPrevious));
			for(unsigned axis=0;axis<3;++axis) {
				next[axis]=savedCurrent[axis]-origin[axis]+reflected[axis];
				oldNext[axis]=savedPrevious[axis]-oldOrigin[axis]+reflected[axis];
				if(!std::isfinite(next[axis]) || !std::isfinite(oldNext[axis])) return false;
			}
			current=world; previous=previousWorld; active=true;
			std::memcpy(current,next.data(),sizeof(next));
			std::memcpy(previous,oldNext.data(),sizeof(oldNext));
			return true;
		}
		bool Restore() noexcept
		{
			if(!active) return true;
			std::memcpy(current,savedCurrent.data(),sizeof(savedCurrent));
			std::memcpy(previous,savedPrevious.data(),sizeof(savedPrevious));
			const bool exact=std::memcmp(current,savedCurrent.data(),sizeof(savedCurrent))==0 &&
				std::memcmp(previous,savedPrevious.data(),sizeof(savedPrevious))==0;
			*this={};
			return exact;
		}
	};

	struct Pipeline
	{
		ID3D11Device* device{};
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
		bool Ensure(ID3D11Device* current) noexcept
		{
			if (!current) return false;
			if (device != current) { depth.Reset(); rasterizer.Reset(); device = current; }
			if (!depth) {
				D3D11_DEPTH_STENCIL_DESC desc{};
				desc.DepthEnable = true;
				desc.DepthFunc = D3D11_COMPARISON_EQUAL;
				desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
				if (FAILED(device->CreateDepthStencilState(&desc, &depth))) return false;
			}
			if (!rasterizer) {
				D3D11_RASTERIZER_DESC desc{};
				desc.FillMode = D3D11_FILL_SOLID;
				desc.CullMode = D3D11_CULL_NONE;

				desc.DepthClipEnable = false;
				if (FAILED(device->CreateRasterizerState(&desc, &rasterizer))) return false;
			}
			return true;
		}
	};

	struct Lease
	{
		ID3D11DeviceContext* context{};
		const Pipeline* pipeline{};
		ID3D11DepthStencilState* savedDepth{};
		ID3D11RasterizerState* savedRasterizer{};
		UINT stencil{}, viewportCount{};
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		D3D11_VIEWPORT skyViewport{};
		bool active{};
		bool Begin(ID3D11DeviceContext* target, const Pipeline& state, unsigned width, unsigned height) noexcept
		{
			if (active || !target || !state.depth || !state.rasterizer || !width || !height) return false;
			context = target; pipeline = &state;
			context->OMGetDepthStencilState(&savedDepth, &stencil);
			context->RSGetState(&savedRasterizer);
			viewportCount = static_cast<UINT>(viewports.size());
			context->RSGetViewports(&viewportCount, viewports.data());
			skyViewport = {0,0,float(width),float(height),1,1};
			active = true;
			Apply();
			return true;
		}
		void Apply() const noexcept
		{
			if (!active) return;
			context->OMSetDepthStencilState(pipeline->depth.Get(), 0);
			context->RSSetState(pipeline->rasterizer.Get());

			context->RSSetViewports(1, &skyViewport);
		}
		bool Restore() noexcept
		{
			if (!active) return true;
			context->OMSetDepthStencilState(savedDepth, stencil);
			context->RSSetState(savedRasterizer);
			context->RSSetViewports(viewportCount, viewports.data());
			ID3D11DepthStencilState* actualDepth{}; ID3D11RasterizerState* actualRasterizer{}; UINT actualStencil{};
			context->OMGetDepthStencilState(&actualDepth, &actualStencil);
			context->RSGetState(&actualRasterizer);
			const bool exact = actualDepth == savedDepth && actualRasterizer == savedRasterizer && actualStencil == stencil;
			if (actualDepth) actualDepth->Release();
			if (actualRasterizer) actualRasterizer->Release();
			if (savedDepth) savedDepth->Release();
			if (savedRasterizer) savedRasterizer->Release();
			*this = {};
			return exact;
		}
	};
}
