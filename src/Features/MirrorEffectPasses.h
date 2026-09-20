#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace MirrorEffectPasses
{

	inline constexpr std::size_t kMaximum = 8192;
	struct Item { void* pass{}; float depth{}; std::size_t order{}; };
	struct Snapshot
	{
		std::array<Item, kMaximum> items{};
		std::array<const void*, kMaximum * 2> seen{};
		std::size_t count{}, visits{};
		void Reset() noexcept { count=visits=0;seen.fill(nullptr); }
		void Sort() noexcept
		{
			std::sort(items.begin(),items.begin()+count,[](const Item& left,const Item& right) {
				return left.depth!=right.depth ? left.depth>right.depth : left.order<right.order;
			});
		}

		bool First(const void* pass) noexcept
		{
			auto slot=(reinterpret_cast<std::uintptr_t>(pass)>>3u) % seen.size();
			for(std::size_t probe=0;probe<seen.size();++probe) {
				if(seen[slot]==pass) return false;
				if(!seen[slot]) {seen[slot]=pass;return true;}
				slot=(slot+1)%seen.size();
			}
			return false;
		}
		template<class Next,class Select>
		bool Chain(void* pass,Next next,Select select) noexcept
		{
			while(pass) {
				if(!First(pass)) return true;
				if(++visits>kMaximum) return false;
				float depth{};
				if(select(pass,depth)) {
					if(count==items.size()) return false;
					items[count]={pass,depth,count};++count;
				}
				pass=next(pass);
			}
			return true;
		}
	};
}
