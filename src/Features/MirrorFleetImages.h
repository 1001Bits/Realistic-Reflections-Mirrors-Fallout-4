#pragma once
#include <cstdint>
#include <unordered_map>
#include <d3d11.h>
#include <wrl/client.h>

namespace MirrorFleet
{

	template<class Identity, class Metadata>
	class Images
	{
	public:
		struct Entry
		{
			Identity identity{};
			Metadata metadata{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			
			Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV;
			std::uint64_t bytes{}, lastNeeded{};
		};
		Entry& Observe(std::uint32_t id) { return entries_[id]; }
		void Needed(std::uint32_t id,std::uint64_t now) noexcept
		{ if (auto* entry=Find(id)) entry->lastNeeded=now; }
		static std::uint64_t TextureBytes(ID3D11Texture2D* texture) noexcept
		{
			if (!texture) return 0;
			D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);

			const unsigned stride=(desc.Format==DXGI_FORMAT_R32_FLOAT || desc.Format==DXGI_FORMAT_R32_UINT ||
				desc.Format==DXGI_FORMAT_R24G8_TYPELESS) ? 4u :
				(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 16u);
			std::uint64_t bytes=0;UINT width=desc.Width,height=desc.Height;
			for (UINT mip=0;mip<desc.MipLevels;++mip) {
				bytes+=std::uint64_t(width)*height*stride*desc.ArraySize*desc.SampleDesc.Count;
				width=width>1?width/2:1;height=height>1?height/2:1;
			}
			return bytes;
		}
		std::uint64_t Bytes() const noexcept
		{ std::uint64_t total=0;for (const auto& [id,entry]:entries_)total+=entry.bytes;return total; }
		template<class Protected>
		unsigned Trim(std::uint64_t now,std::uint64_t budget,Protected&& protect) noexcept
		{
			auto total=Bytes();unsigned retired=0;
			while (total>budget) {
				Entry* oldest=nullptr;
				for (auto& [id,entry]:entries_)
					if (entry.bytes && !protect(id) && now>=entry.lastNeeded && now-entry.lastNeeded>=5000 &&
						(!oldest || entry.lastNeeded<oldest->lastNeeded)) oldest=&entry;
				if (!oldest) break; 
				total-=oldest->bytes;oldest->bytes=0;
				oldest->texture.Reset();oldest->srv.Reset();oldest->depthTexture.Reset();oldest->depthSRV.Reset();
				oldest->metadata={};++retired;
			}
			return retired;
		}
		Entry* Find(std::uint32_t id) noexcept
		{
			const auto found = entries_.find(id);
			return found == entries_.end() ? nullptr : &found->second;
		}
		bool Publish(std::uint32_t id, ID3D11Texture2D* texture,
			ID3D11ShaderResourceView* srv, const Metadata& metadata) noexcept
		{
			auto* entry = Find(id);
			if (!entry || !texture || !srv) return false;
			entry->texture = texture;
			entry->srv = srv;
			entry->metadata = metadata;
			
			entry->depthTexture = nullptr;
			entry->depthSRV = nullptr;
			entry->bytes=TextureBytes(texture);
			return true;
		}

		bool PublishDepth(std::uint32_t id, ID3D11Texture2D* texture,
			ID3D11ShaderResourceView* srv) noexcept
		{
			auto* entry = Find(id);
			if (!entry) return false;
			const bool complete = texture && srv;
			entry->depthTexture = complete ? texture : nullptr;
			entry->depthSRV = complete ? srv : nullptr;
			entry->bytes=TextureBytes(entry->texture.Get())+TextureBytes(entry->depthTexture.Get());
			return complete;
		}
		bool OwnsDepth(ID3D11Texture2D* texture) const noexcept
		{
			if (!texture) return false;
			for (const auto& [id, entry] : entries_)
				if (entry.depthTexture.Get() == texture) return true;
			return false;
		}
		bool Owns(ID3D11Texture2D* texture) const noexcept
		{
			if (!texture) return false;
			for (const auto& [id, entry] : entries_)
				if (entry.texture.Get() == texture) return true;
			return false;
		}
		void Erase(std::uint32_t id) { entries_.erase(id); }
		void Clear() { entries_.clear(); }
		auto& Entries() noexcept { return entries_; }
	private:
		std::unordered_map<std::uint32_t, Entry> entries_;
	};
}
