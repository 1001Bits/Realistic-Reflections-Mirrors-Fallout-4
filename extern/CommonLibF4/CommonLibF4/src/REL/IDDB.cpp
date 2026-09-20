#include "REL/IDDB.h"
#include "REL/Module.h"

#include <mutex>
#include <set>

#include "REX/W32/BCRYPT.h"

#include "F4SE/Logger.h"

namespace REL
{
	namespace log = F4SE::log;

	// Weak default: no port map. A plugin overrides this by defining the symbol itself.
	extern "C" std::size_t DynRefPortIdFallbackDefault(std::uint64_t) noexcept { return 0; }
#pragma comment(linker, "/alternatename:DynRefPortIdFallback=DynRefPortIdFallbackDefault")
	extern "C" bool DynRefPortIdSpaceActiveDefault() noexcept { return false; }
#pragma comment(linker, "/alternatename:DynRefPortIdSpaceActive=DynRefPortIdSpaceActiveDefault")

	std::optional<std::string> SHA512(std::span<const std::byte> a_data)
	{
		REX::W32::BCRYPT_ALG_HANDLE algorithm;
		if (!REX::W32::NT_SUCCESS(REX::W32::BCryptOpenAlgorithmProvider(&algorithm, REX::W32::BCRYPT_SHA512_ALGORITHM))) {
			log::error("failed to open algorithm provider");
			return std::nullopt;
		}

		const stl::scope_exit delAlgorithm([&]() {
			[[maybe_unused]] const auto success = REX::W32::NT_SUCCESS(REX::W32::BCryptCloseAlgorithmProvider(algorithm));
			assert(success);
		});

		REX::W32::BCRYPT_HASH_HANDLE hash;
		if (!REX::W32::NT_SUCCESS(REX::W32::BCryptCreateHash(algorithm, &hash))) {
			log::error("failed to create hash");
			return std::nullopt;
		}

		const stl::scope_exit delHash([&]() {
			[[maybe_unused]] const auto success = REX::W32::NT_SUCCESS(REX::W32::BCryptDestroyHash(hash));
			assert(success);
		});

		if (!REX::W32::NT_SUCCESS(REX::W32::BCryptHashData(
				hash,
				reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(a_data.data())),  // does not modify contents of buffer
				static_cast<std::uint32_t>(a_data.size())))) {
			log::error("failed to hash data");
			return std::nullopt;
		}

		std::uint32_t hashLen = 0;
		std::uint32_t discard = 0;
		if (!REX::W32::NT_SUCCESS(REX::W32::BCryptGetProperty(
				hash,
				REX::W32::BCRYPT_HASH_LENGTH,
				reinterpret_cast<std::uint8_t*>(&hashLen),
				sizeof(hashLen),
				&discard))) {
			log::error("failed to get property");
			return std::nullopt;
		}

		std::vector<std::uint8_t> buffer(static_cast<std::size_t>(hashLen));
		if (!REX::W32::NT_SUCCESS(REX::W32::BCryptFinishHash(
				hash,
				buffer.data(),
				static_cast<std::uint32_t>(buffer.size())))) {
			log::error("failed to finish hash");
			return std::nullopt;
		}

		std::string result;
		result.reserve(buffer.size() * 2);
		for (const auto byte : buffer) {
			result += std::format("{:02X}", byte);
		}

		return { std::move(result) };
	}
}

namespace REL
{
	IDDB::IDDB()
	{
		const auto version = Module::get().version();
		const auto path = std::format("Data/F4SE/Plugins/version-{}.{}", version.string("-"sv), Module::IsVR() ? "csv"sv : "bin"sv);
		if (!_mmap.open(path)) {
			stl::report_and_fail(std::format("failed to open: {}", path));
		}

		if (version == Version{ 1, 10, 980 }) {
			const auto sha = SHA512({ _mmap.data(), _mmap.size() });
			if (!sha) {
				stl::report_and_fail(std::format("failed to hash: {}", path));
			}
			// Address bins are expected to be pre-sorted. This bin was released without being sorted, and will cause lookups to randomly fail.
			if (*sha == "2AD60B95388F1B6E77A6F86F17BEB51D043CF95A341E91ECB2E911A393E45FE8156D585D2562F7B14434483D6E6652E2373B91589013507CABAE596C26A343F1"sv) {
				stl::report_and_fail(std::format(
					"The address bin you are using ({}) is corrupted. "
					"Please go to the Nexus page for Address Library and redownload the file corresponding to version {}.{}.{}.{}",
					path,
					version[0],
					version[1],
					version[2],
					version[3]));
			}
		}

#ifdef ENABLE_FALLOUT_VR
		if (!Module::IsVR()) {
#endif
			_id2offset = std::span{
				reinterpret_cast<const mapping_t*>(_mmap.data() + sizeof(std::uint64_t)),
				*reinterpret_cast<const std::uint64_t*>(_mmap.data())
			};
#ifdef ENABLE_FALLOUT_VR
		} else {
			load_csv(path, version, true);
		}
#endif
	}

	[[nodiscard]] bool IDDB::contains(std::uint64_t a_id) const
	{
		if (_id2offset.empty()) {
			return false;
		}

		const mapping_t elem{ a_id, 0 };
		const auto      it = std::lower_bound(
            _id2offset.begin(),
            _id2offset.end(),
            elem,
            [](auto&& a_lhs, auto&& a_rhs) {
                return a_lhs.id < a_rhs.id;
            });
		// Same lower_bound caveat as id2offset: without the equality check this reports true for any id
		// below the largest one in the library.
		return it != _id2offset.end() && it->id == a_id;
	}

	// A lookup that cannot be answered truthfully. Returning a neighbouring entry corrupts memory silently,
	// so name the id in the log and hand back an offset far outside the module: any use faults immediately
	// at a recognisable address instead of reading or writing unrelated game state.
	[[nodiscard]] std::size_t IDDB::unresolved(std::uint64_t a_id, const char* a_reason)
	{
		static std::mutex           reportedMutex;
		static std::set<std::uint64_t> reported;
		bool                        first = false;
		{
			const std::scoped_lock lock{ reportedMutex };
			first = reported.insert(a_id).second;
		}
		if (first) {
			log::error("[Port] UNRESOLVED address-library id {} ({}); poisoned, any use will fault at +{:#x}",
				a_id, a_reason, kUnresolvedOffset);
		}
		return kUnresolvedOffset;
	}

	[[nodiscard]] std::size_t IDDB::id2offset_portable(std::uint64_t a_id) const
	{
		// Bare REL::ID ids live in the 1.10.163 id space. A plugin port map takes precedence because the
		// running library may reuse the same number for something else. RelocationID/VariantID ids are
		// already in the running library's space and must NOT go through this map (their numbers collide).
		if (const auto mapped = DynRefPortIdFallback(a_id); mapped != 0) {
			return mapped;
		}
		if (DynRefPortIdSpaceActive()) {
			// The running library indexes a different id space, so this number would name an unrelated
			// address there. Report the gap instead of resolving it.
			return unresolved(a_id, "1.10.163 id absent from the port map");
		}
		return id2offset(a_id);
	}

	[[nodiscard]] std::size_t IDDB::id2offset(std::uint64_t a_id) const
	{
		if (_id2offset.empty()) {
			stl::report_and_fail("data is empty"sv);
		}

		const mapping_t elem{ a_id, 0 };
		const auto      it = std::lower_bound(
            _id2offset.begin(),
            _id2offset.end(),
            elem,
            [](auto&& a_lhs, auto&& a_rhs) {
                return a_lhs.id < a_rhs.id;
            });
		// lower_bound returns the first entry with id >= a_id, so an id the library does not contain lands
		// on its neighbour. Verifying the match is required on every runtime, not only VR: without it a
		// missing id silently resolves to an unrelated address (1.11.240 crashed this way inside
		// PlayerCamera::GetSingleton, whose NG id is absent from version-1-11-240-0.bin).
		if (it == _id2offset.end() || it->id != a_id) {
			if (DynRefPortIdSpaceActive()) {
				return unresolved(a_id, "absent from the running address library");
			}
			log::error("[Port] unmapped address-library id {} (game {})", a_id, Module::get().version().string());
			const auto version = Module::get().version();
			const auto str = std::format(
				"Failed to find the id within the address library: {}\n"
				"This means this script extender plugin is incompatible with the address "
				"library for this version of the game, and thus does not support it."
				"\nGame version: {}"sv,
				a_id, version.string());
			stl::report_and_fail(str);
		}

		return static_cast<std::size_t>(it->offset);
	}

#ifdef ENABLE_FALLOUT_VR
	bool IDDB::load_csv(std::string a_filename, Version, bool a_failOnError)
	{
		if (_id2offset.size())
			return true;
		if (!std::filesystem::exists(a_filename)) {
			return stl::report_and_error(
				std::format("Required VR Address Library file {} does not exist"sv, a_filename),
				a_failOnError);
		}

		rapidcsv::Document in(a_filename);
		std::size_t        id, address_count;
		std::string        version, offset;
		address_count = in.GetCell<std::size_t>(0, 0);
		version = in.GetCell<std::string>(1, 0);
		_vrAddressLibraryVersion = Version(version);
		static std::vector<mapping_t> tempVector{};
		if (in.GetRowCount() > address_count + 1) {
			return stl::report_and_error(
				std::format("VR Address Library {} tried to exceed {} allocated entries."sv,
					version, address_count),
				a_failOnError);
		} else if (in.GetRowCount() < address_count + 1) {
			return stl::report_and_error(
				std::format(
					"VR Address Library {} loaded only {} entries but expected {}. Please redownload."sv,
					version, in.GetRowCount() - 1, address_count),
				a_failOnError);
		}
		std::size_t index = 1;
		for (; index < in.GetRowCount(); ++index) {
			id = in.GetCell<std::size_t>(0, index);
			offset = in.GetCell<std::string>(1, index);
			const mapping_t elem = { id, static_cast<std::uint64_t>(std::stoul(offset, nullptr, 16)) };
			tempVector.push_back(elem);
		}
		std::sort(
			tempVector.begin(),
			tempVector.end(),
			[](auto&& a_lhs, auto&& a_rhs) {
				return a_lhs.id < a_rhs.id;
			});
		_id2offset = std::span(tempVector);
		return true;
	}

	bool IDDB::IsVRAddressLibraryAtLeastVersion(const char* a_minimalVRAddressLibVersion, bool a_reportAndFail) const
	{
		const auto minimalVersion = REL::Version(a_minimalVRAddressLibVersion);

		if (minimalVersion <= _vrAddressLibraryVersion) {
			return true;
		}

		if (!a_reportAndFail) {
			return false;
		}

		stl::report_and_fail(
			std::format("You need version: {} of VR Address Library for F4SEVR, you have version: {}"sv,
				minimalVersion, _vrAddressLibraryVersion));
	}
#endif
}
