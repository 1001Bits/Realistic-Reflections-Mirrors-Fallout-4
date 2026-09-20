#pragma once
#include "REL/Version.h"
#ifdef ENABLE_FALLOUT_VR
#	include <rapidcsv.h>
#endif

// Plugin-provided port fallback for ids missing from the running game's address library (0 = none).
extern "C" std::size_t DynRefPortIdFallback(std::uint64_t a_id) noexcept;
// True while the plugin's port map is the authority for bare REL::ID lookups (a ported game build whose
// address library uses a different id space). Bare ids must then never reach the running library.
extern "C" bool DynRefPortIdSpaceActive() noexcept;

namespace REL
{
	class IDDB
	{
	private:
		struct mapping_t
		{
			std::uint64_t id;
			std::uint64_t offset;
		};

	public:
		IDDB(const IDDB&) = delete;
		IDDB(IDDB&&) = delete;

		IDDB& operator=(const IDDB&) = delete;
		IDDB& operator=(IDDB&&) = delete;

		[[nodiscard]] static IDDB& get()
		{
			static IDDB singleton;
			return singleton;
		}

		/** Offset handed back for an id that cannot be resolved: far outside the module, so any use faults. */
		static constexpr std::size_t kUnresolvedOffset{ 0x7FFFFFF0 };

		[[nodiscard]] std::size_t id2offset(std::uint64_t a_id) const;
		/** Bare REL::ID lookups (the 1.10.163 id space): the plugin port map is consulted BEFORE the running library. */
		[[nodiscard]] std::size_t id2offset_portable(std::uint64_t a_id) const;
		[[nodiscard]] bool        contains(std::uint64_t a_id) const;

#ifdef ENABLE_FALLOUT_VR
		bool IsVRAddressLibraryAtLeastVersion(const char* a_minimalVRAddressLibVersion, bool a_reportAndFail = false) const;
#endif

	protected:
		friend class Offset2ID;

		[[nodiscard]] std::span<const mapping_t> get_id2offset() const noexcept { return _id2offset; }

		/** Logs the gap once and returns kUnresolvedOffset. */
		[[nodiscard]] static std::size_t unresolved(std::uint64_t a_id, const char* a_reason);

	private:
#ifdef ENABLE_FALLOUT_VR
		bool load_csv(std::string a_filename, Version a_version, bool a_failOnError);
#endif
		IDDB();
		~IDDB() = default;

		mmio::mapped_file_source   _mmap;
		std::span<const mapping_t> _id2offset;
#ifdef ENABLE_FALLOUT_VR
		Version _vrAddressLibraryVersion;
#endif
	};
}
