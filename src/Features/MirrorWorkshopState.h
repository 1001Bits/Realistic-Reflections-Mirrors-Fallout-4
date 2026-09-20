#pragma once

#include <cstdint>

namespace MirrorWorkshop
{

	struct PlacementState
	{
		std::uint32_t heldFormID{};
		std::uint32_t reservedVRFormID{};
		bool open{};

		bool Update(bool menuOpen, std::uint32_t held, std::uint32_t vrOwner) noexcept
		{
			held = menuOpen ? held : 0u;
			const bool changed = open != menuOpen || heldFormID != held;
			if (heldFormID != held)
				reservedVRFormID = held != 0u && held == vrOwner ? held : 0u;
			heldFormID = held;
			open = menuOpen;
			return changed;
		}

		bool CaptureAllowed(std::uint32_t reference) const noexcept
		{
			return heldFormID == 0u || reference != heldFormID;
		}

		bool CandidateAllowed(std::uint32_t reference, bool vr) const noexcept
		{
			return CaptureAllowed(reference) || (vr && reference == reservedVRFormID);
		}
	};
}
