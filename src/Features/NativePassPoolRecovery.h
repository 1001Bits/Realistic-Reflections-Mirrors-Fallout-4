#pragma once

namespace NativePassPoolRecovery
{
	
	using OwnsScope = bool (*)() noexcept;
	[[nodiscard]] bool Install(OwnsScope ownsScope) noexcept;
	[[nodiscard]] bool PoolsIdle() noexcept;
	
	[[nodiscard]] bool Restore() noexcept;
}
