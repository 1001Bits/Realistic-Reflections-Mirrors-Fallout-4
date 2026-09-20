#pragma once
#include <algorithm>
#include <cstdint>

namespace MirrorSelection
{

	template<class Candidates, class Identity, class Visible>
	void Retain(Candidates& candidates, std::uint32_t incumbent, Identity identity, Visible visible, bool allFit = false)
	{
		if (!incumbent || candidates.empty()) return;
		const auto current = std::find_if(candidates.begin(), candidates.end(),
			[&](const auto& candidate) { return identity(candidate) == incumbent; });
		if (current != candidates.end() && (allFit || visible(*current) || !visible(candidates.front())))
			std::rotate(candidates.begin(), current, current + 1);
	}
}
