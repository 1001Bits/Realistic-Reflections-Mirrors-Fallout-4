#pragma once
#include <array>
#include <d3d11.h>
#include <wrl/client.h>
#include <utility>

namespace MirrorEffectState
{
	struct Pipeline
	{
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> readOnlyDepth;
		Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		ID3D11Device* device{};
		bool Ensure(ID3D11Device* current,ID3D11DepthStencilView* depth) noexcept
		{
			if(!current||!depth) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;depth->GetResource(&resource);
			if(device!=current) {readOnlyDepth.Reset();depthResource.Reset();depthState.Reset();device=current;}
			if(!depthState) {
				D3D11_DEPTH_STENCIL_DESC desc{};desc.DepthEnable=true;
				desc.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
				if(FAILED(current->CreateDepthStencilState(&desc,&depthState))) return false;
			}
			if(depthResource.Get()!=resource.Get()||!readOnlyDepth) {
				D3D11_DEPTH_STENCIL_VIEW_DESC desc{};depth->GetDesc(&desc);
				desc.Flags=D3D11_DSV_READ_ONLY_DEPTH;
				if(desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||desc.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
					desc.Flags|=D3D11_DSV_READ_ONLY_STENCIL;
				Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
				if(FAILED(current->CreateDepthStencilView(resource.Get(),&desc,&view))) return false;
				readOnlyDepth=std::move(view);depthResource=std::move(resource);
			}
			return true;
		}
	};

	struct Lease
	{
		ID3D11DeviceContext* context{};const Pipeline* pipeline{};
		ID3D11RenderTargetView* color{};ID3D11ShaderResourceView* depth{};
		ID3D11ShaderResourceView* savedDepthTexture{};
		ID3D11DepthStencilState* savedDepthState{};UINT savedStencil{};
		std::array<ID3D11RenderTargetView*,8> savedTargets{};
		ID3D11DepthStencilView* savedDepthView{};
		bool active{};
		bool Begin(ID3D11DeviceContext* target,const Pipeline& state,ID3D11RenderTargetView* colorTarget,
			ID3D11ShaderResourceView* depthTexture) noexcept
		{
			if(active||!target||!colorTarget||!depthTexture||!state.readOnlyDepth||!state.depthState) return false;
			context=target;pipeline=&state;color=colorTarget;depth=depthTexture;
			context->OMGetRenderTargets(static_cast<UINT>(savedTargets.size()),savedTargets.data(),&savedDepthView);
			context->OMGetDepthStencilState(&savedDepthState,&savedStencil);
			context->PSGetShaderResources(3,1,&savedDepthTexture);
			active=true;Apply();return true;
		}
		void Apply() const noexcept
		{
			if(!active) return;
			context->OMSetRenderTargets(1,&color,pipeline->readOnlyDepth.Get());
			context->OMSetDepthStencilState(pipeline->depthState.Get(),0);
			context->PSSetShaderResources(3,1,&depth);
		}
		bool Restore() noexcept
		{
			if(!active) return true;
			ID3D11ShaderResourceView* none{};context->PSSetShaderResources(3,1,&none);
			context->OMSetRenderTargets(static_cast<UINT>(savedTargets.size()),savedTargets.data(),savedDepthView);
			context->OMSetDepthStencilState(savedDepthState,savedStencil);
			context->PSSetShaderResources(3,1,&savedDepthTexture);
			ID3D11ShaderResourceView* actualTexture{};ID3D11DepthStencilState* actualState{};UINT actualStencil{};
			context->PSGetShaderResources(3,1,&actualTexture);context->OMGetDepthStencilState(&actualState,&actualStencil);
			bool exact=actualTexture==savedDepthTexture&&actualState==savedDepthState&&actualStencil==savedStencil;
			std::array<ID3D11RenderTargetView*,8> actualTargets{};ID3D11DepthStencilView* actualView{};
			context->OMGetRenderTargets(static_cast<UINT>(actualTargets.size()),actualTargets.data(),&actualView);
			exact=exact&&actualTargets==savedTargets&&actualView==savedDepthView;
			for(auto* item:actualTargets) if(item) item->Release();
			for(auto* item:savedTargets) if(item) item->Release();
			if(actualView) actualView->Release();if(savedDepthView) savedDepthView->Release();
			if(actualTexture) actualTexture->Release();if(actualState) actualState->Release();
			if(savedDepthTexture) savedDepthTexture->Release();if(savedDepthState) savedDepthState->Release();
			*this={};return exact;
		}
	};
}
