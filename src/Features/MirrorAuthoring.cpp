#include "PCH.h"
#include "MirrorAuthoring.h"
#include "MirrorAuthoringPath.h"
#include "MirrorAuthoringGeometry.h"
#include "RE/Bethesda/BSShaderMaterial.h"
#include "RE/Bethesda/BSShaderProperty.h"
#include "RE/Bethesda/BSTextureSet.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>

namespace MirrorAuthoring
{
	using Microsoft::WRL::ComPtr;
	namespace {
		std::atomic_bool enabled{false};
		template<class T> T Field(const void* p,std::size_t offset) noexcept {
			return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p)+offset);
		}
		const char* VRBaseTexture(const void* material) noexcept {

			if (!material) return nullptr;
			auto* textures=Field<void*>(material,0x68);
			if (!textures) return nullptr;
			using Fn=const char* (*)(void*,std::uint32_t);
			return Field<Fn*>(textures,0)[0x2B](textures,0);
		}
		struct Raw {
			RE::NiAVObject* root{};
			RE::BSGeometry* geometry{};
			RE::BSGraphics::TriShape* renderer{};
			RE::BSGraphics::Buffer *vertices{},*indices{};
			RE::NiTransform world{};
			std::uint64_t descriptor{};
			std::uint32_t vertexCount{},indexCount{};
		};
		struct Entry {
			RE::ObjectRefHandle reference;
			RE::NiPointer<RE::NiAVObject> root;
			RE::NiPointer<RE::BSGeometry> geometry;
			Raw source{};
			MirrorAuthoringGeometry::Surface surface;
			ComPtr<ID3D11Buffer> staging[2],gpuSource[2];
			std::uint32_t offsets[2]{};
			std::uint64_t token{};
			std::uint64_t retryAt{};
			bool rejected{},copied{};
		};
		struct Contour {
			std::vector<MirrorAuthoringGeometry::Vec2> triangles;
			ComPtr<ID3D11ShaderResourceView> srv;
		};
		struct State {
			std::unordered_map<std::uint32_t,Entry> entries;
			std::unordered_map<std::uint64_t,Contour> contours;
			ID3D11Device* device{};
			std::uint64_t nextToken{1};
			unsigned rejectLogs{};
		};
		State& Cache() { static auto* state=new State; return *state; }
		
		bool TaggedUnsafe(RE::TESObjectREFR* reference) noexcept {
			if (!reference || reference->IsDeleted() || reference->IsDisabled()) return false;
			auto* base=reference->GetObjectReference();
			if (!base || base->GetFormType()!=RE::ENUM_FORM_ID::kSTAT) return false;
			auto* model=reference->GetTESModel();
			auto* component=model ? model->GetAsModelMaterialSwap() : nullptr;
			auto* swap=component ? component->swapForm : nullptr;
			if (!swap || swap->swapMap.size()>4096) return false;
			unsigned matches=0;
			for (const auto& item:swap->swapMap)
				if (MirrorAuthoringPath::Material(item.second.swapMaterial.c_str())) ++matches;
			return matches==1;
		}
		bool LiveMarker(RE::BSGeometry* geometry) noexcept {
			const auto shift=REL::Module::IsVR() ? 0x40u : 0u;
			auto* property=netimmerse_cast<RE::BSLightingShaderProperty*>(Field<RE::NiProperty*>(geometry,0x138+shift));
			auto* material=property ? property->material : nullptr;
			if (!material || material->GetType()!=RE::BSShaderMaterial::Type::kLighting) return false;
			if (REL::Module::IsVR()) return MirrorAuthoringPath::Texture(VRBaseTexture(material));
			static_assert(offsetof(RE::BSLightingShaderMaterialBase,textureSet)==0x78);
			auto* lighting=static_cast<RE::BSLightingShaderMaterialBase*>(material);
			auto* textures=lighting->textureSet.get();
			return textures && MirrorAuthoringPath::Texture(textures->GetTextureFilename(RE::BSShaderProperty::TextureTypeEnum::kBase));
		}
		bool ReadRaw(RE::TESObjectREFR* reference,Raw& out,RE::BSGeometry* cached=nullptr) noexcept {
			__try {
				if (!TaggedUnsafe(reference)) return false;
				out.root=reference->Get3D();
				if (!out.root) return false;
				RE::BSGeometry* pane=cached;
				if (!pane) {
					RE::NiAVObject* pending[4096]{}; pending[0]=out.root;
					std::size_t size=1,visits=0;
					while (size) {
						auto* object=pending[--size];
						if (++visits>4096 || object->controllers) return false;
						if (auto* geometry=object->IsGeometry(); geometry && LiveMarker(geometry)) {
							
							if (pane) return false;
							pane=geometry;
						}
						if (auto* node=object->IsNode()) {
							auto& children=node->GetRuntimeData().children;
							if (children.capacity()>4096) return false;
							for (std::uint32_t i=0;i<children.capacity();++i) {
								auto* child=children[static_cast<std::uint16_t>(i)].get();
								if (!child) continue;
								if (child->parent!=node || size==4096) return false;
								pending[size++]=child;
							}
						}
					}
				}
				if (!pane || !LiveMarker(pane)) return false;
				
				const auto* type=pane->GetRTTI();
				if (!type || !type->GetName() || std::strcmp(type->GetName(),"BSTriShape")) return false;
				auto* ancestor=static_cast<RE::NiAVObject*>(pane);
				unsigned depth=0;
				while (ancestor && ancestor!=out.root && ++depth<64) {
					if (ancestor->controllers) return false;
					ancestor=ancestor->parent;
				}
				if (ancestor!=out.root || out.root->controllers) return false;
				const auto shift=REL::Module::IsVR() ? 0x40u : 0u;
				if (Field<void*>(pane,0x140+shift)) return false;
				out.geometry=pane;out.world=pane->world;
				out.renderer=Field<RE::BSGraphics::TriShape*>(pane,0x148+shift);
				out.descriptor=Field<std::uint64_t>(pane,0x150+shift);
				out.vertexCount=Field<std::uint16_t>(pane,0x164+shift);
				const auto triangles=Field<std::uint32_t>(pane,0x160+shift);
				if (!out.renderer || !out.vertexCount || !triangles || triangles>MirrorAuthoringGeometry::kMaxTriangleVertices/3 ||
					out.renderer->vertexDesc.desc!=out.descriptor) return false;
				out.indexCount=triangles*3;
				out.vertices=out.renderer->vertexBuffer;out.indices=out.renderer->indexBuffer;
				return out.vertices && out.indices;
			} __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool Same(const Raw& a,const Raw& b) noexcept {
			return a.root==b.root && a.geometry==b.geometry && a.renderer==b.renderer &&
				a.vertices==b.vertices && a.indices==b.indices && a.descriptor==b.descriptor &&
				a.vertexCount==b.vertexCount && a.indexCount==b.indexCount;
		}
		bool CPUCopy(RE::BSGraphics::Buffer* buffer,void* output,std::size_t bytes) noexcept {
			__try {
				if (!buffer || !buffer->data || buffer->invalidCpuData || buffer->pendingCopy ||
					std::atomic_ref(buffer->pendingRequests.load_unchecked()).load(std::memory_order_acquire)!=0 || bytes>buffer->maxDataSize) return false;
				std::memcpy(output,buffer->data,bytes); return true;
			} __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool GPUBuffer(RE::BSGraphics::Buffer* buffer,ID3D11Buffer*& out,std::uint32_t& offset,std::uint32_t bytes) noexcept {
			__try {
				if (!buffer || !buffer->buffer || buffer->pendingCopy ||
					std::atomic_ref(buffer->pendingRequests.load_unchecked()).load(std::memory_order_acquire)!=0 || bytes>buffer->maxDataSize) return false;
				out=reinterpret_cast<ID3D11Buffer*>(buffer->buffer);offset=buffer->dataOffset;return true;
			} __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool CopyStreams(Entry& entry,ID3D11Device* device,ID3D11DeviceContext* context,
			std::vector<std::byte>& vertices,std::vector<std::uint16_t>& indices) {
			const auto stride=static_cast<std::uint32_t>(entry.source.descriptor&15u)*4;
			if (stride<8 || stride>60) { entry.rejected=true;return false; }
			vertices.resize(entry.source.vertexCount*stride);indices.resize(entry.source.indexCount);
			if (CPUCopy(entry.source.vertices,vertices.data(),vertices.size()) &&
				CPUCopy(entry.source.indices,indices.data(),indices.size()*2)) return true;
			const std::uint32_t sizes[]{static_cast<std::uint32_t>(vertices.size()),static_cast<std::uint32_t>(indices.size()*2)};
			RE::BSGraphics::Buffer* buffers[]{entry.source.vertices,entry.source.indices};
			if (!entry.copied) {
				for (unsigned i=0;i<2;++i) {
					ID3D11Buffer* source{};std::uint32_t offset{};
					if (!GPUBuffer(buffers[i],source,offset,sizes[i])) return false;
					D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
					ComPtr<ID3D11Device> owner;source->GetDevice(owner.GetAddressOf());
					if (owner.Get()!=device || offset>desc.ByteWidth || sizes[i]>desc.ByteWidth-offset) return false;
					entry.gpuSource[i]=source;entry.offsets[i]=offset;
					D3D11_BUFFER_DESC copy{};copy.ByteWidth=sizes[i];copy.Usage=D3D11_USAGE_STAGING;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
					entry.staging[i].Reset();
					if (FAILED(device->CreateBuffer(&copy,nullptr,entry.staging[i].GetAddressOf()))) return false;
				}
				for (unsigned i=0;i<2;++i) {
					D3D11_BOX box{entry.offsets[i],0,0,entry.offsets[i]+sizes[i],1,1};
					context->CopySubresourceRegion(entry.staging[i].Get(),0,0,0,0,entry.gpuSource[i].Get(),0,&box);
				}
				entry.copied=true;return false;
			}
			void* outputs[]{vertices.data(),indices.data()};
			for (unsigned i=0;i<2;++i) {
				ID3D11Buffer* source{};std::uint32_t offset{};
				if (!GPUBuffer(buffers[i],source,offset,sizes[i]) || source!=entry.gpuSource[i].Get() || offset!=entry.offsets[i]) {
					entry.copied=false;return false;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(context->Map(entry.staging[i].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped))) return false;
				std::memcpy(outputs[i],mapped.pData,sizes[i]);context->Unmap(entry.staging[i].Get(),0);
			}
			return true;
		}
		bool WorldPane(const Entry& entry,const RE::NiTransform& world,Pane& out) noexcept {
			using namespace MirrorAuthoringGeometry;
			if (!Finite(world.scale) || world.scale<=0) return false;

			const auto rotate=world.rotate.Transpose();
			const auto transform=[&](Vec3 p) { const auto r=rotate*RE::NiPoint3{p.x,p.y,p.z};return Vec3{r.x,r.y,r.z}; };
			const auto& local=entry.surface.pane;
			out.world=local;
			const auto c=rotate*(RE::NiPoint3{local.center.x,local.center.y,local.center.z}*world.scale)+world.translate;
			out.world.center={c.x,c.y,c.z};
			const auto n=transform(local.normal),t=transform(local.tangent),b=transform(local.bitangent);
			if (!Finite(out.world.center) || !Normalize(n,out.world.normal) || !Normalize(t,out.world.tangent) ||
				!Normalize(b,out.world.bitangent) || std::abs(Dot(out.world.normal,out.world.tangent))>1e-3f ||
				std::abs(Dot(out.world.normal,out.world.bitangent))>1e-3f || std::abs(Dot(out.world.tangent,out.world.bitangent))>1e-3f) return false;
			out.world.halfWidth*=world.scale;out.world.halfHeight*=world.scale;
			if (out.world.halfWidth<1 || out.world.halfHeight<1 || out.world.halfWidth>5000 || out.world.halfHeight>5000) return false;
			out.world.bounds={out.world.center,0.05f,out.world.halfWidth,out.world.halfHeight};
			out.contour=entry.token;out.vertexCount=static_cast<std::uint32_t>(entry.surface.triangles.size());
			out.area=entry.surface.area*world.scale*world.scale;
			return Finite(out.area) && out.area>0;
		}
	}
	void Initialize() noexcept {
		enabled.store(false,std::memory_order_release);
		WIN32_FILE_ATTRIBUTE_DATA attributes{};
		const bool requested=GetFileAttributesExW(kEnableFile,GetFileExInfoStandard,&attributes) &&
			!(attributes.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
			!attributes.nFileSizeHigh && !attributes.nFileSizeLow;
		auto* data=RE::TESDataHandler::GetSingleton();
		auto* form=data ? data->LookupForm(kSurfaceFormID,kPlugin) : nullptr;
		const bool present=form && form->GetFormType()==RE::ENUM_FORM_ID::kTXST;
		enabled.store(requested && present,std::memory_order_release);
		logger::info("[MirrorAuthoring] experimental CK materials: requested={} surface={} enabled={} (one static part per object)",requested,present,requested && present);
	}
	bool Tagged(RE::TESObjectREFR* reference) noexcept {
		if (!enabled.load(std::memory_order_acquire)) return false;
		__try { return TaggedUnsafe(reference); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void Forget(std::uint32_t id) noexcept {
		auto& state=Cache();const auto it=state.entries.find(id);
		if (it!=state.entries.end()) { state.contours.erase(it->second.token);state.entries.erase(it); }
	}
	void Reset() noexcept { auto& state=Cache();state.entries.clear();state.contours.clear();state.device=nullptr;state.rejectLogs=0; }
	void Prune() noexcept {
		auto& entries=Cache().entries;
		for (auto it=entries.begin();it!=entries.end();) {
			const auto reference=it->second.reference.get();
			auto* cell=reference ? reference->GetParentCell() : nullptr;
			if (!reference || !cell || cell->cellDetached || !Tagged(reference.get()) || reference->Get3D()!=it->second.root.get()) {
				Cache().contours.erase(it->second.token);it=entries.erase(it);
			} else ++it;
		}
	}
	bool Read(RE::TESObjectREFR* reference,ID3D11Device* device,ID3D11DeviceContext* context,Pane& out) noexcept {
		out={};if (!device || !context || !Tagged(reference)) return false;
		try {
			auto& state=Cache();
			if (state.device!=device) { Reset();state.device=device; }
			const auto id=reference->GetFormID();auto it=state.entries.find(id);
			if (it!=state.entries.end() && it->second.root.get()!=reference->Get3D()) { Forget(id);it=state.entries.end(); }
			if (it!=state.entries.end() && it->second.retryAt>GetTickCount64()) return false;
			Raw raw{};
			if (!ReadRaw(reference,raw,it!=state.entries.end() ? it->second.geometry.get() : nullptr)) {
				Forget(id);
				if (raw.root && state.entries.size()<4096) {
					Entry retry;retry.reference=reference->GetHandle();retry.root.reset(raw.root);retry.retryAt=GetTickCount64()+1000;
					state.entries.emplace(id,std::move(retry));
					if (state.rejectLogs++<16) logger::info("[MirrorAuthoring] rejected REFR {:08X}: requires exactly one material-tagged, unanimated, unskinned BSTriShape with available buffers (retry 1s)",id);
				}
				return false;
			}
			if (it!=state.entries.end() && !Same(raw,it->second.source)) { Forget(id);it=state.entries.end(); }
			if (it==state.entries.end()) {
				if (state.entries.size()>=4096) return false;
				Entry value;value.reference=reference->GetHandle();value.root.reset(raw.root);value.geometry.reset(raw.geometry);value.source=raw;
				it=state.entries.emplace(id,std::move(value)).first;
			}
			auto& entry=it->second;
			if (entry.rejected) return false;
			if (!entry.token) {
				std::vector<std::byte> vertices;std::vector<std::uint16_t> indices;
				if (!CopyStreams(entry,device,context,vertices,indices)) return false;
				Raw after{};if (!ReadRaw(reference,after,entry.geometry.get()) || !Same(after,raw)) { Forget(id);return false; }
				std::vector<MirrorAuthoringGeometry::Vec3> positions;
				if (!MirrorAuthoringGeometry::Decode(vertices,indices,raw.vertexCount,raw.descriptor,positions) ||
					!MirrorAuthoringGeometry::Build(positions,entry.surface)) {
					entry.rejected=true;logger::info("[MirrorAuthoring] rejected REFR {:08X}: nonplanar, inconsistent-facing or unsupported static stream",id);return false;
				}
				entry.token=state.nextToken++;
				Contour contour;contour.triangles=entry.surface.triangles;state.contours.emplace(entry.token,std::move(contour));
				for (unsigned i=0;i<2;++i) { entry.staging[i].Reset();entry.gpuSource[i].Reset(); }
				logger::info("[MirrorAuthoring] accepted REFR {:08X}: {} triangles, half-size=({:.2f},{:.2f})",id,raw.indexCount/3,entry.surface.pane.halfWidth,entry.surface.pane.halfHeight);
			}
			return WorldPane(entry,raw.world,out);
		} catch (...) { return false; }
	}
	ID3D11ShaderResourceView* Vertices(std::uint64_t token,std::uint32_t count,ID3D11Device* device) noexcept {
		try {
			auto& state=Cache();if (!device || device!=state.device) return nullptr;
			const auto it=state.contours.find(token);
			if (it==state.contours.end() || count!=it->second.triangles.size() || !count) return nullptr;
			auto& contour=it->second;
			if (!contour.srv) {
				D3D11_BUFFER_DESC desc{};desc.ByteWidth=count*sizeof(MirrorAuthoringGeometry::Vec2);desc.Usage=D3D11_USAGE_IMMUTABLE;
				desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(MirrorAuthoringGeometry::Vec2);
				D3D11_SUBRESOURCE_DATA data{contour.triangles.data(),0,0};ComPtr<ID3D11Buffer> buffer;
				if (FAILED(device->CreateBuffer(&desc,&data,buffer.GetAddressOf()))) return nullptr;
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=count;
				if (FAILED(device->CreateShaderResourceView(buffer.Get(),&view,contour.srv.GetAddressOf()))) return nullptr;
			}
			return contour.srv.Get();
		} catch (...) { return nullptr; }
	}
}
