#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <DirectXMath.h>
#include "VRMirrorScene.h"
#include "MirrorLightFade.h"
#include "../../features/Mirrors of Fallout/Shaders/MirrorsOfFallout/MirrorLightingABI.hlsli"

namespace FlatMirrorLighting
{

	inline constexpr std::size_t kMaximumLights = MIRROR_MAX_LIGHTS;
	inline constexpr unsigned kABI = MIRROR_LIGHTING_ABI;

	struct Light { DirectX::XMFLOAT4 positionRadius{}, color{}, spot{ 0, 0, 0, -2 },
		falloff{ 0, 1, 2, 0 }; std::array<DirectX::XMFLOAT4,3> volume{}; };
	inline constexpr std::uint32_t kShapePoint = 2u, kShapeSpot = 6u;  
	struct alignas(16) Constants
	{

		DirectX::XMFLOAT4 contract{MIRROR_LIGHTING_ABI,0,0,0};
		DirectX::XMFLOAT4X4 inverseViewProjection{}, normalToWorld{}, previousViewProjection{}, previousInverseViewProjection{},
			viewProjection{};
		DirectX::XMFLOAT4 eye{}, previousEye{};
		std::array<DirectX::XMFLOAT4,3> ambient{};
		DirectX::XMUINT4 extentLightsHistory{};
		DirectX::XMFLOAT4 history{0,100,0.5f,0}; 
		std::array<Light,kMaximumLights> lights{};
	};
	static_assert(sizeof(Constants) == 29120);
	inline void ScaleSunlight(Constants& output, float scale) noexcept
	{
		for (unsigned i=0;i<output.extentLightsHistory.z;++i) {
			auto& light=output.lights[i];
			if (light.positionRadius.w==0) {
				light.color.x*=scale;light.color.y*=scale;light.color.z*=scale;
			}
		}
	}

	template<class T> T Read(const void* base, std::size_t offset) noexcept
	{
		T result{}; std::memcpy(&result, static_cast<const std::byte*>(base)+offset, sizeof(result)); return result;
	}
	inline bool Pointer(const void* p) noexcept { return reinterpret_cast<std::uintptr_t>(p)>0x10000; }

	inline bool ReadBox(const void* wrapper, const void* native, Light& output) noexcept
	{
		using namespace DirectX;
		const auto center=Read<XMFLOAT3>(wrapper,0xA0);
		const auto extents=Read<XMFLOAT3>(wrapper,0xAC);
		const std::array<float,3> halfExtents{extents.x,extents.y,extents.z};
		const auto scale=Read<float>(native,0xAC);
		if (!std::isfinite(scale) || scale<=0 || !std::isfinite(extents.x) || !std::isfinite(extents.y) ||
			!std::isfinite(extents.z) || extents.x<=0 || extents.y<=0 || extents.z<=0) return false;
		XMFLOAT4X4 transform{};
		for(unsigned axis=0;axis<3;++axis) {
			const auto rotation=Read<XMFLOAT3>(wrapper,0x60+16*axis);
			const float halfExtent=halfExtents[axis]*scale;
			transform.m[axis][0]=rotation.x*halfExtent;
			transform.m[axis][1]=rotation.y*halfExtent;
			transform.m[axis][2]=rotation.z*halfExtent;
		}
		transform._41=center.x; transform._42=center.y; transform._43=center.z; transform._44=1;
		XMVECTOR determinant{};
		const auto inverse=XMMatrixInverse(&determinant,XMLoadFloat4x4(&transform));
		const float det=XMVectorGetX(determinant);
		if (!std::isfinite(det) || std::fabs(det)<1.e-12f) return false;
		XMStoreFloat4x4(&transform,XMMatrixTranspose(inverse));
		for(unsigned axis=0;axis<3;++axis) {
			for(float v:transform.m[axis]) if(!std::isfinite(v)) return false;
			output.volume[axis]={transform.m[axis][0],transform.m[axis][1],transform.m[axis][2],transform.m[axis][3]};
		}
		output.falloff.w=1;
		return true;
	}

	inline bool Ambient(const float (&colors)[18], Constants& output) noexcept
	{
		for(float value:colors) if(!std::isfinite(value)) return false;
		for(unsigned channel=0;channel<3;++channel) {
			output.ambient[channel] = {(colors[3+channel]-colors[channel])*.5f,
				(colors[9+channel]-colors[6+channel])*.5f,
				(colors[15+channel]-colors[12+channel])*.5f,
				(colors[3+channel]+colors[channel])*.5f};
		}
		return true;
	}
	
	enum Reject : std::uint8_t { kAccepted=0, kNoWrapper=1, kType=2, kFlag17E=3, kAppCulled=4, kFade=5, kColor=6,
		kFrustum=7, kScore=8, kBudget=9, kAbsent=10 };
	struct Diagnostics
	{
		std::array<const void*,kMaximumLights> previous{};   
		std::array<const void*,8> dropped{};      
		std::array<std::uint8_t,8> reasons{};
		std::array<DirectX::XMFLOAT4,8> droppedPositions{};
		unsigned droppedCount{}, visited{}, accepted{};
		
		unsigned frustumRejected{}, budgetRejected{}, scoreRejected{};
		unsigned flagRejected{}, fadeRejected{}, otherRejected{};   

		std::array<unsigned,11> rejectReasons{};

		std::array<unsigned,8> rejectedShapes{};
		
		unsigned duplicateSkipped{};

		unsigned previsFlagged{}, mainViewCulled{};
		float engineFadeSum{};
		
		struct ShapeSample
		{
			std::uint32_t shape{};
			const void* vtable{};
			DirectX::XMFLOAT4 positionRadius{};
			DirectX::XMFLOAT3 diffuse{};
		};
		std::array<ShapeSample,4> shapeSamples{};
		unsigned shapeSampleCount{};
		std::array<float,kMaximumLights> fades{};   
	};
	
	inline bool ReadLight(const void* wrapper, bool sun, Light& output, std::uint8_t* reason=nullptr,
		float* fadeOut=nullptr) noexcept
	{
		auto fail=[&](std::uint8_t why){ if(reason) *reason=why; return false; };
		if(!Pointer(wrapper)) return fail(kNoWrapper);
		const auto* native=Read<const void*>(wrapper,0xB8);
		if(!Pointer(native)) return fail(kNoWrapper);

		const auto shape=Read<std::uint32_t>(wrapper,0x180);
		if(Read<std::uint8_t>(wrapper,0x17E)) return fail(kFlag17E);
		
		if(!sun && MirrorLightFade::g_rejectPrevisCulled &&
			!MirrorLightFade::PrevisAllows(native,Read<std::uint8_t>(wrapper,0x17C)!=0)) return fail(kFlag17E);
		if(Read<std::uint32_t>(native,0x108)&1u) return fail(kAppCulled);
		const auto diffuse=Read<DirectX::XMFLOAT3>(native,0x12C);
		
		const float visibility=Read<float>(wrapper,0x10);
		const bool culledByMainView=Read<std::uint32_t>(wrapper,0x18)==MirrorLightFade::kCulledMarker;
		const float fade=Read<float>(native,0x144)*
			(sun || !std::isfinite(visibility) ? visibility :
			 (MirrorLightFade::g_useEngineFade ? MirrorLightFade::Resolve(native,visibility,culledByMainView) : 1.0f));
		if(!std::isfinite(fade) || fade<=0) return fail(kFade);
		if(fadeOut) *fadeOut=fade;
		if(!std::isfinite(diffuse.x) || !std::isfinite(diffuse.y) ||
			!std::isfinite(diffuse.z) || diffuse.x<0 || diffuse.y<0 || diffuse.z<0) return fail(kColor);
		output.color={std::pow(diffuse.x,2.2f)*fade,std::pow(diffuse.y,2.2f)*fade,std::pow(diffuse.z,2.2f)*fade,0};
		if(!std::isfinite(output.color.x)||!std::isfinite(output.color.y)||!std::isfinite(output.color.z)) return false;
		output.spot={0,0,0,-2};
		output.volume={};
		output.falloff={0,1,2,0};
		if(sun) {
			const auto direction=Read<DirectX::XMFLOAT3>(native,0x170);
			const auto length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
			if(!std::isfinite(length)||length<1.e-6f) return false;
			output.positionRadius={-direction.x/length,-direction.y/length,-direction.z/length,0};
		} else {
			const auto position=Read<DirectX::XMFLOAT3>(native,0xA0);
			const float radius=Read<float>(native,0x138);
			if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)||
				!std::isfinite(radius)||radius<=0||radius>1.e6f) return fail(kType);
			output.positionRadius={position.x,position.y,position.z,radius};

			{
				const float bias=Read<float>(native,0x170), scale=Read<float>(native,0x174),
					exponent=Read<float>(native,0x178);
				const bool usable=std::isfinite(bias)&&std::isfinite(scale)&&std::isfinite(exponent)&&
					exponent>0.f&&exponent<=64.f&&scale>0.f&&scale<=64.f;
				output.falloff=usable?DirectX::XMFLOAT4{bias,scale,exponent,0}:DirectX::XMFLOAT4{0,1,2,0};
			}
			if(shape==5 && !ReadBox(wrapper,native,output)) return fail(kType);
			if(shape==kShapeSpot) {

				const auto direction=Read<DirectX::XMFLOAT3>(native,0x70);
				const float cosine=Read<float>(wrapper,0xA4), exponent=Read<float>(wrapper,0xA0);
				const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
				if(!std::isfinite(length)||length<1.e-6f||!std::isfinite(cosine)||!std::isfinite(exponent)||
					cosine < -1.f || cosine >= 1.f || exponent <= 0.f) return fail(kType);
				output.spot={direction.x/length,direction.y/length,direction.z/length,cosine};
				output.color.w=exponent;
			}
		}
		return true;
	}
	inline bool Snapshot(const void* scene, const VRMirrorScene::Frustum& frustum,
		const DirectX::XMFLOAT3& focus, Constants& output,
		std::array<const void*,kMaximumLights>* selectedWrappers = nullptr, Diagnostics* diagnostics = nullptr,
		std::size_t budget = kMaximumLights) noexcept
	{
		
		budget = budget == 0 ? kMaximumLights : (std::min)(budget, kMaximumLights);
		output.extentLightsHistory.z=0; output.lights={};
		if (selectedWrappers) *selectedWrappers={};
		std::array<float,kMaximumLights> scores{}; std::array<const void*,kMaximumLights> selected{};
		
		std::array<std::uint8_t,kMaximumLights> previousReason{}; std::array<DirectX::XMFLOAT4,kMaximumLights> previousPosition{};
		if(diagnostics) { diagnostics->dropped={}; diagnostics->reasons={}; diagnostics->droppedPositions={};
			diagnostics->droppedCount=0; diagnostics->visited=0; diagnostics->accepted=0;
			diagnostics->frustumRejected=0; diagnostics->budgetRejected=0; diagnostics->scoreRejected=0;
			diagnostics->flagRejected=0; diagnostics->fadeRejected=0; diagnostics->otherRejected=0; diagnostics->fades={};
			diagnostics->rejectReasons={}; diagnostics->rejectedShapes={}; diagnostics->duplicateSkipped=0;
			diagnostics->previsFlagged=0; diagnostics->mainViewCulled=0; diagnostics->engineFadeSum=0;
			diagnostics->shapeSamples={}; diagnostics->shapeSampleCount=0;
			previousReason.fill(kAbsent); }
		auto previousSlot=[&](const void* native) noexcept -> int {
			if(!diagnostics || !native) return -1;
			for(int i=0;i<static_cast<int>(kMaximumLights);++i) if(diagnostics->previous[i]==native) return i;
			return -1; };
		const auto* sun=Read<const void*>(scene,0x1F8);
		Light light{};
		if(ReadLight(sun,true,light)) {
			output.lights[0]=light; scores[0]=INFINITY; if(diagnostics) diagnostics->fades[0]=1;
			selected[0]=Read<const void*>(sun,0xB8); output.extentLightsHistory.z=1;
			if (selectedWrappers) {

				const auto* shadow=Read<const void*>(scene,0x208);
				(*selectedWrappers)[0]=Pointer(shadow) && Read<const void*>(shadow,0xB8)==selected[0] ? shadow : nullptr;
			}
		}
		unsigned visited=0;
		for(std::size_t offset:{0x158u,0x170u}) {
			const auto* entries=Read<const void* const*>(scene,offset);
			const auto capacity=Read<std::uint32_t>(scene,offset+8), count=Read<std::uint32_t>(scene,offset+16);
			if(count>capacity || count>4096 || (count&&!Pointer(entries))) return false;
			for(unsigned i=0;i<count;++i) {
				if(++visited>4096) return false;
				if(diagnostics && Pointer(entries[i])) {
					if(Read<std::uint8_t>(entries[i],0x17C)) ++diagnostics->previsFlagged;
					if(Read<std::uint32_t>(entries[i],0x18)==MirrorLightFade::kCulledMarker) ++diagnostics->mainViewCulled;
					const auto visibility=Read<float>(entries[i],0x10);
					if(std::isfinite(visibility)) diagnostics->engineFadeSum+=visibility;
				}
				std::uint8_t reason=kAccepted; float fade=1;
				if(!ReadLight(entries[i],false,light,&reason,&fade)) {
					if(diagnostics) {
						if(reason==kFlag17E) ++diagnostics->flagRejected;
						else if(reason==kFade) ++diagnostics->fadeRejected;
						else ++diagnostics->otherRejected;
						if(reason<diagnostics->rejectReasons.size()) ++diagnostics->rejectReasons[reason];
						if(reason==kType && Pointer(entries[i])) {
							const auto shape=Read<std::uint32_t>(entries[i],0x180);
							++diagnostics->rejectedShapes[shape<diagnostics->rejectedShapes.size()?shape:
								diagnostics->rejectedShapes.size()-1];
							const auto* refused=Read<const void*>(entries[i],0xB8);
							bool known=false;
							for(unsigned s=0;s<diagnostics->shapeSampleCount;++s)
								known|=diagnostics->shapeSamples[s].shape==shape;
							if(!known && Pointer(refused) &&
								diagnostics->shapeSampleCount<diagnostics->shapeSamples.size()) {
								auto& sample=diagnostics->shapeSamples[diagnostics->shapeSampleCount++];
								const auto position=Read<DirectX::XMFLOAT3>(refused,0xA0);
								sample.shape=shape;
								sample.vtable=Read<const void*>(refused,0);
								sample.positionRadius={position.x,position.y,position.z,
									Read<float>(refused,0x138)};
								sample.diffuse=Read<DirectX::XMFLOAT3>(refused,0x12C);
							}
						}
					}
					if(diagnostics && Pointer(entries[i])) {
						const int slot=previousSlot(Read<const void*>(entries[i],0xB8));
						if(slot>=0) previousReason[slot]=reason;
					}
					continue;
				}
				const auto* native=Read<const void*>(entries[i],0xB8);
				const int trackedSlot=previousSlot(native);
				if(trackedSlot>=0) previousPosition[trackedSlot]=light.positionRadius;
				const auto duplicate=std::find(selected.begin(),selected.end(),native);
				if(duplicate!=selected.end()) {
					if(diagnostics) ++diagnostics->duplicateSkipped;

					if (selectedWrappers && offset==0x170u)
						(*selectedWrappers)[duplicate-selected.begin()]=entries[i];
					continue;
				}
				const auto& p=light.positionRadius;
				if(!frustum.Intersects(p.x,p.y,p.z,p.w)) { if(diagnostics) ++diagnostics->frustumRejected; if(trackedSlot>=0) previousReason[trackedSlot]=kFrustum; continue; }
				const auto dx=p.x-focus.x,dy=p.y-focus.y,dz=p.z-focus.z;
				const float score=(.2126f*light.color.x+.7152f*light.color.y+.0722f*light.color.z)/
					(1+(dx*dx+dy*dy+dz*dz)/(p.w*p.w));
				if(!std::isfinite(score)||score<=0) { if(diagnostics) ++diagnostics->scoreRejected; if(trackedSlot>=0) previousReason[trackedSlot]=kScore; continue; }
				std::size_t slot=output.extentLightsHistory.z;
				if(slot==budget) {slot=std::min_element(scores.begin(),scores.begin()+budget)-scores.begin();
					if(score<=scores[slot]) { if(diagnostics) ++diagnostics->budgetRejected; if(trackedSlot>=0) previousReason[trackedSlot]=kBudget; continue; }
					if(diagnostics) { const int evicted=previousSlot(selected[slot]); if(evicted>=0) { previousReason[evicted]=kBudget; previousPosition[evicted]=output.lights[slot].positionRadius; } }
				}
				else ++output.extentLightsHistory.z;
				output.lights[slot]=light;scores[slot]=score;selected[slot]=native;
				if(diagnostics) diagnostics->fades[slot]=fade;
				if(trackedSlot>=0) previousReason[trackedSlot]=kAccepted;
				if (selectedWrappers) (*selectedWrappers)[slot]=entries[i];
			}
		}
		if(diagnostics) {
			diagnostics->visited=visited; diagnostics->accepted=output.extentLightsHistory.z;
			for(int i=0;i<static_cast<int>(kMaximumLights);++i) {
				const void* native=diagnostics->previous[i];
				if(!native || previousReason[i]==kAccepted) continue;
				if(std::find(selected.begin(),selected.end(),native)!=selected.end()) continue;
				if(diagnostics->droppedCount<diagnostics->dropped.size()) {
					const auto d=diagnostics->droppedCount++;
					diagnostics->dropped[d]=native; diagnostics->reasons[d]=previousReason[i];
					diagnostics->droppedPositions[d]=previousPosition[i];
				}
			}
			diagnostics->previous=selected;
		}
		return true;
	}
}
