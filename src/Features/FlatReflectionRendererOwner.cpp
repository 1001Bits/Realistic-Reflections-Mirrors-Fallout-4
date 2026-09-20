#include "Features/ReflectionRuntime.h"
#include "FlatReflectionRendererOwner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include <bcrypt.h>
#include <wrl/client.h>

#pragma comment(lib, "bcrypt.lib")

#include "RE/Bethesda/BSFixedString.h"
#include "RE/Bethesda/BSLock.h"
#include "RE/Bethesda/IMenu.h"
#include "RE/Bethesda/Interface3D.h"
#include "RE/NetImmerse/NiNode.h"
#include "RE/NetImmerse/NiUpdateData.h"
#include "RE/VTABLE_IDs.h"

#include "FlatDeferredPlayerCapture.h"

namespace FlatReflectionRendererOwner
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr std::uintptr_t kCanonicalPointerMinimum = 0x10000000000ull;
		constexpr std::uintptr_t kCanonicalPointerMaximum = 0x7FFFFFFFFFFFull;
		constexpr std::size_t kMaximumLiveOrdinaryLights = 2048;
		constexpr std::size_t kSSNOrdinaryLightArrayOffset = 0x158;
		constexpr std::size_t kSSNOrdinaryLightCapacityOffset = 0x160;
		constexpr std::size_t kSSNOrdinaryLightCountOffset = 0x168;
		constexpr std::size_t kSSNShadowLightArrayOffset = 0x170;
		constexpr std::size_t kSSNShadowLightCapacityOffset = 0x178;
		constexpr std::size_t kSSNShadowLightCountOffset = 0x180;
		constexpr std::size_t kSSNPortalLightArrayOffset = 0x188;
		constexpr std::size_t kSSNPortalLightCapacityOffset = 0x190;
		constexpr std::size_t kSSNPortalLightCountOffset = 0x198;
		constexpr std::size_t kSSNQueuedAddArrayOffset = 0x1A0;
		constexpr std::size_t kSSNQueuedAddCapacityOffset = 0x1A8;
		constexpr std::size_t kSSNQueuedAddCountOffset = 0x1B0;
		constexpr std::size_t kSSNQueuedRemoveArrayOffset = 0x1B8;
		constexpr std::size_t kSSNQueuedRemoveCapacityOffset = 0x1C0;
		constexpr std::size_t kSSNQueuedRemoveCountOffset = 0x1C8;
		constexpr std::size_t kSSNLightLockOffset = 0x1D0;
		constexpr std::size_t kSSNBaseSunOffset = 0x1F8;
		constexpr std::size_t kSSNShadowSunOffset = 0x208;
		constexpr std::size_t kSSNChildrenDataOffset = 0x128;
		constexpr std::size_t kSSNChildrenCapacityOffset = 0x130;
		constexpr std::size_t kSSNChildrenCountOffset = 0x134;

		constexpr std::size_t kBSLightLodDimmerOffset = 0x10;
		constexpr std::size_t kBSLightNiLightOffset = 0xB8;
		constexpr std::size_t kBSLightPortalByteOffset = 0x171;
		constexpr std::size_t kBSLightShapeOffset = 0x180;
		constexpr std::uint32_t kOrdinaryPointShape = 2;

		constexpr std::size_t kNiAVLocalRotationOffset = 0x30;
		constexpr std::size_t kNiAVLocalTranslationOffset = 0x60;
		constexpr std::size_t kNiAVWorldRotationOffset = 0x70;
		constexpr std::size_t kNiAVWorldTranslationOffset = 0xA0;
		constexpr std::size_t kNiAVFlagsOffset = 0x108;
		constexpr std::size_t kNiLightPODOffset = 0x120;
		constexpr std::size_t kNiLightPODSize = 0x28;
		constexpr std::size_t kNiLightDiffuseOffset = 0x12C;
		constexpr std::size_t kNiPointRadiusOffset = 0x138;
		constexpr std::size_t kNiPointRadiusLane1Offset = 0x13C;
		constexpr std::size_t kNiPointRadiusLane2Offset = 0x140;
		constexpr std::size_t kNiLightFadeOffset = 0x144;
		constexpr std::uint64_t kNiAVAppCulledBit = 1ull;
		constexpr std::uint64_t kNiAVFlattenedFadeRouteBit = 0x4000ull;

		constexpr std::size_t kRendererNeedsLightSetupOffset = 0x59;
		constexpr std::size_t kAccumulatorCameraOffset = 0x10;
		constexpr std::size_t kAccumulatorBatchOffset = 0xC8;
		constexpr std::size_t kBatchGroupArrayOffset = 0x08;
		constexpr std::size_t kBatchGroupArrayStride = 0x18;
		constexpr std::size_t kBatchSortedCurrentRecordOffset = 0x410;
		constexpr std::size_t kBatchSortedCountOffset = 0x418;
		constexpr std::size_t kPassRecordHeadOffset = 0x08;
		constexpr std::size_t kPassRecordTailOffset = 0x10;
		constexpr std::size_t kPassRecordCountOffset = 0x1C;
		constexpr std::size_t kPassPoolBatchCountOffset = 0x34;
		constexpr std::uint32_t kAccumulatorGroupArrayCount = 13;

		constexpr std::uintptr_t kRVA_ActiveShadowSceneNode = 0x6721B70;
		constexpr std::uintptr_t kRVA_CurrentAccumulator = 0x6721AB0;
		constexpr std::uintptr_t kRVA_RenderPassPoolPointer = 0x6732B70;
		constexpr std::uintptr_t kRVA_RenderLightPoolPointer = 0x6732B78;
		constexpr std::uintptr_t kRVA_BSMTAMode = 0x6723A44;
		constexpr std::uintptr_t kRVA_Global1CDC = 0x6721CDC;
		constexpr std::uintptr_t kRVA_Global1BC8 = 0x6721BC8;
		constexpr std::uintptr_t kRVA_Global1BCC = 0x6721BCC;
		constexpr std::uintptr_t kRVA_Global1C19 = 0x6721C19;
		constexpr std::uintptr_t kRVA_Global1F55 = 0x6721F55;
		constexpr std::uintptr_t kRVA_InterfaceDisplayGeometry = 0x6721D30;
		constexpr std::uintptr_t kRVA_InterfaceGlobal1D34 = 0x6721D34;
		constexpr std::uintptr_t kRVA_InterfaceGlobal1D34Source = 0x377C768;
		constexpr std::uintptr_t kRVA_InterfacePostAA = 0x6721D38;
		constexpr std::uintptr_t kRVA_InterfaceOpacityAlpha = 0x6721D3C;
		constexpr std::uintptr_t kRVA_InterfaceMenuEmitIntensity = 0x6721BE0;
		constexpr std::uintptr_t kRVA_InterfaceMenuDiffuseIntensity = 0x6721BE4;

		constexpr float kMaximumFiniteLightValue = 65504.0f;
		constexpr float kMaximumPointRadius = 1000000.0f;
		constexpr float kMinimumPointRadius = 0.001f;

		constexpr std::size_t kMaximumAbandonedGenerations = 16;

		constexpr float kRotationTolerance = 0.001f;

		struct OwnerFrozenRange
		{
			std::uint32_t beginRVA{};
			std::uint32_t endRVAInclusive{};
			std::array<std::uint8_t, 32> sha256{};
		};

		template <std::size_t N>
		consteval auto HexBytes(const char (&text)[N])
		{
			static_assert(N % 2 == 1);
			std::array<std::uint8_t, (N - 1) / 2> result{};
			auto nibble = [](char value) consteval -> std::uint8_t {
				if (value >= '0' && value <= '9')
					return static_cast<std::uint8_t>(value - '0');
				if (value >= 'a' && value <= 'f')
					return static_cast<std::uint8_t>(value - 'a' + 10);
				if (value >= 'A' && value <= 'F')
					return static_cast<std::uint8_t>(value - 'A' + 10);
				return 0xFF;
			};
			for (std::size_t index = 0; index < result.size(); ++index)
				result[index] = static_cast<std::uint8_t>(
					(nibble(text[index * 2]) << 4) | nibble(text[index * 2 + 1]));
			return result;
		}

		constexpr OwnerFrozenRange kOwnerCreateRange{ 0x0AE59B0, 0x0AE5AC1,
			HexBytes("e55c659f40a6877e19f520f055d09376b88c5486d5a0cb2024e0703dfd5b4f2a") };
		constexpr OwnerFrozenRange kOwnerReleaseRange{ 0x0AE5AD0, 0x0AE5BA2,
			HexBytes("3b4ea9debadaba3ce3987acef2e32a9e8200b6753d5c5321c8568f1285d3cc32") };
		constexpr OwnerFrozenRange kOwnerDisableRange{ 0x0AE5C10, 0x0AE5C8D,
			HexBytes("8aeddb29cab8d4ee79c1fb74ae97374526400c56bd7c28bd901b943bb52cacfc") };
		constexpr OwnerFrozenRange kOwnerGetByNameRange{ 0x0AE5CA0, 0x0AE5D70,
			HexBytes("01d42009bc78e4972741c99d6801798c4b87ab3fc6a5c72003fd581e9e41b3a4") };
		constexpr OwnerFrozenRange kOwnerAddPointRange{ 0x0AE5F80, 0x0AE60C6,
			HexBytes("5cd8db5eadd246c55986c2bc18150a863f7e50ae3d6bc6bd98b872f170d1daaf") };
		constexpr OwnerFrozenRange kOwnerSetDirectionalRange{ 0x0AE6670, 0x0AE6958,
			HexBytes("2c2b1ff7e2f02635faa75f0319d70d3f13a08ec4c400f3385fb823ee0c4e4dd1") };
		constexpr OwnerFrozenRange kDirectionalUpdateRange{ 0x1BA3EA0, 0x1BA3F27,
			HexBytes("f20f1dd3318e60b593b80f7106afeed19ac52d8d6b219734eb51236808dc76ea") };
		constexpr OwnerFrozenRange kInterface3DUpdateLightsRange{ 0x0AE9FB0, 0x0AEA562,
			HexBytes("cca2e66ed83d38f92592075bb636c83c5d2086481ff1f79196a87c690d26a2d8") };
		constexpr OwnerFrozenRange kProcessQueuedLightsRange{ 0x28101E0, 0x28103B2,
			HexBytes("f75efc89c2b3918b8144b36000ed8caf5163ee5dd5105c91e2c891801b1c295f") };
		constexpr OwnerFrozenRange kSetSunLightRange{ 0x2811360, 0x2811503,
			HexBytes("5822d3b65711c97a012165132de44cbb93ef19a23e6ab256aebdaf1554020322") };
		constexpr OwnerFrozenRange kNiAVObjectUpdateRange{ 0x1BA3BE0, 0x1BA3C2E,
			HexBytes("c54128e28786951396b6064f863b275ad3125c50e2db7fc681e5bbb6c1c6dd6f") };
		constexpr std::uintptr_t kDirectionalVtableRVA = 0x2E17998;
		constexpr std::size_t kDirectionalUpdateVtableOffset = 0x30 * sizeof(void*);
		constexpr std::uintptr_t kDirectionalUpdateRVA = 0x1BA3EA0;

		struct PointLightSnapshot
		{
			std::uintptr_t wrapperIdentity{};
			std::uintptr_t lightIdentity{};
			RE::NiPoint3 translation{};
			RE::NiColor diffuse{};
			float radius{};
			float dimmer{};
			float score{};
		};

		struct DirectionalLightSnapshot
		{
			std::uintptr_t lightIdentity{};
			std::array<float, 12> worldRotation{};
			std::array<std::byte, kNiLightPODSize> lightPOD{};
			bool contributes{};
			bool valid{};
		};

		struct LightingSnapshot
		{
			DirectionalLightSnapshot directional{};
			std::array<PointLightSnapshot, Lease::kMaximumPointLights> points{};
			std::array<std::uintptr_t, kMaximumLiveOrdinaryLights> sourceWrapperIdentities{};
			std::array<std::uintptr_t, kMaximumLiveOrdinaryLights> sourceLightIdentities{};
			std::uint32_t pointCount{};
			std::uint32_t sourceIdentityCount{};
			std::uint64_t pointTopologySignature{};
		};

		struct PinnedOrdinaryLights
		{
			RE::BSLight* lights[kMaximumLiveOrdinaryLights]{};
			std::uint32_t count{};
			std::uint32_t exceptionCode{};
			bool layoutValid{};
		};

		struct PinnedSun
		{
			RE::NiDirectionalLight* light{};
			std::uint32_t exceptionCode{};
			bool wrappersAttested{};
			
			bool sourceAbsent{};
		};

		struct AbandonedGeneration
		{
			RE::Interface3D::Renderer* renderer{};
			RE::BSFixedString registryName{};
			void* accumulator{};
			RE::NiPointer<RE::NiAVObject> modelRoot{};
			RE::NiPointer<RE::NiAVObject> shadowSceneNode{};
			RE::NiPointer<RE::NiAVObject> directionalLight{};
			PrivateDirectionalLightValues directionalLightValues{};
			std::array<RE::NiPointer<RE::NiAVObject>, Lease::kMaximumPointLights> pointLights{};
			std::array<PrivatePointLightValues, Lease::kMaximumPointLights> pointLightValues{};
			std::array<std::uintptr_t, Lease::kMaximumPointLights> sourcePointWrapperIdentities{};
			std::array<std::uintptr_t, Lease::kMaximumPointLights> sourcePointLightIdentities{};
			std::uint32_t pointLightCount{};
			ComPtr<ID3D11Device> device{};
			void* liveShadowSceneNode{};
			std::uint64_t loadGeneration{};
			std::uint64_t generationSerial{};
			std::uint64_t pointTopologySignature{};
			std::uint32_t renderThreadId{};
			
			bool reclaimableOnOwnerThread{};
		};

		struct OwnerState
		{
			SRWLOCK lock{ SRWLOCK_INIT };
			RE::Interface3D::Renderer* renderer{};
			RE::BSFixedString registryName{};
			void* accumulator{};
			RE::NiPointer<RE::NiAVObject> modelRoot{};
			RE::NiPointer<RE::NiAVObject> shadowSceneNode{};
			RE::NiPointer<RE::NiAVObject> directionalLight{};
			PrivateDirectionalLightValues directionalLightValues{};
			std::array<RE::NiPointer<RE::NiAVObject>, Lease::kMaximumPointLights> pointLights{};
			std::array<PrivatePointLightValues, Lease::kMaximumPointLights> pointLightValues{};
			std::array<std::uintptr_t, Lease::kMaximumPointLights> sourcePointWrapperIdentities{};
			std::array<std::uintptr_t, Lease::kMaximumPointLights> sourcePointLightIdentities{};
			std::uint32_t pointLightCount{};
			ComPtr<ID3D11Device> device{};
			void* liveShadowSceneNode{};
			std::uint64_t loadGeneration{};
			std::uint64_t generationSerial{};
			std::uint64_t pointTopologySignature{};
			std::uint32_t renderThreadId{};
			std::uint64_t activeToken{};
			bool poisoned{};
			bool releasePending{};
			bool permanentFailure{};
			std::array<AbandonedGeneration, kMaximumAbandonedGenerations> abandonedGenerations{};
			std::uint32_t abandonedGenerationCount{};
		};

		class OwnerExclusiveLock
		{
		public:
			explicit OwnerExclusiveLock(SRWLOCK& lock) noexcept : lock_(std::addressof(lock))
			{
				AcquireSRWLockExclusive(lock_);
			}

			OwnerExclusiveLock(const OwnerExclusiveLock&) = delete;
			OwnerExclusiveLock& operator=(const OwnerExclusiveLock&) = delete;

			~OwnerExclusiveLock() noexcept
			{
				ReleaseSRWLockExclusive(lock_);
			}

		private:
			SRWLOCK* lock_{};
		};

		alignas(OwnerState) std::byte g_ownerStorage[sizeof(OwnerState)]{};
		OwnerState& g_owner = *::new (static_cast<void*>(g_ownerStorage)) OwnerState{};
		std::atomic<std::uint64_t> g_nextGeneration{ 1 };
		std::atomic<std::uint64_t> g_nextLeaseToken{ 1 };
		std::atomic<int> g_ownerFrozenCodeContractState{ 0 };

		[[nodiscard]] bool PointerPlausible(const void* pointer) noexcept
		{
			const auto value = reinterpret_cast<std::uintptr_t>(pointer);
			return value >= kCanonicalPointerMinimum && value <= kCanonicalPointerMaximum &&
			       (value & (alignof(void*) - 1u)) == 0;
		}

		template <class T>
		[[nodiscard]] bool SafeRead(std::uintptr_t address, T& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			__try {
				std::memcpy(std::addressof(value), reinterpret_cast<const void*>(address), sizeof(T));
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool DeviceHealthy(ID3D11Device* device) noexcept
		{
			bool healthy = false;
			if (!device)
				return false;
			__try {
				healthy = device->GetDeviceRemovedReason() == S_OK;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				healthy = false;
			}
			return healthy;
		}

		[[nodiscard]] bool MatchOwnerRangeHash(const OwnerFrozenRange& range) noexcept
		{
			if (range.endRVAInclusive < range.beginRVA)
				return false;
			const auto size = static_cast<std::size_t>(range.endRVAInclusive - range.beginRVA + 1u);

			std::array<std::uint8_t, 0x800> bytes{};
			if (size == 0 || size > bytes.size())
				return false;
			__try {
				std::memcpy(bytes.data(), reinterpret_cast<const void*>(
					REL::Module::get().base() + range.beginRVA), size);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			DWORD objectLength = 0;
			DWORD resultLength = 0;
			std::array<std::uint8_t, 0x1000> hashObject{};
			std::array<std::uint8_t, 32> digest{};
			bool matched = false;
			if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
					std::addressof(algorithm), BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
				goto done;
			if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(std::addressof(objectLength)), sizeof(objectLength),
					std::addressof(resultLength), 0)) || objectLength > hashObject.size())
				goto done;
			if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, std::addressof(hash), hashObject.data(),
					objectLength, nullptr, 0, 0)) ||
				!BCRYPT_SUCCESS(BCryptHashData(hash, bytes.data(), static_cast<ULONG>(size), 0)) ||
				!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0)))
				goto done;
			matched = digest == range.sha256;

done:
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
			return matched;
		}

		[[nodiscard]] bool AttestOwnerFrozenCodeContract() noexcept
		{
			const int cached = g_ownerFrozenCodeContractState.load(std::memory_order_acquire);
			if (cached != 0)
				return cached > 0;
			std::uintptr_t directionalUpdate = 0;
			std::uintptr_t pointUpdate = 0;
			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
			const auto base = REL::Module::get().base();
			const bool attested = MatchOwnerRangeHash(kOwnerCreateRange) &&
				MatchOwnerRangeHash(kOwnerReleaseRange) &&
				MatchOwnerRangeHash(kOwnerDisableRange) &&
				MatchOwnerRangeHash(kOwnerGetByNameRange) &&
				MatchOwnerRangeHash(kOwnerAddPointRange) &&
				MatchOwnerRangeHash(kOwnerSetDirectionalRange) &&
				MatchOwnerRangeHash(kDirectionalUpdateRange) &&
				MatchOwnerRangeHash(kInterface3DUpdateLightsRange) &&
				MatchOwnerRangeHash(kProcessQueuedLightsRange) &&
				MatchOwnerRangeHash(kSetSunLightRange) &&
				MatchOwnerRangeHash(kNiAVObjectUpdateRange) &&
				SafeRead(base + kDirectionalVtableRVA + kDirectionalUpdateVtableOffset,
					directionalUpdate) && directionalUpdate == base + kDirectionalUpdateRVA &&
				SafeRead(pointVtable.address() + kDirectionalUpdateVtableOffset, pointUpdate) &&
				pointUpdate == base + kDirectionalUpdateRVA;
			g_ownerFrozenCodeContractState.store(attested ? 1 : -1, std::memory_order_release);
			return attested;
		}

		[[nodiscard]] bool FiniteColor(const RE::NiColor& color) noexcept
		{
			return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
			       color.r >= 0.0f && color.g >= 0.0f && color.b >= 0.0f &&
			       color.r <= kMaximumFiniteLightValue && color.g <= kMaximumFiniteLightValue &&
			       color.b <= kMaximumFiniteLightValue;
		}

		[[nodiscard]] bool FinitePoint(const RE::NiPoint3& point) noexcept
		{
			return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
			       std::fabs(point.x) <= kMaximumPointRadius && std::fabs(point.y) <= kMaximumPointRadius &&
			       std::fabs(point.z) <= kMaximumPointRadius;
		}

		[[nodiscard]] bool NearlyEqual(float lhs, float rhs, float absolute, float relative) noexcept
		{
			const float delta = std::fabs(lhs - rhs);
			return delta <= absolute || delta <= relative * std::max(std::fabs(lhs), std::fabs(rhs));
		}

		[[nodiscard]] bool OrthonormalRotation(const std::array<float, 12>& matrix) noexcept
		{
			for (std::size_t index = 0; index < matrix.size(); ++index) {
				const float value = matrix[index];
				if (!std::isfinite(value))
					return false;
				if (index % 4u == 3u) {

					if ((std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu) != 0u)
						return false;
				} else if (std::fabs(value) > 1.001f) {
					return false;
				}
			}
			for (std::size_t row = 0; row < 3; ++row) {
				float lengthSquared = 0.0f;
				for (std::size_t column = 0; column < 3; ++column)
					lengthSquared += matrix[row * 4 + column] * matrix[row * 4 + column];
				if (std::fabs(lengthSquared - 1.0f) > kRotationTolerance)
					return false;
			}
			for (std::size_t lhs = 0; lhs < 3; ++lhs) {
				for (std::size_t rhs = lhs + 1; rhs < 3; ++rhs) {
					float dot = 0.0f;
					for (std::size_t column = 0; column < 3; ++column)
						dot += matrix[lhs * 4 + column] * matrix[rhs * 4 + column];
					if (std::fabs(dot) > kRotationTolerance)
						return false;
				}
			}
			const float determinant =
				matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
				matrix[1] * (matrix[4] * matrix[10] - matrix[6] * matrix[8]) +
				matrix[2] * (matrix[4] * matrix[9] - matrix[5] * matrix[8]);
			return std::isfinite(determinant) &&
			       std::fabs(std::fabs(determinant) - 1.0f) <= kRotationTolerance;
		}

		[[nodiscard]] bool BuildPrivateDirectionalRotation(
			const std::array<float, 12>& liveWorldRotation,
			std::array<float, 12>& privateRotation) noexcept
		{

			for (std::size_t index = 0; index < liveWorldRotation.size(); ++index) {
				const float value = liveWorldRotation[index];
				if (!std::isfinite(value))
					return false;
				if (index % 4u == 3u) {
					if ((std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu) != 0u)
						return false;
				} else if (std::fabs(value) > 1.001f) {
					return false;
				}
			}

			const float dx = liveWorldRotation[0];
			const float dy = liveWorldRotation[1];
			const float dz = liveWorldRotation[2];
			const float directionLengthSquared = dx * dx + dy * dy + dz * dz;
			if (!std::isfinite(directionLengthSquared) ||
				std::fabs(directionLengthSquared - 1.0f) > kRotationTolerance)
				return false;

			const float hx = 0.0f;
			const float hy = std::fabs(dz) < 0.9f ? 0.0f : 1.0f;
			const float hz = std::fabs(dz) < 0.9f ? 1.0f : 0.0f;
			float r1x = hy * dz - hz * dy;
			float r1y = hz * dx - hx * dz;
			float r1z = hx * dy - hy * dx;
			const float r1LengthSquared = r1x * r1x + r1y * r1y + r1z * r1z;
			if (!std::isfinite(r1LengthSquared) || r1LengthSquared <= 1.0e-8f)
				return false;
			const float inverseR1Length = 1.0f / std::sqrt(r1LengthSquared);
			r1x *= inverseR1Length;
			r1y *= inverseR1Length;
			r1z *= inverseR1Length;

			const float r2x = dy * r1z - dz * r1y;
			const float r2y = dz * r1x - dx * r1z;
			const float r2z = dx * r1y - dy * r1x;
			privateRotation = { dx, dy, dz, 0.0f, r1x, r1y, r1z, 0.0f, r2x, r2y, r2z, 0.0f };
			return OrthonormalRotation(privateRotation);
		}

		[[nodiscard]] bool ValidateArrayHeader(
			std::byte* ssn,
			std::size_t dataOffset,
			std::size_t capacityOffset,
			std::size_t countOffset,
			std::uint32_t maximum,
			void*& data,
			std::uint32_t& capacity,
			std::uint32_t& count) noexcept
		{
			data = *reinterpret_cast<void**>(ssn + dataOffset);
			capacity = *reinterpret_cast<std::uint32_t*>(ssn + capacityOffset);
			count = *reinterpret_cast<std::uint32_t*>(ssn + countOffset);
			return count <= capacity && capacity <= maximum && (count == 0 || PointerPlausible(data));
		}

		void PinOrdinaryLightsWhileLocked(
			std::byte* ssn, std::uintptr_t baseLightVtable,
			PinnedOrdinaryLights& output) noexcept
		{
			void* ordinaryData = nullptr;
			std::uint32_t ordinaryCapacity = 0;
			std::uint32_t ordinaryCount = 0;
			void* shadowData = nullptr;
			std::uint32_t shadowCapacity = 0;
			std::uint32_t shadowCount = 0;
			void* portalData = nullptr;
			std::uint32_t portalCapacity = 0;
			std::uint32_t portalCount = 0;
			void* queuedAddData = nullptr;
			std::uint32_t queuedAddCapacity = 0;
			std::uint32_t queuedAddCount = 0;
			void* queuedRemoveData = nullptr;
			std::uint32_t queuedRemoveCapacity = 0;
			std::uint32_t queuedRemoveCount = 0;

			output.layoutValid =
				ValidateArrayHeader(ssn, kSSNOrdinaryLightArrayOffset,
					kSSNOrdinaryLightCapacityOffset, kSSNOrdinaryLightCountOffset,
					static_cast<std::uint32_t>(kMaximumLiveOrdinaryLights), ordinaryData,
					ordinaryCapacity, ordinaryCount) &&
				ValidateArrayHeader(ssn, kSSNShadowLightArrayOffset,
					kSSNShadowLightCapacityOffset, kSSNShadowLightCountOffset,
					static_cast<std::uint32_t>(kMaximumLiveOrdinaryLights), shadowData,
					shadowCapacity, shadowCount) &&
				ValidateArrayHeader(ssn, kSSNPortalLightArrayOffset,
					kSSNPortalLightCapacityOffset, kSSNPortalLightCountOffset,
					static_cast<std::uint32_t>(kMaximumLiveOrdinaryLights), portalData,
					portalCapacity, portalCount) &&
				ValidateArrayHeader(ssn, kSSNQueuedAddArrayOffset,
					kSSNQueuedAddCapacityOffset, kSSNQueuedAddCountOffset,
					static_cast<std::uint32_t>(kMaximumLiveOrdinaryLights), queuedAddData,
					queuedAddCapacity, queuedAddCount) &&
				ValidateArrayHeader(ssn, kSSNQueuedRemoveArrayOffset,
					kSSNQueuedRemoveCapacityOffset, kSSNQueuedRemoveCountOffset,
					static_cast<std::uint32_t>(kMaximumLiveOrdinaryLights), queuedRemoveData,
					queuedRemoveCapacity, queuedRemoveCount) &&
				queuedAddCount == 0 && queuedRemoveCount == 0;
			if (!output.layoutValid)
				return;

			auto** ordinary = reinterpret_cast<RE::BSLight**>(ordinaryData);
			for (std::uint32_t index = 0; index < ordinaryCount; ++index) {
				auto* light = ordinary[index];
				if (!PointerPlausible(light)) {
					output.layoutValid = false;
					break;
				}
				if (*reinterpret_cast<const std::uintptr_t*>(light) != baseLightVtable)
					continue;
				light->IncRefCount();
				output.lights[output.count++] = light;
			}
		}

		void PinSunWhileLocked(
			std::byte* ssn, std::uintptr_t baseLightVtable,
			std::uintptr_t shadowDirectionalVtable, std::uintptr_t directionalVtable,
			PinnedSun& output) noexcept
		{
			auto* baseWrapper = *reinterpret_cast<void**>(ssn + kSSNBaseSunOffset);
			auto* shadowWrapper = *reinterpret_cast<void**>(ssn + kSSNShadowSunOffset);
			if (!baseWrapper && !shadowWrapper) {
				output.sourceAbsent = true;
				return;
			}
			if (!PointerPlausible(baseWrapper) ||
				*reinterpret_cast<std::uintptr_t*>(baseWrapper) != baseLightVtable)
				return;
			auto* baseDirectional = *reinterpret_cast<RE::NiDirectionalLight**>(
				reinterpret_cast<std::byte*>(baseWrapper) + kBSLightNiLightOffset);
			if (!PointerPlausible(baseDirectional) ||
				*reinterpret_cast<std::uintptr_t*>(baseDirectional) != directionalVtable)
				return;
			if (shadowWrapper) {
				if (!PointerPlausible(shadowWrapper) ||
					*reinterpret_cast<std::uintptr_t*>(shadowWrapper) != shadowDirectionalVtable)
					return;
				auto* shadowDirectional = *reinterpret_cast<RE::NiDirectionalLight**>(
					reinterpret_cast<std::byte*>(shadowWrapper) + kBSLightNiLightOffset);
				if (shadowDirectional != baseDirectional)
					return;
			}
			baseDirectional->IncRefCount();
			output.light = baseDirectional;
			output.wrappersAttested = true;
		}

		void PinOrdinaryLightsLeaf(
			void* liveShadowSceneNode, std::uintptr_t baseLightVtable,
			PinnedOrdinaryLights& output) noexcept
		{
			bool locked = false;
			auto* ssn = reinterpret_cast<std::byte*>(liveShadowSceneNode);
			auto* lock = reinterpret_cast<RE::BSSpinLock*>(ssn + kSSNLightLockOffset);
			__try {
				__try {
					lock->lock("DynRefFlatPrivateLights");
					locked = true;
					PinOrdinaryLightsWhileLocked(ssn, baseLightVtable, output);
				} __except (output.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
					output.layoutValid = false;
				}
			} __finally {
				if (locked)
					lock->unlock();
			}
		}

		void ReleasePinnedOrdinaryLights(PinnedOrdinaryLights& pinned) noexcept
		{
			for (std::uint32_t index = 0; index < pinned.count; ++index) {
				auto* light = pinned.lights[index];
				if (!light)
					continue;
				__try {
					light->DecRefCount();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
				}
				pinned.lights[index] = nullptr;
			}
			pinned.count = 0;
		}

		void PinLightingGraphLeaf(
			void* liveShadowSceneNode, std::uintptr_t baseLightVtable,
			std::uintptr_t shadowDirectionalVtable, std::uintptr_t directionalVtable,
			PinnedSun& sun, PinnedOrdinaryLights& ordinary) noexcept
		{
			bool locked = false;
			auto* ssn = reinterpret_cast<std::byte*>(liveShadowSceneNode);
			auto* lock = reinterpret_cast<RE::BSSpinLock*>(ssn + kSSNLightLockOffset);
			__try {
				__try {
					lock->lock("DynRefFlatPrivateLighting");
					locked = true;
					
					PinSunWhileLocked(ssn, baseLightVtable, shadowDirectionalVtable,
						directionalVtable, sun);
					PinOrdinaryLightsWhileLocked(ssn, baseLightVtable, ordinary);
				} __except (sun.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
					sun.wrappersAttested = false;
					sun.sourceAbsent = false;
					ordinary.exceptionCode = sun.exceptionCode;
					ordinary.layoutValid = false;
				}
			} __finally {
				if (locked)
					lock->unlock();
			}
		}

		void ReleasePinnedSun(PinnedSun& pinned) noexcept
		{
			if (!pinned.light)
				return;
			__try {
				pinned.light->DecRefCount();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			pinned.light = nullptr;
		}

		[[nodiscard]] bool ReadDirectionalSnapshot(
			const PinnedSun& pin,
			DirectionalLightSnapshot& output) noexcept
		{
			if (!pin.wrappersAttested || !pin.light)
				return false;
			const auto address = reinterpret_cast<std::uintptr_t>(pin.light);
			output.lightIdentity = address;
			std::array<float, 12> liveWorldRotation{};
			if (!SafeRead(address + kNiAVWorldRotationOffset, liveWorldRotation) ||
				!SafeRead(address + kNiLightPODOffset, output.lightPOD) ||
				!BuildPrivateDirectionalRotation(liveWorldRotation, output.worldRotation))
				return false;
			std::array<float, kNiLightPODSize / sizeof(float)> values{};
			std::memcpy(values.data(), output.lightPOD.data(), output.lightPOD.size());
			for (const float value : values) {
				if (!std::isfinite(value) || value < 0.0f || value > kMaximumFiniteLightValue)
					return false;
			}

			const float diffuseEnergy = values[3] + values[4] + values[5];
			output.contributes = values[9] > 0.0f && diffuseEnergy > 0.0001f;
			output.valid = true;
			return true;
		}

		void MakeNeutralDirectionalSnapshot(DirectionalLightSnapshot& output) noexcept
		{
			output = {};
			output.worldRotation = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			};

			output.valid = true;
			output.contributes = false;
		}

		[[nodiscard]] PrivateDirectionalLightValues ExportDirectionalValues(
			const DirectionalLightSnapshot& snapshot) noexcept
		{
			PrivateDirectionalLightValues values{};
			values.localRotation = snapshot.worldRotation;
			std::memcpy(values.lightPOD.data(), snapshot.lightPOD.data(), snapshot.lightPOD.size());
			return values;
		}

		[[nodiscard]] PrivatePointLightValues ExportPointValues(
			const PointLightSnapshot& snapshot) noexcept
		{
			return {
				{ snapshot.translation.x, snapshot.translation.y, snapshot.translation.z },
				{ snapshot.diffuse.r, snapshot.diffuse.g, snapshot.diffuse.b },
				snapshot.radius,
				snapshot.dimmer
			};
		}

		[[nodiscard]] bool ReadPointSnapshot(
			RE::BSLight* wrapper,
			std::uintptr_t baseLightVtable,
			std::uintptr_t pointVtable,
			PointLightSnapshot& output) noexcept
		{
			if (!wrapper)
				return false;
			const auto wrapperAddress = reinterpret_cast<std::uintptr_t>(wrapper);
			std::uintptr_t actualWrapperVtable = 0;
			std::uintptr_t lightAddress = 0;
			std::uintptr_t actualLightVtable = 0;
			std::uint64_t flags = 0;
			std::uint32_t shape = 0;
			std::uint8_t portal = 0;
			float lodDimmer = 0.0f;
			float niFade = 0.0f;
			float radius0 = 0.0f;
			float radius1 = 0.0f;
			float radius2 = 0.0f;
			if (!SafeRead(wrapperAddress, actualWrapperVtable) || actualWrapperVtable != baseLightVtable ||
				!SafeRead(wrapperAddress + kBSLightNiLightOffset, lightAddress) ||
				!PointerPlausible(reinterpret_cast<void*>(lightAddress)) ||
				!SafeRead(lightAddress, actualLightVtable) || actualLightVtable != pointVtable ||
				!SafeRead(wrapperAddress + kBSLightShapeOffset, shape) || shape != kOrdinaryPointShape ||
				!SafeRead(wrapperAddress + kBSLightPortalByteOffset, portal) || portal != 0 ||
				!SafeRead(wrapperAddress + kBSLightLodDimmerOffset, lodDimmer) ||
				!SafeRead(lightAddress + kNiAVFlagsOffset, flags) || (flags & kNiAVAppCulledBit) != 0 ||
				!SafeRead(lightAddress + kNiAVWorldTranslationOffset, output.translation) ||
				!SafeRead(lightAddress + kNiLightDiffuseOffset, output.diffuse) ||
				!SafeRead(lightAddress + kNiPointRadiusOffset, radius0) ||
				!SafeRead(lightAddress + kNiPointRadiusLane1Offset, radius1) ||
				!SafeRead(lightAddress + kNiPointRadiusLane2Offset, radius2) ||
				!SafeRead(lightAddress + kNiLightFadeOffset, niFade))
				return false;

			if (!FinitePoint(output.translation) || !FiniteColor(output.diffuse) ||
				!std::isfinite(radius0) || radius0 < kMinimumPointRadius || radius0 > kMaximumPointRadius ||
				!NearlyEqual(radius0, radius1, 0.001f, 0.0001f) ||
				!NearlyEqual(radius0, radius2, 0.001f, 0.0001f) ||
				!std::isfinite(niFade) || niFade <= 0.0f || niFade > kMaximumFiniteLightValue ||
				!std::isfinite(lodDimmer) || lodDimmer < 0.0f || lodDimmer > kMaximumFiniteLightValue)
				return false;

			const float dimmer = niFade * (lodDimmer > 0.0f ? lodDimmer : 1.0f);
			if (!std::isfinite(dimmer) || dimmer <= 0.0f || dimmer > kMaximumFiniteLightValue)
				return false;
			output.wrapperIdentity = wrapperAddress;
			output.lightIdentity = lightAddress;
			output.radius = radius0;
			output.dimmer = dimmer;
			return true;
		}

		[[nodiscard]] float RankPointLight(
			const PointLightSnapshot& light,
			const RE::NiBound& playerBound) noexcept
		{
			const float dx = light.translation.x - playerBound.center.x;
			const float dy = light.translation.y - playerBound.center.y;
			const float dz = light.translation.z - playerBound.center.z;
			const float distanceSquared = dx * dx + dy * dy + dz * dz;
			if (!std::isfinite(distanceSquared))
				return 0.0f;
			const float distance = std::sqrt(std::max(0.0f, distanceSquared));
			if (distance > light.radius + playerBound.fRadius)
				return 0.0f;
			const float luminance =
				0.2126f * light.diffuse.r + 0.7152f * light.diffuse.g + 0.0722f * light.diffuse.b;

			const float surfaceDistance = std::max(0.0f, distance - playerBound.fRadius);
			const float attenuation = std::max(0.0f, 1.0f - surfaceDistance / light.radius);
			const float score = luminance * light.dimmer * attenuation;
			return std::isfinite(score) && score > 0.0f ? score : 0.0f;
		}

		[[nodiscard]] float RankPointLightAgainstBounds(
			const PointLightSnapshot& light,
			const RE::NiBound* bounds,
			std::size_t boundCount) noexcept
		{
			if (!bounds || boundCount == 0)
				return 0.0f;
			const float luminance =
				0.2126f * light.diffuse.r + 0.7152f * light.diffuse.g + 0.0722f * light.diffuse.b;
			float strongestAttenuation = 0.0f;
			for (std::size_t index = 0; index < boundCount; ++index) {
				const auto& bound = bounds[index];
				const float dx = light.translation.x - bound.center.x;
				const float dy = light.translation.y - bound.center.y;
				const float dz = light.translation.z - bound.center.z;
				const float distanceSquared = dx * dx + dy * dy + dz * dz;
				if (!std::isfinite(distanceSquared))
					return 0.0f;
				const float distance = std::sqrt((std::max)(0.0f, distanceSquared));
				if (distance > light.radius + bound.fRadius)
					continue;

				const float surfaceDistance = (std::max)(0.0f, distance - bound.fRadius);
				const float normalizedDistance = (std::min)(surfaceDistance / light.radius, 1.0f);
				const float attenuation = 1.0f - normalizedDistance * normalizedDistance;
				strongestAttenuation = (std::max)(strongestAttenuation, attenuation);
			}
			const float score = luminance * light.dimmer * strongestAttenuation;
			return std::isfinite(score) && score > 0.0f ? score : 0.0f;
		}

		[[nodiscard]] std::uint64_t HashPointTopology(const LightingSnapshot& snapshot) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			auto add = [&hash](std::uint64_t value) noexcept {
				for (std::uint32_t byte = 0; byte < 8; ++byte) {
					hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
					hash *= 1099511628211ull;
				}
			};
			add(snapshot.pointCount);
			for (std::uint32_t index = 0; index < snapshot.pointCount; ++index) {
				const auto& light = snapshot.points[index];
				add(light.wrapperIdentity);
				add(light.lightIdentity);
			}
			return hash ? hash : 1ull;
		}

		[[nodiscard]] bool SnapshotLighting(const AcquireRequest& request, LightingSnapshot& output) noexcept
		{
			static std::atomic_uint32_t failureLogs{ 0u };
			REL::Relocation<std::uintptr_t> baseLightVtable{ RE::VTABLE::BSLight[0] };
			REL::Relocation<std::uintptr_t> shadowDirectionalVtable{ RE::VTABLE::BSShadowDirectionalLight[0] };
			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };

			PinnedSun sun{};
			PinnedOrdinaryLights pinned{};
			PinLightingGraphLeaf(request.liveShadowSceneNode, baseLightVtable.address(),
				shadowDirectionalVtable.address(), directionalVtable.address(), sun, pinned);
			const bool directionalRead = ReadDirectionalSnapshot(sun, output.directional);
			const bool sunAbsent = sun.sourceAbsent && sun.exceptionCode == 0u;
			const auto sunPointer = reinterpret_cast<std::uintptr_t>(sun.light);
			const auto sunException = sun.exceptionCode;
			const auto sunWrappersAttested = sun.wrappersAttested;
			ReleasePinnedSun(sun);
			if (!directionalRead && !sunAbsent) {
				if (failureLogs.fetch_add(1u, std::memory_order_relaxed) < 12u) {
					logger::warn(
						"[FlatReflectionOwner] live lighting snapshot rejected at sun: ssn={:p} "
						"wrappers={} light=0x{:X} exception=0x{:X}",
						request.liveShadowSceneNode, sunWrappersAttested, sunPointer, sunException);
					logger::warn(
						"[FlatReflectionOwner] sun values rotation=[{},{},{},{};{},{},{},{};{},{},{},{}] "
						"pod=[{},{},{},{},{},{},{},{},{},{}]",
						output.directional.worldRotation[0], output.directional.worldRotation[1],
						output.directional.worldRotation[2], output.directional.worldRotation[3],
						output.directional.worldRotation[4], output.directional.worldRotation[5],
						output.directional.worldRotation[6], output.directional.worldRotation[7],
						output.directional.worldRotation[8], output.directional.worldRotation[9],
						output.directional.worldRotation[10], output.directional.worldRotation[11],
						output.directional.lightPOD[0], output.directional.lightPOD[1],
						output.directional.lightPOD[2], output.directional.lightPOD[3],
						output.directional.lightPOD[4], output.directional.lightPOD[5],
						output.directional.lightPOD[6], output.directional.lightPOD[7],
						output.directional.lightPOD[8], output.directional.lightPOD[9]);
				}
				ReleasePinnedOrdinaryLights(pinned);
				return false;
			}
			if (sunAbsent)
				MakeNeutralDirectionalSnapshot(output.directional);

			if (!pinned.layoutValid) {
				if (failureLogs.fetch_add(1u, std::memory_order_relaxed) < 12u)
					logger::warn(
						"[FlatReflectionOwner] live lighting snapshot rejected at ordinary graph: "
						"ssn={:p} count={} exception=0x{:X}",
						request.liveShadowSceneNode, pinned.count, pinned.exceptionCode);
				ReleasePinnedOrdinaryLights(pinned);
				return false;
			}

			const auto ranksBefore = [](const PointLightSnapshot& lhs, const PointLightSnapshot& rhs) noexcept {
				if (lhs.score != rhs.score)
					return lhs.score > rhs.score;
				if (lhs.lightIdentity != rhs.lightIdentity)
					return lhs.lightIdentity < rhs.lightIdentity;
				return lhs.wrapperIdentity < rhs.wrapperIdentity;
			};
			for (std::uint32_t index = 0; index < pinned.count; ++index) {
				const auto wrapperIdentity = reinterpret_cast<std::uintptr_t>(pinned.lights[index]);
				std::uintptr_t lightIdentity = 0;
				if (!SafeRead(wrapperIdentity + kBSLightNiLightOffset, lightIdentity) ||
					!PointerPlausible(reinterpret_cast<void*>(lightIdentity)) ||
					output.sourceIdentityCount >= output.sourceLightIdentities.size()) {
					ReleasePinnedOrdinaryLights(pinned);
					return false;
				}
				for (std::uint32_t prior = 0; prior < output.sourceIdentityCount; ++prior) {
					if (output.sourceWrapperIdentities[prior] == wrapperIdentity ||
						output.sourceLightIdentities[prior] == lightIdentity) {
						ReleasePinnedOrdinaryLights(pinned);
						return false;
					}
				}

				output.sourceWrapperIdentities[output.sourceIdentityCount] = wrapperIdentity;
				output.sourceLightIdentities[output.sourceIdentityCount] = lightIdentity;
				++output.sourceIdentityCount;

				PointLightSnapshot sample{};
				if (!ReadPointSnapshot(pinned.lights[index], baseLightVtable.address(), pointVtable.address(), sample))
					continue;
				if (sample.wrapperIdentity != wrapperIdentity || sample.lightIdentity != lightIdentity) {
					ReleasePinnedOrdinaryLights(pinned);
					return false;
				}
				sample.score = request.admittedWorldBounds ?
					RankPointLightAgainstBounds(
						sample, request.admittedWorldBounds, request.admittedWorldBoundCount) :
					RankPointLight(sample, request.playerWorldBound);
				if (sample.score <= 0.0f)
					continue;
				std::uint32_t insertAt = 0;
				while (insertAt < output.pointCount && !ranksBefore(sample, output.points[insertAt]))
					++insertAt;
				if (insertAt >= output.points.size())
					continue;
				output.pointCount = static_cast<std::uint32_t>(
					std::min<std::size_t>(output.pointCount + 1u, output.points.size()));
				for (std::uint32_t move = output.pointCount; move > insertAt + 1u; --move)
					output.points[move - 1u] = output.points[move - 2u];
				output.points[insertAt] = sample;
			}
			ReleasePinnedOrdinaryLights(pinned);
			output.pointTopologySignature = HashPointTopology(output);
			return true;
		}

		[[nodiscard]] bool SnapshotPointLighting(
			const PointLightSnapshotRequest& request,
			LightingSnapshot& output) noexcept
		{
			REL::Relocation<std::uintptr_t> baseLightVtable{ RE::VTABLE::BSLight[0] };
			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
			PinnedOrdinaryLights pinned{};
			PinOrdinaryLightsLeaf(request.liveShadowSceneNode, baseLightVtable.address(), pinned);
			if (!pinned.layoutValid) {
				ReleasePinnedOrdinaryLights(pinned);
				return false;
			}

			const auto ranksBefore = [](const PointLightSnapshot& lhs, const PointLightSnapshot& rhs) noexcept {
				if (lhs.score != rhs.score)
					return lhs.score > rhs.score;
				if (lhs.lightIdentity != rhs.lightIdentity)
					return lhs.lightIdentity < rhs.lightIdentity;
				return lhs.wrapperIdentity < rhs.wrapperIdentity;
			};
			for (std::uint32_t index = 0; index < pinned.count; ++index) {
				const auto wrapperIdentity = reinterpret_cast<std::uintptr_t>(pinned.lights[index]);
				std::uintptr_t lightIdentity = 0;
				if (!SafeRead(wrapperIdentity + kBSLightNiLightOffset, lightIdentity) ||
					!PointerPlausible(reinterpret_cast<void*>(lightIdentity)) ||
					output.sourceIdentityCount >= output.sourceLightIdentities.size()) {
					ReleasePinnedOrdinaryLights(pinned);
					return false;
				}
				for (std::uint32_t prior = 0; prior < output.sourceIdentityCount; ++prior) {
					if (output.sourceWrapperIdentities[prior] == wrapperIdentity ||
						output.sourceLightIdentities[prior] == lightIdentity) {
						ReleasePinnedOrdinaryLights(pinned);
						return false;
					}
				}
				output.sourceWrapperIdentities[output.sourceIdentityCount] = wrapperIdentity;
				output.sourceLightIdentities[output.sourceIdentityCount] = lightIdentity;
				++output.sourceIdentityCount;

				PointLightSnapshot sample{};
				if (!ReadPointSnapshot(
						pinned.lights[index], baseLightVtable.address(), pointVtable.address(), sample))
					continue;
				if (sample.wrapperIdentity != wrapperIdentity || sample.lightIdentity != lightIdentity) {
					ReleasePinnedOrdinaryLights(pinned);
					return false;
				}
				sample.score = RankPointLightAgainstBounds(
					sample, request.admittedWorldBounds, request.admittedWorldBoundCount);
				if (sample.score <= 0.0f)
					continue;
				std::uint32_t insertAt = 0;
				while (insertAt < output.pointCount && !ranksBefore(sample, output.points[insertAt]))
					++insertAt;
				if (insertAt >= output.points.size())
					continue;
				output.pointCount = static_cast<std::uint32_t>(
					(std::min<std::size_t>)(output.pointCount + 1u, output.points.size()));
				for (std::uint32_t move = output.pointCount; move > insertAt + 1u; --move)
					output.points[move - 1u] = output.points[move - 2u];
				output.points[insertAt] = sample;
			}
			ReleasePinnedOrdinaryLights(pinned);
			output.pointTopologySignature = HashPointTopology(output);
			return true;
		}

		struct EngineCallResult
		{
			std::uint32_t exceptionCode{};
			bool completed{};
		};

		struct RendererCreateResult
		{
			RE::Interface3D::Renderer* renderer{};
			std::uint32_t exceptionCode{};
			bool registryCollision{};
			bool completed{};
		};

		struct ModelRootCreateResult
		{
			RE::NiNode* modelRoot{};
			std::uint32_t exceptionCode{};
			bool completed{};
		};

		void CreateRendererLeaf(
			const RE::BSFixedString& name,
			RendererCreateResult& result) noexcept
		{
			__try {
				if (RE::Interface3D::Renderer::GetByName(name) != nullptr) {
					result.registryCollision = true;
					return;
				}
				result.renderer = RE::Interface3D::Renderer::Create(
					name, RE::UI_DEPTH_PRIORITY::kUndefined, 90.0f, false);
				if (result.renderer)
					result.renderer->Disable();
				result.completed = result.renderer != nullptr && !result.renderer->enabled &&
					RE::Interface3D::Renderer::GetByName(name) == result.renderer;
			} __except (result.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				result.completed = false;
			}
		}

		__declspec(noinline) RE::NiNode* CreateDetachedModelRootUnprotected()
		{
			return new RE::NiNode(static_cast<std::uint16_t>(0));
		}

		void CreateDetachedModelRootLeaf(ModelRootCreateResult& result) noexcept
		{
			result = {};
			__try {
				result.modelRoot = CreateDetachedModelRootUnprotected();
				result.completed = result.modelRoot != nullptr;
			} __except (result.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				result.completed = false;
			}
		}

		bool ReleaseRendererLeaf(
			RE::Interface3D::Renderer* renderer,
			const RE::BSFixedString& registryName,
			EngineCallResult& call) noexcept
		{
			if (!renderer)
				return true;
			__try {
				renderer->Release();
				call.completed = RE::Interface3D::Renderer::GetByName(registryName) == nullptr;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				call.completed = false;
			}
			return call.completed;
		}

		bool ConfigurePrivateLightsLeaf(
			RE::Interface3D::Renderer* renderer,
			const LightingSnapshot& lighting,
			EngineCallResult& call) noexcept
		{
			__try {
				renderer->offscreen3DEnabled = false;
				renderer->MainScreen_SetUseDeferredRenderer(true);

				renderer->MainScreen_SetClearDepthStencil(true);
				renderer->MainScreen_EnableAO(false);
				renderer->MainScreen_SetPostAA(false);
				renderer->alwaysRenderWhenEnabled = false;
				renderer->Offscreen_SetRenderTargetSize(
					RE::Interface3D::OffscreenMenuSize::kPipboy);
				renderer->Offscreen_SetPostEffect(RE::Interface3D::PostEffect::kHUDGlassWithMod);
				renderer->Offscreen_SetDirectionalLight(0.0f, 0.0f, RE::NiColor{}, 0.0f);
				for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
					const auto& light = lighting.points[index];
					renderer->MainScreen_AddPointLight(
						light.translation, light.diffuse, light.radius, light.dimmer);
				}
				call.completed = true;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				call.completed = false;
			}
			return call.completed;
		}

		bool ApplyDirectionalSnapshotLeaf(
			RE::NiAVObject* destination,
			const DirectionalLightSnapshot& source,
			EngineCallResult& call) noexcept
		{
			if (!destination || !source.valid)
				return false;
			__try {
				if (destination->parent != nullptr)
					return false;
				auto* bytes = reinterpret_cast<std::byte*>(destination);
				std::memcpy(bytes + kNiAVLocalRotationOffset,
					source.worldRotation.data(), sizeof(source.worldRotation));
				std::memcpy(bytes + kNiLightPODOffset, source.lightPOD.data(), source.lightPOD.size());
				RE::NiUpdateData update{};
				destination->Update(update);
				call.completed = true;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				call.completed = false;
			}
			return call.completed;
		}

		[[nodiscard]] bool AttestAppliedDirectional(
			RE::NiAVObject* light,
			const PrivateDirectionalLightValues& expected) noexcept
		{
			if (!light)
				return false;
			const auto address = reinterpret_cast<std::uintptr_t>(light);
			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			std::uintptr_t actualVtable = 0;
			std::array<float, 12> localRotation{};
			std::array<float, 12> worldRotation{};
			std::array<float, 10> lightPOD{};
			void* parent = nullptr;
			std::uint64_t flags = 0;
			return SafeRead(address, actualVtable) && actualVtable == directionalVtable.address() &&
			       SafeRead(address + 0x28, parent) && parent == nullptr &&
			       SafeRead(address + kNiAVLocalRotationOffset, localRotation) &&
			       SafeRead(address + kNiAVWorldRotationOffset, worldRotation) &&
			       SafeRead(address + kNiAVFlagsOffset, flags) && (flags & kNiAVAppCulledBit) == 0 &&
			       SafeRead(address + kNiLightPODOffset, lightPOD) &&
			       localRotation == expected.localRotation && worldRotation == expected.localRotation &&
			       lightPOD == expected.lightPOD;
		}

		[[nodiscard]] bool AttestPrivatePointValues(
			RE::NiAVObject* privateLight,
			const PrivatePointLightValues& expected,
			std::uintptr_t pointVtable) noexcept
		{
			if (!privateLight)
				return false;
			const auto address = reinterpret_cast<std::uintptr_t>(privateLight);
			std::uintptr_t vtable = 0;
			RE::NiPoint3 translation{};
			RE::NiColor diffuse{};
			float radius0 = 0.0f;
			float radius1 = 0.0f;
			float radius2 = 0.0f;
			float dimmer = 0.0f;
			return SafeRead(address, vtable) && vtable == pointVtable &&
			       SafeRead(address + kNiAVLocalTranslationOffset, translation) &&
			       SafeRead(address + kNiLightDiffuseOffset, diffuse) &&
			       SafeRead(address + kNiPointRadiusOffset, radius0) &&
			       SafeRead(address + kNiPointRadiusLane1Offset, radius1) &&
			       SafeRead(address + kNiPointRadiusLane2Offset, radius2) &&
			       SafeRead(address + kNiLightFadeOffset, dimmer) &&
			       std::memcmp(std::addressof(translation), expected.translation.data(), sizeof(translation)) == 0 &&
			       std::memcmp(std::addressof(diffuse), expected.diffuse.data(), sizeof(diffuse)) == 0 &&
			       std::bit_cast<std::uint32_t>(radius0) == std::bit_cast<std::uint32_t>(expected.radius) &&
			       std::bit_cast<std::uint32_t>(radius1) == 0u && std::bit_cast<std::uint32_t>(radius2) == 0u &&
			       std::bit_cast<std::uint32_t>(dimmer) == std::bit_cast<std::uint32_t>(expected.dimmer);
		}

		[[nodiscard]] bool AttestPrivatePoint(
			RE::NiAVObject* privateLight,
			const PointLightSnapshot& source,
			std::uintptr_t pointVtable) noexcept
		{
			return privateLight && reinterpret_cast<std::uintptr_t>(privateLight) != source.lightIdentity &&
			       AttestPrivatePointValues(privateLight, ExportPointValues(source), pointVtable);
		}

		bool ApplyPointSnapshotsLeaf(
			OwnerState& owner,
			const LightingSnapshot& lighting,
			EngineCallResult& call) noexcept
		{
			if (owner.pointLightCount != lighting.pointCount || !owner.shadowSceneNode)
				return false;
			__try {
				REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
				for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
					auto* destination = owner.pointLights[index].get();
					const auto expected = ExportPointValues(lighting.points[index]);
					if (!destination || destination->parent != owner.shadowSceneNode.get() ||
						!AttestPrivatePointValues(destination, owner.pointLightValues[index],
							pointVtable.address()))
						return false;

					auto* bytes = reinterpret_cast<std::byte*>(destination);
					std::memcpy(bytes + kNiAVLocalTranslationOffset,
						expected.translation.data(), sizeof(expected.translation));
					std::memcpy(bytes + kNiLightDiffuseOffset,
						expected.diffuse.data(), sizeof(expected.diffuse));
					std::memcpy(bytes + kNiPointRadiusOffset,
						std::addressof(expected.radius), sizeof(expected.radius));
					constexpr float zero = 0.0f;
					std::memcpy(bytes + kNiPointRadiusLane1Offset, std::addressof(zero), sizeof(zero));
					std::memcpy(bytes + kNiPointRadiusLane2Offset, std::addressof(zero), sizeof(zero));
					std::memcpy(bytes + kNiLightFadeOffset,
						std::addressof(expected.dimmer), sizeof(expected.dimmer));
					RE::NiUpdateData update{};
					destination->Update(update);
					if (!AttestPrivatePointValues(destination, expected, pointVtable.address()))
						return false;
				}
				call.completed = true;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				call.completed = false;
			}
			return call.completed;
		}

		[[nodiscard]] bool AttestAndPinGeneration(
			RE::Interface3D::Renderer* renderer,
			const LightingSnapshot& lighting,
			OwnerState& owner) noexcept
		{
			if (!renderer || renderer->enabled || renderer->offscreen3DEnabled || renderer->enableAO ||
				renderer->postAA || renderer->alwaysRenderWhenEnabled || renderer->worldAttachedElementRoot ||
				renderer->screenAttachedElementRoot || renderer->offscreenElement || !renderer->accum ||
				!renderer->screenSSN || !renderer->offscreenSSN ||
				renderer->screenSSN.get() == renderer->offscreenSSN.get() || !renderer->directionalLight ||
				renderer->screenSSN.get() == owner.liveShadowSceneNode ||
				renderer->offscreenSSN.get() == owner.liveShadowSceneNode ||
				renderer->mainLights.size() != lighting.pointCount ||
				renderer->postfx.get() != RE::Interface3D::PostEffect::kHUDGlassWithMod)
				return false;

			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
			std::uintptr_t actualDirectionalVtable = 0;
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(renderer->directionalLight), actualDirectionalVtable) ||
				actualDirectionalVtable != directionalVtable.address() ||
				reinterpret_cast<std::uintptr_t>(renderer->directionalLight) == lighting.directional.lightIdentity)
				return false;
			for (std::uint32_t source = 0; source < lighting.sourceIdentityCount; ++source) {
				if (reinterpret_cast<std::uintptr_t>(renderer->directionalLight) ==
						lighting.sourceLightIdentities[source] ||
					reinterpret_cast<std::uintptr_t>(renderer->directionalLight) ==
						lighting.sourceWrapperIdentities[source])
					return false;
			}

			owner.accumulator = renderer->accum.get();
			owner.shadowSceneNode.reset(renderer->screenSSN.get());
			owner.directionalLight.reset(renderer->directionalLight);
			owner.directionalLightValues = ExportDirectionalValues(lighting.directional);
			owner.pointLightCount = lighting.pointCount;
			for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
				const auto& params = renderer->mainLights[index];
				if (params.type.get() != RE::Interface3D::LightType::kPoint || params.lookAtObject || !params.light ||
					!AttestPrivatePoint(params.light.get(), lighting.points[index], pointVtable.address()))
					return false;
				if (reinterpret_cast<std::uintptr_t>(params.light.get()) == lighting.directional.lightIdentity)
					return false;
				for (std::uint32_t source = 0; source < lighting.sourceIdentityCount; ++source) {
					if (reinterpret_cast<std::uintptr_t>(params.light.get()) ==
							lighting.sourceLightIdentities[source] ||
						reinterpret_cast<std::uintptr_t>(params.light.get()) ==
							lighting.sourceWrapperIdentities[source])
						return false;
				}
				for (std::uint32_t prior = 0; prior < index; ++prior) {
					if (renderer->mainLights[prior].light.get() == params.light.get())
						return false;
				}
				owner.pointLights[index].reset(params.light.get());
				owner.pointLightValues[index] = ExportPointValues(lighting.points[index]);
			}
			for (std::size_t index = lighting.pointCount; index < owner.pointLights.size(); ++index) {
				owner.pointLights[index].reset();
				owner.pointLightValues[index] = {};
			}
			return true;
		}

		[[nodiscard]] bool AttestDetachedEmptyModelRoot(const OwnerState& owner) noexcept
		{
			auto* modelRoot = owner.modelRoot.get();
			if (!PointerPlausible(modelRoot) || modelRoot == owner.shadowSceneNode.get() ||
				modelRoot == owner.directionalLight.get() || modelRoot == owner.liveShadowSceneNode)
				return false;
			for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
				if (modelRoot == owner.pointLights[index].get())
					return false;
			}

			const auto address = reinterpret_cast<std::uintptr_t>(modelRoot);
			std::uintptr_t vtable = 0;
			void* parent = reinterpret_cast<void*>(1);
			std::uint64_t flags = 0;
			void* childData = nullptr;
			std::uint16_t childCapacity = 0;
			std::uint16_t childCount = 0;
			REL::Relocation<std::uintptr_t> expectedVtable{ RE::VTABLE::NiNode[0] };
			return SafeRead(address, vtable) && vtable == expectedVtable.address() &&
			       SafeRead(address + 0x28, parent) && parent == nullptr &&
			       SafeRead(address + kNiAVFlagsOffset, flags) &&
			       (flags & (kNiAVAppCulledBit | kNiAVFlattenedFadeRouteBit)) == 0 &&
			       SafeRead(address + kSSNChildrenDataOffset, childData) &&
			       SafeRead(address + kSSNChildrenCapacityOffset, childCapacity) &&
			       SafeRead(address + kSSNChildrenCountOffset, childCount) && childCount == 0 &&
			       childCapacity <= 64u &&
			       (childCapacity == 0 ? childData == nullptr : PointerPlausible(childData));
		}

		[[nodiscard]] bool AttestExistingGeneration(const OwnerState& owner) noexcept
		{
			bool attested = false;
			__try {
				REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
				auto* renderer = owner.renderer;
				if (!renderer || !PointerPlausible(owner.accumulator) ||
					owner.pointLightCount > Lease::kMaximumPointLights ||
					renderer->enabled || renderer->offscreen3DEnabled || renderer->enableAO ||
					renderer->postAA || renderer->alwaysRenderWhenEnabled || renderer->worldAttachedElementRoot ||
					renderer->screenAttachedElementRoot || renderer->offscreenElement ||
					renderer->postfx.get() != RE::Interface3D::PostEffect::kHUDGlassWithMod ||
					RE::Interface3D::Renderer::GetByName(owner.registryName) != renderer ||
					renderer->accum.get() != owner.accumulator ||
					!renderer->screenSSN || !renderer->offscreenSSN ||
					renderer->screenSSN.get() != owner.shadowSceneNode.get() ||
					renderer->screenSSN.get() == renderer->offscreenSSN.get() ||
					renderer->screenSSN.get() == owner.liveShadowSceneNode ||
					renderer->offscreenSSN.get() == owner.liveShadowSceneNode ||
					renderer->directionalLight != owner.directionalLight.get() ||
					renderer->mainLights.size() != owner.pointLightCount ||
					!AttestDetachedEmptyModelRoot(owner) ||
					!AttestAppliedDirectional(owner.directionalLight.get(), owner.directionalLightValues))
					return false;
				for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
					if (renderer->mainLights[index].type.get() != RE::Interface3D::LightType::kPoint ||
						renderer->mainLights[index].lookAtObject ||
						renderer->mainLights[index].light.get() != owner.pointLights[index].get() ||
						!AttestPrivatePointValues(owner.pointLights[index].get(),
							owner.pointLightValues[index], pointVtable.address()))
						return false;
				}
				attested = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				attested = false;
			}
			return attested;
		}

		[[nodiscard]] bool SelectedPointTopologyEqual(
			const OwnerState& owner,
			const LightingSnapshot& lighting) noexcept
		{
			if (owner.pointLightCount != lighting.pointCount)
				return false;
			for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
				if (owner.sourcePointWrapperIdentities[index] != lighting.points[index].wrapperIdentity ||
					owner.sourcePointLightIdentities[index] != lighting.points[index].lightIdentity)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool SelectedPointValuesEqual(
			const OwnerState& owner,
			const LightingSnapshot& lighting) noexcept
		{
			if (owner.pointLightCount != lighting.pointCount)
				return false;
			for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
				const auto expected = ExportPointValues(lighting.points[index]);
				if (std::memcmp(std::addressof(owner.pointLightValues[index]),
						std::addressof(expected), sizeof(expected)) != 0)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool PrivateGraphDisjointFromSnapshot(
			const OwnerState& owner,
			const LightingSnapshot& lighting) noexcept
		{
			const auto privateDirectional = reinterpret_cast<std::uintptr_t>(owner.directionalLight.get());
			if (privateDirectional == 0 || privateDirectional == lighting.directional.lightIdentity)
				return false;
			for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
				const auto privatePoint = reinterpret_cast<std::uintptr_t>(owner.pointLights[index].get());
				if (privatePoint == 0 || privatePoint == lighting.directional.lightIdentity)
					return false;
				for (std::uint32_t prior = 0; prior < index; ++prior) {
					if (owner.pointLights[prior].get() == owner.pointLights[index].get())
						return false;
				}
			}
			for (std::uint32_t source = 0; source < lighting.sourceIdentityCount; ++source) {
				const auto wrapper = lighting.sourceWrapperIdentities[source];
				const auto light = lighting.sourceLightIdentities[source];
				if (privateDirectional == wrapper || privateDirectional == light)
					return false;
				for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
					const auto privatePoint = reinterpret_cast<std::uintptr_t>(owner.pointLights[index].get());
					if (privatePoint == wrapper || privatePoint == light)
						return false;
				}
			}
			return true;
		}

		void ClearOwnerPins(OwnerState& owner) noexcept
		{
			owner.modelRoot.reset();
			for (auto& light : owner.pointLights)
				light.reset();
			owner.pointLightValues = {};
			owner.sourcePointWrapperIdentities = {};
			owner.sourcePointLightIdentities = {};
			owner.pointLightCount = 0;
			owner.shadowSceneNode.reset();
			owner.accumulator = nullptr;

			owner.directionalLight.reset();
			owner.directionalLightValues = {};
			owner.device.Reset();
			owner.registryName = nullptr;
			owner.liveShadowSceneNode = nullptr;
			owner.loadGeneration = 0;
			owner.generationSerial = 0;
			owner.pointTopologySignature = 0;
			owner.renderThreadId = 0;
			owner.activeToken = 0;
			owner.poisoned = false;
			owner.releasePending = false;
		}

		[[nodiscard]] bool LegitimateThreadRollover(
			const OwnerState& owner,
			const AcquireRequest& request) noexcept
		{
			if (!owner.renderer || owner.activeToken != 0 || owner.renderThreadId == 0 ||
				owner.renderThreadId == request.renderThreadId)
				return false;

			return request.renderThreadId == GetCurrentThreadId();
		}

		void ReclaimAbandonedGenerationsForCurrentThreadLocked(OwnerState& owner) noexcept
		{
			const auto currentThread = GetCurrentThreadId();
			for (auto& abandoned : owner.abandonedGenerations) {
				if (!abandoned.renderer || !abandoned.reclaimableOnOwnerThread ||
					abandoned.renderThreadId != currentThread)
					continue;
				EngineCallResult call{};
				const auto generationSerial = abandoned.generationSerial;
				if (!ReleaseRendererLeaf(abandoned.renderer, abandoned.registryName, call)) {

					abandoned.reclaimableOnOwnerThread = false;
					try {
						logger::critical(
							"[FlatReflectionRendererOwner] deferred affine generation={} Release faulted on owner thread: code=0x{:08X}; retained without retry",
							generationSerial, call.exceptionCode);
					} catch (...) {
					}
					continue;
				}

				abandoned.renderer = nullptr;

				abandoned = {};
				if (owner.abandonedGenerationCount != 0)
					--owner.abandonedGenerationCount;
				try {
					logger::info(
						"[FlatReflectionRendererOwner] reclaimed deferred affine generation={} on owner thread={}",
						generationSerial, currentThread);
				} catch (...) {
				}
			}
		}

		[[nodiscard]] bool QuarantineOwnerGenerationLocked(
			OwnerState& owner, const char* reason,
			bool reclaimableOnOwnerThread) noexcept
		{
			if (!owner.renderer || owner.activeToken != 0 ||
				owner.abandonedGenerationCount >= owner.abandonedGenerations.size())
				return false;
			auto abandonedSlot = std::find_if(owner.abandonedGenerations.begin(),
				owner.abandonedGenerations.end(), [](const AbandonedGeneration& candidate) {
					return candidate.renderer == nullptr;
				});
			if (abandonedSlot == owner.abandonedGenerations.end())
				return false;
			auto& abandoned = *abandonedSlot;

			abandoned.renderer = std::exchange(owner.renderer, nullptr);
			abandoned.registryName = std::move(owner.registryName);
			abandoned.accumulator = std::exchange(owner.accumulator, nullptr);
			abandoned.modelRoot = std::move(owner.modelRoot);
			abandoned.shadowSceneNode = std::move(owner.shadowSceneNode);
			abandoned.directionalLight = std::move(owner.directionalLight);
			abandoned.directionalLightValues = std::exchange(owner.directionalLightValues, {});
			abandoned.pointLights = std::move(owner.pointLights);
			abandoned.pointLightValues = std::exchange(owner.pointLightValues, {});
			abandoned.sourcePointWrapperIdentities =
				std::exchange(owner.sourcePointWrapperIdentities, {});
			abandoned.sourcePointLightIdentities =
				std::exchange(owner.sourcePointLightIdentities, {});
			abandoned.pointLightCount = std::exchange(owner.pointLightCount, 0);
			abandoned.device = std::move(owner.device);
			abandoned.liveShadowSceneNode = std::exchange(owner.liveShadowSceneNode, nullptr);
			abandoned.loadGeneration = std::exchange(owner.loadGeneration, 0);
			abandoned.generationSerial = std::exchange(owner.generationSerial, 0);
			abandoned.pointTopologySignature = std::exchange(owner.pointTopologySignature, 0);
			abandoned.renderThreadId = std::exchange(owner.renderThreadId, 0);
			abandoned.reclaimableOnOwnerThread = reclaimableOnOwnerThread;
			owner.activeToken = 0;
			owner.poisoned = false;
			owner.releasePending = false;
			owner.permanentFailure = false;
			++owner.abandonedGenerationCount;
			try {
				if (abandoned.reclaimableOnOwnerThread) {
					logger::info(
						"[FlatReflectionRendererOwner] {} deferred affine generation={} ownerThread={} abandonedCount={}",
						reason ? reason : "render-thread rollover",
						abandoned.generationSerial, abandoned.renderThreadId,
						owner.abandonedGenerationCount);
				} else {
					logger::critical(
						"[FlatReflectionRendererOwner] {} pinned terminal generation={} ownerThread={} abandonedCount={}; no further Renderer::Release attempted",
						reason ? reason : "generation quarantine",
						abandoned.generationSerial, abandoned.renderThreadId,
						owner.abandonedGenerationCount);
				}
			} catch (...) {
			}
			return true;
		}

		void RecoverPermanentFailureIntoAvailableQuarantineLocked(OwnerState& owner) noexcept
		{
			if (!owner.permanentFailure || !owner.renderer || owner.activeToken != 0 ||
				owner.abandonedGenerationCount >= owner.abandonedGenerations.size())
				return;

			owner.permanentFailure = false;
			if (!QuarantineOwnerGenerationLocked(
					owner, "deferred terminal quarantine recovery", false))
				owner.permanentFailure = true;
		}

		[[nodiscard]] bool ReleaseOwnerLocked(OwnerState& owner) noexcept
		{
			if (!owner.renderer) {
				ClearOwnerPins(owner);
				return true;
			}
			EngineCallResult call{};
			auto* renderer = owner.renderer;
			const bool released = ReleaseRendererLeaf(renderer, owner.registryName, call);
			if (!released) {
				try {
					logger::critical(
						"[FlatReflectionRendererOwner] private Renderer::Release faulted: code=0x{:08X}; generation quarantined",
						call.exceptionCode);
				} catch (...) {
				}
				owner.activeToken = 0;

				if (QuarantineOwnerGenerationLocked(owner, "faulted release", false))
					return true;
				owner.permanentFailure = true;
				owner.poisoned = true;
				owner.releasePending = true;
				return false;
			}
			owner.renderer = nullptr;
			ClearOwnerPins(owner);
			return true;
		}

		[[nodiscard]] Status CreateGenerationLocked(
			const AcquireRequest& request,
			const LightingSnapshot& lighting,
			OwnerState& owner) noexcept
		{
			try {
				const std::uint64_t serial = g_nextGeneration.fetch_add(1, std::memory_order_relaxed);
				char nameBuffer[64]{};
				std::snprintf(nameBuffer, sizeof(nameBuffer), "DynRefFlatCapture_%016llX",
					static_cast<unsigned long long>(serial));
				RE::BSFixedString name{ nameBuffer };

				RendererCreateResult create{};
				CreateRendererLeaf(name, create);
				if (!create.completed) {
					if (create.renderer) {
						owner.renderer = create.renderer;
						owner.registryName = name;
						owner.renderThreadId = request.renderThreadId;
						owner.poisoned = true;
						owner.releasePending = true;
						if (!ReleaseOwnerLocked(owner))
							return Status::kReleaseFailed;
					}
					return create.registryCollision ? Status::kRegistryCollision : Status::kCreateFailed;
				}

				owner.renderer = create.renderer;
				owner.registryName = name;
				owner.device = request.device;
				owner.liveShadowSceneNode = request.liveShadowSceneNode;
				owner.loadGeneration = request.loadGeneration;
				owner.generationSerial = serial;
				owner.pointTopologySignature = lighting.pointTopologySignature;
				owner.renderThreadId = request.renderThreadId;

				EngineCallResult lightCall{};
				if (!ConfigurePrivateLightsLeaf(owner.renderer, lighting, lightCall)) {
					if (!ReleaseOwnerLocked(owner))
						return Status::kReleaseFailed;
					return Status::kPrivateLightCreationFailed;
				}

				if (!AttestAndPinGeneration(owner.renderer, lighting, owner)) {
					if (!ReleaseOwnerLocked(owner))
						return Status::kReleaseFailed;
					return Status::kPrivateRendererAttestationFailed;
				}

				ModelRootCreateResult modelRootCreate{};
				CreateDetachedModelRootLeaf(modelRootCreate);
				if (modelRootCreate.modelRoot)
					owner.modelRoot.reset(modelRootCreate.modelRoot);
				if (!modelRootCreate.completed || !AttestDetachedEmptyModelRoot(owner)) {
					if (!ReleaseOwnerLocked(owner))
						return Status::kReleaseFailed;
					return Status::kPrivateRendererAttestationFailed;
				}
				for (std::uint32_t index = 0; index < lighting.pointCount; ++index) {
					owner.sourcePointWrapperIdentities[index] = lighting.points[index].wrapperIdentity;
					owner.sourcePointLightIdentities[index] = lighting.points[index].lightIdentity;
				}

				EngineCallResult directionalCall{};
				if (!ApplyDirectionalSnapshotLeaf(owner.directionalLight.get(), lighting.directional, directionalCall)) {
					if (!ReleaseOwnerLocked(owner))
						return Status::kReleaseFailed;
					return Status::kPrivateLightCreationFailed;
				}
				owner.directionalLightValues = ExportDirectionalValues(lighting.directional);
				if (!AttestAppliedDirectional(owner.directionalLight.get(), owner.directionalLightValues)) {
					if (!ReleaseOwnerLocked(owner))
						return Status::kReleaseFailed;
					return Status::kPrivateRendererAttestationFailed;
				}
				logger::info(
					"[FlatReflectionRendererOwner] private renderer ready: generation={} pointLights={} sunContributes={} thread={}",
					owner.generationSerial, owner.pointLightCount, lighting.directional.contributes,
					owner.renderThreadId);
				return Status::kSuccess;
			} catch (...) {
				owner.poisoned = owner.renderer != nullptr;
				owner.releasePending = owner.renderer != nullptr;
				if (owner.renderer && !ReleaseOwnerLocked(owner))
					return Status::kReleaseFailed;
				return Status::kCreateFailed;
			}
		}

		enum class NativeGraphPhase : std::uint32_t
		{
			kAcquiredIdle,
			kPublishedIdle,
			kPrepared,
			kPostSunDetach
		};

		struct NativeGraphEvidence
		{
			std::array<std::uintptr_t, Lease::kMaximumPointLights> pointWrappers{};
			std::uint32_t pointWrapperCount{};
			std::uintptr_t shadowSunWrapper{};
		};

		[[nodiscard]] bool FailNativeWorld(std::uint32_t* failureCode, std::uint32_t code) noexcept
		{
			if (failureCode)
				*failureCode = code;
			return false;
		}

		[[nodiscard]] bool LoadPrivateArray(
			std::uintptr_t base, std::size_t dataOffset, std::size_t capacityOffset,
			std::size_t countOffset, void*& data, std::uint32_t& capacity,
			std::uint32_t& count) noexcept
		{
			return SafeRead(base + dataOffset, data) && SafeRead(base + capacityOffset, capacity) &&
			       SafeRead(base + countOffset, count) && count <= capacity && capacity <= 64u &&
			       (count == 0 || PointerPlausible(data));
		}

		[[nodiscard]] bool ActiveShadowSceneNodeMatchesPhase(
			const void* privateSSN, NativeGraphPhase phase) noexcept
		{
			std::array<void*, 5> active{};
			if (!SafeRead(REL::Module::get().base() + kRVA_ActiveShadowSceneNode, active))
				return false;
			const auto aliasesPrivate = [privateSSN](const void* value) {
				return value == privateSSN;
			};
			if (phase == NativeGraphPhase::kPublishedIdle ||
				phase == NativeGraphPhase::kPrepared) {

				return active[4] == privateSSN &&
				       std::none_of(active.begin(), active.begin() + 4, aliasesPrivate);
			}
			return std::none_of(active.begin(), active.end(), aliasesPrivate);
		}

		[[nodiscard]] bool PassPoolsIdle() noexcept
		{
			auto read = [](long& passes, long& lights, long& mode) noexcept {
				passes = lights = mode = -1;
				const auto base = REL::Module::get().base();
				void* passPool = nullptr;
				void* lightPool = nullptr;
				if (!SafeRead(base + ReflectionRuntime::Rva(kRVA_RenderPassPoolPointer), passPool) ||
					!SafeRead(base + ReflectionRuntime::Rva(kRVA_RenderLightPoolPointer), lightPool) ||
					!PointerPlausible(passPool) || !PointerPlausible(lightPool))
					return false;
				__try {
					passes = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(
						reinterpret_cast<std::byte*>(passPool) + kPassPoolBatchCountOffset), 0, 0);
					lights = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(
						reinterpret_cast<std::byte*>(lightPool) + kPassPoolBatchCountOffset), 0, 0);
					mode = _InterlockedCompareExchange(
						reinterpret_cast<volatile long*>(base + ReflectionRuntime::Rva(kRVA_BSMTAMode)), 0, 0);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					return false;
				}
				return passes == 0 && lights == 0 && mode == 0;
			};
			long passes = -1, lights = -1, mode = -1;
			return read(passes, lights, mode) && read(passes, lights, mode);
		}

		[[nodiscard]] bool AccumulatorPassListsIdle(void* accumulator) noexcept
		{
			if (!PointerPlausible(accumulator))
				return false;
			const auto batch = reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorBatchOffset;
			for (std::uint32_t index = 0; index < kAccumulatorGroupArrayCount; ++index) {
				std::uint32_t count = ~0u;
				if (!SafeRead(batch + kBatchGroupArrayOffset +
						static_cast<std::size_t>(index) * kBatchGroupArrayStride + 0x10, count) ||
					count != 0)
					return false;
			}
			std::uint32_t sortedCount = ~0u;
			void* currentRecord = nullptr;
			if (!SafeRead(batch + kBatchSortedCountOffset, sortedCount) || sortedCount != 0 ||
				!SafeRead(batch + kBatchSortedCurrentRecordOffset, currentRecord))
				return false;
			if (currentRecord) {
				void* head = nullptr;
				void* tail = nullptr;
				std::uint32_t count = ~0u;
				const auto record = reinterpret_cast<std::uintptr_t>(currentRecord);
				if (!PointerPlausible(currentRecord) ||
					!SafeRead(record + kPassRecordHeadOffset, head) ||
					!SafeRead(record + kPassRecordTailOffset, tail) ||
					!SafeRead(record + kPassRecordCountOffset, count) || head || tail || count != 0)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool SubmittedRootsMatch(
			const NativeWorldCallbackContext& context, RE::NiAVObject* const* roots,
			std::uint32_t rootCount, const OwnerState& owner,
			const RE::NiAVObject* captureCamera) noexcept
		{
			if (!roots || !context.submittedRoots || rootCount == 0 ||
				rootCount != context.submittedRootCount || rootCount > 8192u)
				return false;
			for (std::uint32_t index = 0; index < rootCount; ++index) {
				auto* root = roots[index];
				if (root != context.submittedRoots[index] || !PointerPlausible(root) ||
					root == owner.modelRoot.get() ||
					root == owner.shadowSceneNode.get() ||
					root == captureCamera || root == owner.directionalLight.get())
					return false;
				for (std::uint32_t light = 0; light < owner.pointLightCount; ++light) {
					if (root == owner.pointLights[light].get())
						return false;
				}
				for (std::uint32_t prior = 0; prior < index; ++prior) {
					if (root == roots[prior])
						return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool OwnerAccumulatorIdle(
			const OwnerState& owner, std::uint32_t* failureDetail = nullptr) noexcept
		{
			if (failureDetail)
				*failureDetail = 0u;
			const auto fail = [failureDetail](std::uint32_t detail) noexcept {
				if (failureDetail)
					*failureDetail = detail;
				return false;
			};
			if (!PointerPlausible(owner.accumulator))
				return fail(1u);
			void* camera = reinterpret_cast<void*>(1);
			void* current = nullptr;
			const auto base = REL::Module::get().base();
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(owner.accumulator) +
					kAccumulatorCameraOffset, camera))
				return fail(2u);
			if (camera != nullptr)
				return fail(3u);
			if (!SafeRead(base + ReflectionRuntime::Rva(kRVA_CurrentAccumulator), current))
				return fail(4u);
			if (current == owner.accumulator)
				return fail(5u);
			if (!AccumulatorPassListsIdle(owner.accumulator))
				return fail(6u);
			return true;
		}

		[[nodiscard]] bool PrivateIdentityAliasesSource(
			const OwnerState& owner, const void* value) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(value);
			for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
				if (owner.sourcePointWrapperIdentities[index] == identity ||
					owner.sourcePointLightIdentities[index] == identity)
					return true;
			}
			return false;
		}

		[[nodiscard]] bool AttestPrivateLightGraph(
			const OwnerState& owner, NativeGraphPhase phase, NativeGraphEvidence* evidence,
			std::uint32_t* failureDetail = nullptr) noexcept
		{
			if (failureDetail)
				*failureDetail = 0;
			const auto fail = [failureDetail](std::uint32_t detail) noexcept {
				if (failureDetail)
					*failureDetail = detail;
				return false;
			};

			if (!owner.renderer)
				return fail(1u);
			if (!owner.shadowSceneNode)
				return fail(2u);
			if (owner.pointLightCount > Lease::kMaximumPointLights)
				return fail(3u);
			if (!ActiveShadowSceneNodeMatchesPhase(owner.shadowSceneNode.get(), phase))
				return fail(4u);
			const auto ssn = reinterpret_cast<std::uintptr_t>(owner.shadowSceneNode.get());
			std::uintptr_t ssnVtable = 0;
			void* ssnParent = reinterpret_cast<void*>(1);
			std::uint64_t ssnFlags = 0;
			bool needsSetup = false;
			REL::Relocation<std::uintptr_t> expectedSSNVtable{ RE::VTABLE::ShadowSceneNode[0] };
			if (!SafeRead(ssn, ssnVtable))
				return fail(5u);
			if (ssnVtable != expectedSSNVtable.address())
				return fail(6u);
			if (!SafeRead(ssn + 0x28, ssnParent))
				return fail(7u);
			if (ssnParent != nullptr)
				return fail(8u);
			if (!SafeRead(ssn + kNiAVFlagsOffset, ssnFlags))
				return fail(9u);
			if ((ssnFlags & (kNiAVAppCulledBit | kNiAVFlattenedFadeRouteBit)) != 0)
				return fail(10u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(owner.renderer) +
					kRendererNeedsLightSetupOffset, needsSetup))
				return fail(11u);

			void* childData = nullptr;
			std::uint16_t childCapacity = 0;
			std::uint16_t childCount = 0;
			if (!SafeRead(ssn + kSSNChildrenDataOffset, childData))
				return fail(12u);
			if (!SafeRead(ssn + kSSNChildrenCapacityOffset, childCapacity))
				return fail(13u);
			if (!SafeRead(ssn + kSSNChildrenCountOffset, childCount))
				return fail(14u);
			if (childCount > childCapacity)
				return fail(15u);
			if (childCapacity > 64u)
				return fail(16u);
			if (childCapacity != 0 && !PointerPlausible(childData))
				return fail(17u);

			void* activeData = nullptr;
			void* scratchData = nullptr;
			std::uint32_t activeCapacity = 0, activeCount = 0;
			std::uint32_t scratchCapacity = 0, scratchCount = 0;
			if (!LoadPrivateArray(ssn, kSSNOrdinaryLightArrayOffset,
					kSSNOrdinaryLightCapacityOffset, kSSNOrdinaryLightCountOffset,
					activeData, activeCapacity, activeCount))
				return fail(18u);
			if (!LoadPrivateArray(ssn, kSSNShadowLightArrayOffset,
					kSSNShadowLightCapacityOffset, kSSNShadowLightCountOffset,
					scratchData, scratchCapacity, scratchCount))
				return fail(19u);
			if (scratchCount != 0)
				return fail(20u);
			if (!LoadPrivateArray(ssn, kSSNPortalLightArrayOffset,
					kSSNPortalLightCapacityOffset, kSSNPortalLightCountOffset,
					scratchData, scratchCapacity, scratchCount))
				return fail(21u);
			if (scratchCount != 0)
				return fail(22u);
			if (!LoadPrivateArray(ssn, kSSNQueuedAddArrayOffset,
					kSSNQueuedAddCapacityOffset, kSSNQueuedAddCountOffset,
					scratchData, scratchCapacity, scratchCount))
				return fail(23u);
			if (scratchCount != 0)
				return fail(24u);
			if (!LoadPrivateArray(ssn, kSSNQueuedRemoveArrayOffset,
					kSSNQueuedRemoveCapacityOffset, kSSNQueuedRemoveCountOffset,
					scratchData, scratchCapacity, scratchCount))
				return fail(25u);
			if (scratchCount != 0)
				return fail(26u);
			std::uint64_t queueLock = ~0ull;
			if (!SafeRead(ssn + kSSNLightLockOffset, queueLock))
				return fail(27u);
			if (queueLock != 0)
				return fail(28u);

			const bool materialized = !needsSetup;

			if ((phase == NativeGraphPhase::kPrepared ||
					phase == NativeGraphPhase::kPostSunDetach) &&
				!materialized)
				return fail(29u);
			const auto expectedCount = materialized ? owner.pointLightCount : 0u;
			if (childCount != expectedCount)
				return fail(30u);
			if (activeCount != expectedCount)
				return fail(31u);

			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
			REL::Relocation<std::uintptr_t> baseLightVtable{ RE::VTABLE::BSLight[0] };
			std::array<bool, Lease::kMaximumPointLights> childMatched{};
			std::uint32_t nonNullChildCount = 0;
			for (std::uint16_t slot = 0; slot < childCapacity; ++slot) {
				void* child = nullptr;
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(childData) +
						static_cast<std::size_t>(slot) * sizeof(void*), child))
					return fail(32u);
				if (!child)
					continue;
				++nonNullChildCount;
				bool matched = false;
				for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
					if (!childMatched[index] && child == owner.pointLights[index].get()) {
						childMatched[index] = true;
						matched = true;
						break;
					}
				}
				if (!matched)
					return fail(33u);
			}
			if (nonNullChildCount != childCount)
				return fail(34u);

			NativeGraphEvidence observed{};
			observed.pointWrapperCount = activeCount;
			std::array<bool, Lease::kMaximumPointLights> wrapperMatched{};
			for (std::uint32_t index = 0; index < activeCount; ++index) {
				void* wrapper = nullptr;
				std::uintptr_t wrapperVtable = 0;
				void* light = nullptr;
				std::uint32_t shape = 0;
				std::uint8_t portal = 0;
				float lodDimmer = 0.0f;
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(activeData) +
						static_cast<std::size_t>(index) * sizeof(void*), wrapper))
					return fail(35u);
				if (!PointerPlausible(wrapper))
					return fail(36u);
				if (PrivateIdentityAliasesSource(owner, wrapper))
					return fail(37u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(wrapper), wrapperVtable))
					return fail(38u);
				if (wrapperVtable != baseLightVtable.address())
					return fail(39u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(wrapper) + kBSLightNiLightOffset, light))
					return fail(40u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(wrapper) + kBSLightShapeOffset, shape))
					return fail(41u);
				if (shape != kOrdinaryPointShape)
					return fail(42u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(wrapper) + kBSLightPortalByteOffset, portal))
					return fail(43u);
				if (portal != 0)
					return fail(44u);

				if (!SafeRead(reinterpret_cast<std::uintptr_t>(wrapper) + kBSLightLodDimmerOffset, lodDimmer))
					return fail(47u);
				if (!std::isfinite(lodDimmer) || lodDimmer < 0.0f ||
					lodDimmer > kMaximumFiniteLightValue)
					return fail(48u);
				bool matched = false;
				for (std::uint32_t lightIndex = 0; lightIndex < owner.pointLightCount; ++lightIndex) {
					if (!wrapperMatched[lightIndex] && light == owner.pointLights[lightIndex].get()) {
						wrapperMatched[lightIndex] = true;
						matched = true;
						break;
					}
				}
				if (!matched)
					return fail(49u);
				observed.pointWrappers[index] = reinterpret_cast<std::uintptr_t>(wrapper);
			}

			for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
				auto* point = owner.pointLights[index].get();
				void* parent = nullptr;
				std::uintptr_t vtable = 0;
				std::uint64_t flags = 0;
				std::array<float, 4> bound{};
				if (!point)
					return fail(50u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(point), vtable))
					return fail(51u);
				if (vtable != pointVtable.address())
					return fail(52u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(point) + 0x28, parent))
					return fail(53u);
				if (parent != (materialized ? owner.shadowSceneNode.get() : nullptr))
					return fail(54u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(point) + kNiAVFlagsOffset, flags))
					return fail(55u);
				if ((flags & kNiAVAppCulledBit) != 0)
					return fail(56u);
				if (!SafeRead(reinterpret_cast<std::uintptr_t>(point) + 0xB0, bound))
					return fail(57u);
				if (!std::all_of(bound.begin(), bound.end(),
						[](float value) { return std::isfinite(value); }))
					return fail(58u);
				if (bound[3] < 0.0f || bound[3] > kMaximumPointRadius)
					return fail(59u);
				if (!AttestPrivatePointValues(
						point, owner.pointLightValues[index], pointVtable.address()))
					return fail(60u);
			}

			void* primaryWrapper = nullptr;
			void* shadowWrapper = nullptr;
			std::uintptr_t primaryVtable = 0, shadowVtable = 0;
			void* primarySun = nullptr;
			void* shadowSun = nullptr;
			REL::Relocation<std::uintptr_t> shadowDirectionalVtable{ RE::VTABLE::BSShadowDirectionalLight[0] };
			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			std::uintptr_t primarySunVtable = 0, shadowSunVtable = 0;
			if (!SafeRead(ssn + kSSNBaseSunOffset, primaryWrapper))
				return fail(61u);
			if (!SafeRead(ssn + kSSNShadowSunOffset, shadowWrapper))
				return fail(62u);
			if (!PointerPlausible(primaryWrapper))
				return fail(63u);
			if (!PointerPlausible(shadowWrapper))
				return fail(64u);
			if (primaryWrapper == shadowWrapper)
				return fail(65u);
			if (PrivateIdentityAliasesSource(owner, primaryWrapper))
				return fail(66u);
			if (PrivateIdentityAliasesSource(owner, shadowWrapper))
				return fail(67u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(primaryWrapper), primaryVtable))
				return fail(68u);
			if (primaryVtable != baseLightVtable.address())
				return fail(69u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(shadowWrapper), shadowVtable))
				return fail(70u);
			if (shadowVtable != shadowDirectionalVtable.address())
				return fail(71u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(primaryWrapper) +
					kBSLightNiLightOffset, primarySun))
				return fail(72u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(shadowWrapper) +
					kBSLightNiLightOffset, shadowSun))
				return fail(73u);
			if (!PointerPlausible(primarySun))
				return fail(74u);
			if (!PointerPlausible(shadowSun))
				return fail(75u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(primarySun), primarySunVtable))
				return fail(76u);
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(shadowSun), shadowSunVtable))
				return fail(77u);
			if (primarySunVtable != directionalVtable.address())
				return fail(78u);
			if (shadowSunVtable != directionalVtable.address())
				return fail(79u);

			auto* privateDirectional = owner.directionalLight.get();
			if (phase == NativeGraphPhase::kPrepared) {
				if (primarySun != privateDirectional)
					return fail(80u);
				if (shadowSun != privateDirectional)
					return fail(81u);
			} else {
				if (primarySun == privateDirectional)
					return fail(82u);
				if (materialized && shadowSun != privateDirectional)
					return fail(83u);
			}
			for (std::uint32_t index = 0; index < owner.pointLightCount; ++index) {
				if (primarySun == owner.pointLights[index].get())
					return fail(84u);
				if (shadowSun == owner.pointLights[index].get())
					return fail(85u);
			}
			observed.shadowSunWrapper = reinterpret_cast<std::uintptr_t>(shadowWrapper);

			if (evidence)
				*evidence = observed;
			return true;
		}

		[[nodiscard]] bool LeaseGraphIdentityMatchesOwner(
			const Lease& lease, const OwnerState& owner) noexcept
		{
			if (lease.renderer != owner.renderer || lease.accumulator != owner.accumulator ||
				lease.modelRoot.get() != owner.modelRoot.get() ||
				lease.shadowSceneNode.get() != owner.shadowSceneNode.get() ||
				lease.directionalLight.get() != owner.directionalLight.get() ||
				lease.pointLightCount != owner.pointLightCount || !lease.compositeFlag ||
				lease.generationSerial == 0 || lease.generationSerial != owner.generationSerial ||
				std::memcmp(std::addressof(lease.directionalLightValues),
					std::addressof(owner.directionalLightValues),
					sizeof(lease.directionalLightValues)) != 0)
				return false;
			for (std::size_t index = 0; index < lease.pointLights.size(); ++index) {
				if (lease.pointLights[index].get() != owner.pointLights[index].get() ||
					std::memcmp(std::addressof(lease.pointLightValues[index]),
						std::addressof(owner.pointLightValues[index]),
						sizeof(lease.pointLightValues[index])) != 0)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool AttestActiveLeaseLocked(
			const NativeWorldCallbackContext& context, std::uint64_t ownerToken,
			const RE::NiAVObject* viewCamera, void* viewAccumulator,
			const RE::NiAVObject* viewSSN, std::uint64_t ownerSerial,
			std::uint64_t preparedSerial, std::uint64_t frameSerial,
			std::uint32_t* failureDetail = nullptr) noexcept
		{
			if (failureDetail)
				*failureDetail = 0u;
			const auto fail = [failureDetail](std::uint32_t detail) noexcept {
				if (failureDetail)
					*failureDetail = detail;
				return false;
			};
			const auto* lease = context.lease;
			if (!lease)
				return fail(1u);
			if (ownerToken == 0 || g_owner.activeToken != ownerToken)
				return fail(2u);
			if (!g_owner.renderer || g_owner.poisoned || g_owner.releasePending ||
				g_owner.permanentFailure)
				return fail(3u);
			if (g_owner.renderThreadId == 0 || GetCurrentThreadId() != g_owner.renderThreadId)
				return fail(4u);
			if (!LeaseGraphIdentityMatchesOwner(*lease, g_owner))
				return fail(5u);
			if (ownerSerial != g_owner.generationSerial ||
				viewSSN != g_owner.shadowSceneNode.get())
				return fail(6u);
			if (context.exclusiveAccumulator != g_owner.accumulator ||
				viewAccumulator != g_owner.accumulator)
				return fail(7u);
			if (viewCamera != context.captureCamera)
				return fail(8u);
			if (preparedSerial == 0 || preparedSerial != context.preparedAccumulatorSerial)
				return fail(9u);
			if (frameSerial == 0 || frameSerial != context.frameSerial)
				return fail(10u);
			if (!DeviceHealthy(g_owner.device.Get()))
				return fail(11u);
			if (!AttestExistingGeneration(g_owner))
				return fail(12u);
			std::uint32_t accumulatorFailureDetail = 0u;
			if (!OwnerAccumulatorIdle(g_owner, std::addressof(accumulatorFailureDetail)))
				return fail(20u + accumulatorFailureDetail);
			return true;
		}

		struct NativeRendererPrefixCallResult
		{
			std::uint32_t exceptionCode{};
			std::uint32_t completedStage{};
			bool completed{};
		};

		void PublishNativeWorldRendererPrefixLeaf(
			OwnerState& owner, NativeRendererPrefixCallResult& call) noexcept
		{
			call = {};
			__try {
				auto* renderer = owner.renderer;
				if (!renderer || !std::isfinite(renderer->opacityAlpha) ||
					!std::isfinite(renderer->menuDiffuseIntensity) ||
					!std::isfinite(renderer->menuEmitIntensity) ||
					(std::abs(renderer->menuDiffuseIntensity) +
						std::abs(renderer->menuEmitIntensity)) <= 1.0e-6f)
					return;

				const auto base = REL::Module::get().base();
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_InterfaceDisplayGeometry)) = 0.0f;
				call.completedStage = 1u;
				*reinterpret_cast<std::uint32_t*>(base + ReflectionRuntime::Rva(kRVA_InterfaceGlobal1D34)) =
					*reinterpret_cast<const std::uint32_t*>(
						base + ReflectionRuntime::Rva(kRVA_InterfaceGlobal1D34Source));
				call.completedStage = 2u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_InterfacePostAA)) =
					renderer->postAA ? 1.0f : 0.0f;
				call.completedStage = 3u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_InterfaceOpacityAlpha)) =
					renderer->opacityAlpha;
				call.completedStage = 4u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_InterfaceMenuDiffuseIntensity)) =
					renderer->menuDiffuseIntensity;
				call.completedStage = 5u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_InterfaceMenuEmitIntensity)) =
					renderer->menuEmitIntensity;
				call.completedStage = 6u;

				call.completedStage = 7u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_Global1CDC)) =
					renderer->menuBlend.get() !=
						RE::Interface3D::OffscreenMenuBlendMode::kAdditive ?
					1.0f : 0.0f;
				call.completedStage = 8u;
				*reinterpret_cast<std::uint8_t*>(base + ReflectionRuntime::Rva(kRVA_Global1BC8)) =
					renderer->usePremultAlpha ? 1u : 0u;
				call.completedStage = 9u;
				*reinterpret_cast<float*>(base + ReflectionRuntime::Rva(kRVA_Global1BCC)) = renderer->opacityAlpha;
				call.completedStage = 10u;
				*reinterpret_cast<std::uint8_t*>(base + ReflectionRuntime::Rva(kRVA_Global1C19)) = 4u;
				call.completedStage = 11u;
				if (renderer->useFullPremultAlpha)
					*reinterpret_cast<std::uint8_t*>(base + ReflectionRuntime::Rva(kRVA_Global1F55)) = 1u;
				call.completedStage = 12u;
				call.completed = true;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				call.completed = false;
			}
		}

		[[nodiscard]] bool OwnerGenerationSafelyIdleLocked(const OwnerState& owner) noexcept
		{

			return AttestExistingGeneration(owner) && OwnerAccumulatorIdle(owner) &&
			       PassPoolsIdle() &&
			       AttestPrivateLightGraph(owner, NativeGraphPhase::kAcquiredIdle, nullptr);
		}

	}

	Lease::Lease(Lease&& other) noexcept
	{
		MoveFrom(other);
	}

	Lease& Lease::operator=(Lease&& other) noexcept
	{
		if (this != std::addressof(other)) {
			Finish(FinishDisposition::kQuarantine);
			MoveFrom(other);
		}
		return *this;
	}

	void Lease::MoveFrom(Lease& other) noexcept
	{
		renderer = std::exchange(other.renderer, nullptr);
		accumulator = std::exchange(other.accumulator, nullptr);
		modelRoot = std::move(other.modelRoot);
		shadowSceneNode = std::move(other.shadowSceneNode);
		directionalLight = std::move(other.directionalLight);
		directionalLightValues = std::exchange(other.directionalLightValues, {});
		pointLights = std::move(other.pointLights);
		pointLightValues = std::exchange(other.pointLightValues, {});
		pointLightCount = std::exchange(other.pointLightCount, 0);
		compositeFlag = std::exchange(other.compositeFlag, false);
		generationSerial = std::exchange(other.generationSerial, 0);
		ownerToken = std::exchange(other.ownerToken, 0);
		nativeLightingCallbackEntered =
			std::exchange(other.nativeLightingCallbackEntered, false);
		nativeLightingCallbackSucceeded =
			std::exchange(other.nativeLightingCallbackSucceeded, false);
		nativePostCleanupAttested =
			std::exchange(other.nativePostCleanupAttested, false);
		active = std::exchange(other.active, false);
	}

	Lease::~Lease() noexcept
	{
		Finish(FinishDisposition::kQuarantine);
	}

	void Lease::Finish(FinishDisposition disposition) noexcept
	{
		FinishLease(*this, disposition);
	}

	bool PrepareNativeWorldLightingCallback(
		void* opaqueContext,
		const FlatDeferredPlayerCapture::NativeWorldLightingPreparationView& view,
		std::uint32_t* failureCode) noexcept
	{
		if (failureCode)
			*failureCode = 0;
		auto* context = static_cast<NativeWorldCallbackContext*>(opaqueContext);
		if (!context || !context->lease)
			return FailNativeWorld(failureCode, 101u);
		auto& lease = *context->lease;
		OwnerExclusiveLock ownerLock{ g_owner.lock };
		if (!lease.active || context->lightingPreparationAttempted)
			return FailNativeWorld(failureCode, 102u);
		context->lightingPreparationAttempted = true;
		lease.nativeLightingCallbackEntered = true;
		if (!AttestActiveLeaseLocked(*context, lease.ownerToken,
				reinterpret_cast<RE::NiAVObject*>(view.captureCamera), view.exclusiveAccumulator,
				view.privateShadowSceneNode, view.ownerIdentitySerial,
				view.preparedAccumulatorSerial, view.frameSerial))
			return FailNativeWorld(failureCode, 104u);
		if (!context->sealedSubmittedRoots ||
			context->sealedSubmittedRootCount != context->submittedRootCount)
			return FailNativeWorld(failureCode, 105u);
		if (!SubmittedRootsMatch(*context, context->sealedSubmittedRoots,
				context->sealedSubmittedRootCount, g_owner,
				reinterpret_cast<RE::NiAVObject*>(view.captureCamera)))
			return FailNativeWorld(failureCode, 107u);
		if (!PassPoolsIdle())
			return FailNativeWorld(failureCode, 108u);
		std::uint32_t graphFailureDetail = 0;
		if (!AttestPrivateLightGraph(
				g_owner, NativeGraphPhase::kPublishedIdle, nullptr,
				std::addressof(graphFailureDetail)))
			return FailNativeWorld(
				failureCode, graphFailureDetail != 0 ? 12000u + graphFailureDetail : 120u);

		NativeRendererPrefixCallResult prefixCall{};
		PublishNativeWorldRendererPrefixLeaf(g_owner, prefixCall);
		if (!prefixCall.completed)
			return FailNativeWorld(failureCode, 1100u + prefixCall.completedStage);
		context->rendererPrefixPublished = true;

		context->lightingPreparationCompleted = true;
		lease.nativeLightingCallbackSucceeded = true;
		return true;
	}

	bool AttestNativeWorldOwnerCallback(
		void* opaqueContext,
		const FlatDeferredPlayerCapture::NativeWorldOwnerAttestationView& view,
		std::uint32_t* failureCode) noexcept
	{
		if (failureCode)
			*failureCode = 0;
		auto* context = static_cast<NativeWorldCallbackContext*>(opaqueContext);
		if (!context || !context->lease)
			return FailNativeWorld(failureCode, 201u);
		auto& lease = *context->lease;
		OwnerExclusiveLock ownerLock{ g_owner.lock };
		std::uint32_t activeLeaseFailureDetail = 0u;
		if (!lease.active) {
			activeLeaseFailureDetail = 1u;
		} else {
			(void)AttestActiveLeaseLocked(*context, lease.ownerToken,
				reinterpret_cast<RE::NiAVObject*>(view.captureCamera), view.exclusiveAccumulator,
				view.privateShadowSceneNode, view.ownerIdentitySerial,
				view.preparedAccumulatorSerial, view.frameSerial,
				std::addressof(activeLeaseFailureDetail));
		}
		if (activeLeaseFailureDetail != 0u)
			return FailNativeWorld(failureCode, 20200u + activeLeaseFailureDetail);
		if (!SubmittedRootsMatch(*context, view.submittedRoots, view.submittedRootCount,
				g_owner, reinterpret_cast<RE::NiAVObject*>(view.captureCamera)) || !PassPoolsIdle())
			return FailNativeWorld(failureCode, 203u);

		using Phase = FlatDeferredPlayerCapture::NativeWorldOwnerAttestationPhase;
		if (view.phase == Phase::kSealedPreparedEntry) {
			if (context->sealedSubmittedRoots || context->sealedSubmittedRootCount != 0 ||
				context->lightingPreparationAttempted || context->rendererPrefixPublished ||
				context->lightingPreparationCompleted ||
				lease.nativeLightingCallbackEntered || lease.nativeLightingCallbackSucceeded ||
				lease.nativePostCleanupAttested ||
				view.nativeMutationBegan || view.nativeCallReturned || view.lightingPreparationReturned ||
				view.privateSunDetached || view.accumulatorActivePassesCleared ||
				view.sortedMode18CurrentRecordRepaired ||
				view.declaredResourcesRestored || view.engineStateRestored || view.pipelineStateRestored ||
				!AttestPrivateLightGraph(g_owner, NativeGraphPhase::kAcquiredIdle, nullptr))
				return FailNativeWorld(failureCode, 204u);
			context->sealedSubmittedRoots = view.submittedRoots;
			context->sealedSubmittedRootCount = view.submittedRootCount;
			return true;
		}
		if (view.phase != Phase::kRestoredPostCleanup)
			return FailNativeWorld(failureCode, 205u);

		if (view.submittedRoots != context->sealedSubmittedRoots ||
			view.submittedRootCount != context->sealedSubmittedRootCount)
			return FailNativeWorld(failureCode, 20601u);
		if (!context->lightingPreparationAttempted || !context->rendererPrefixPublished ||
			!context->lightingPreparationCompleted)
			return FailNativeWorld(failureCode, 20602u);
		if (!lease.nativeLightingCallbackEntered || !lease.nativeLightingCallbackSucceeded ||
			lease.nativePostCleanupAttested)
			return FailNativeWorld(failureCode, 20603u);
		if (!view.nativeMutationBegan || !view.nativeCallReturned ||
			!view.lightingPreparationReturned)
			return FailNativeWorld(failureCode, 20604u);
		if (!view.privateSunDetached || !view.accumulatorActivePassesCleared ||
			!view.sortedMode18CurrentRecordRepaired)
			return FailNativeWorld(failureCode, 20605u);
		if (!view.nativeWorldRasterizerPolicyAttested)
			return FailNativeWorld(failureCode, 20606u);
		if (!view.contextConstantGroupsRestored)
			return FailNativeWorld(failureCode, 20607u);
		if (!view.declaredResourcesRestored || !view.engineStateRestored ||
			!view.pipelineStateRestored)
			return FailNativeWorld(failureCode, 20608u);
		if (!AccumulatorPassListsIdle(view.exclusiveAccumulator) ||
			!AttestPrivateLightGraph(g_owner, NativeGraphPhase::kPostSunDetach, nullptr))
			return FailNativeWorld(failureCode, 207u);
		lease.nativePostCleanupAttested = true;
		return true;
	}

	PointLightSnapshotStatus SnapshotPointLights(
		const PointLightSnapshotRequest& request,
		PointLightValuesSnapshot& snapshot) noexcept
	{
		snapshot = {};
		try {
			if (REL::Module::IsVR() || (REL::Module::get().version() != REL::Version{ 1, 10, 163, 0 } && !ReflectionRuntime::IsPort240()))
				return PointLightSnapshotStatus::kUnsupportedRuntime;
			if (!PointerPlausible(request.liveShadowSceneNode) || !request.admittedWorldBounds ||
				request.admittedWorldBoundCount == 0 ||
				request.admittedWorldBoundCount > kMaximumLiveOrdinaryLights * 4u ||
				request.renderThreadId == 0)
				return PointLightSnapshotStatus::kInvalidRequest;
			if (GetCurrentThreadId() != request.renderThreadId)
				return PointLightSnapshotStatus::kWrongThread;
			for (std::size_t index = 0; index < request.admittedWorldBoundCount; ++index) {
				const auto& bound = request.admittedWorldBounds[index];
				if (!FinitePoint(bound.center) || !std::isfinite(bound.fRadius) ||
					bound.fRadius < 0.0f || bound.fRadius > kMaximumPointRadius)
					return PointLightSnapshotStatus::kInvalidRequest;
			}

			std::uintptr_t liveSSNVtable = 0;
			REL::Relocation<std::uintptr_t> expectedSSNVtable{ RE::VTABLE::ShadowSceneNode[0] };
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(request.liveShadowSceneNode), liveSSNVtable) ||
				liveSSNVtable != expectedSSNVtable.address())
				return PointLightSnapshotStatus::kInvalidRequest;

			LightingSnapshot lighting{};
			if (!SnapshotPointLighting(request, lighting))
				return PointLightSnapshotStatus::kLiveLightingSnapshotFailed;
			for (std::uint32_t index = 0; index < lighting.pointCount; ++index)
				snapshot.pointLights[index] = ExportPointValues(lighting.points[index]);
			snapshot.pointLightCount = lighting.pointCount;
			return PointLightSnapshotStatus::kSuccess;
		} catch (...) {
			snapshot = {};
			return PointLightSnapshotStatus::kLiveLightingSnapshotFailed;
		}
	}

	Status Acquire(const AcquireRequest& request, Lease& lease) noexcept
	{
		try {
			if (lease.active)
				return Status::kBusy;
			if (REL::Module::IsVR() || (REL::Module::get().version() != REL::Version{ 1, 10, 163, 0 } && !ReflectionRuntime::IsPort240()))
				return Status::kUnsupportedRuntime;

			const bool frozenCodeDiagnostic = AttestOwnerFrozenCodeContract();
			if (!frozenCodeDiagnostic) {
				static std::atomic<bool> logged{ false };
				if (!logged.exchange(true, std::memory_order_relaxed))
					logger::warn(
						"[FlatReflectionRendererOwner] resident code hashes differ from the reference install; continuing under runtime and live-identity attestation");
			}
			if (!request.device || !PointerPlausible(request.liveShadowSceneNode) || request.loadGeneration == 0 ||
				request.renderThreadId == 0 ||
				(request.admittedWorldBounds == nullptr) != (request.admittedWorldBoundCount == 0) ||
				request.admittedWorldBoundCount > kMaximumLiveOrdinaryLights * 4u)
				return Status::kInvalidRequest;
			if (request.admittedWorldBounds) {
				for (std::size_t index = 0; index < request.admittedWorldBoundCount; ++index) {
					const auto& bound = request.admittedWorldBounds[index];
					if (!FinitePoint(bound.center) || !std::isfinite(bound.fRadius) ||
						bound.fRadius < 0.0f || bound.fRadius > kMaximumPointRadius)
						return Status::kInvalidRequest;
				}
			} else if (!FinitePoint(request.playerWorldBound.center) ||
				!std::isfinite(request.playerWorldBound.fRadius) || request.playerWorldBound.fRadius < 0.0f ||
				request.playerWorldBound.fRadius > kMaximumPointRadius) {
				return Status::kInvalidRequest;
			}
			if (GetCurrentThreadId() != request.renderThreadId)
				return Status::kWrongThread;
			if (!DeviceHealthy(request.device))
				return Status::kInvalidRequest;

			std::uintptr_t liveSSNVtable = 0;
			REL::Relocation<std::uintptr_t> expectedSSNVtable{ RE::VTABLE::ShadowSceneNode[0] };
			if (!SafeRead(reinterpret_cast<std::uintptr_t>(request.liveShadowSceneNode), liveSSNVtable) ||
				liveSSNVtable != expectedSSNVtable.address())
				return Status::kInvalidRequest;

			{
				OwnerExclusiveLock ownerLock{ g_owner.lock };
				ReclaimAbandonedGenerationsForCurrentThreadLocked(g_owner);
				RecoverPermanentFailureIntoAvailableQuarantineLocked(g_owner);
				if (g_owner.permanentFailure)
					return Status::kReleaseFailed;
				if (g_owner.activeToken != 0)
					return Status::kBusy;
				if (g_owner.renderer && g_owner.renderThreadId != request.renderThreadId &&
					!LegitimateThreadRollover(g_owner, request))
					return Status::kWrongThread;
				if (g_owner.renderer && g_owner.renderThreadId == request.renderThreadId &&
					(g_owner.poisoned || g_owner.releasePending) && !ReleaseOwnerLocked(g_owner))
					return Status::kReleaseFailed;
			}

			LightingSnapshot lighting{};
			if (!SnapshotLighting(request, lighting))
				return Status::kLiveLightingSnapshotFailed;
			if (!lighting.directional.contributes && lighting.pointCount == 0)
				return Status::kNoLightingAuthority;

			OwnerExclusiveLock ownerLock{ g_owner.lock };
			ReclaimAbandonedGenerationsForCurrentThreadLocked(g_owner);
			RecoverPermanentFailureIntoAvailableQuarantineLocked(g_owner);
			if (g_owner.permanentFailure)
				return Status::kReleaseFailed;
			if (g_owner.activeToken != 0)
				return Status::kBusy;
			if (g_owner.renderer && (g_owner.renderThreadId == 0 ||
				GetCurrentThreadId() != g_owner.renderThreadId)) {
				if (!LegitimateThreadRollover(g_owner, request))
					return Status::kWrongThread;
				const bool safelyIdle = OwnerGenerationSafelyIdleLocked(g_owner);
				if (!QuarantineOwnerGenerationLocked(
						g_owner, "render-thread rollover", safelyIdle))
					return Status::kReleaseFailed;
			}

			const bool identityChanged = g_owner.renderer &&
				(g_owner.device.Get() != request.device ||
				 g_owner.liveShadowSceneNode != request.liveShadowSceneNode ||
				 g_owner.loadGeneration != request.loadGeneration ||
				 g_owner.renderThreadId != request.renderThreadId ||
				 g_owner.pointTopologySignature != lighting.pointTopologySignature ||
				 !SelectedPointTopologyEqual(g_owner, lighting));
			if (g_owner.renderer && (identityChanged || g_owner.poisoned || g_owner.releasePending ||
				!AttestExistingGeneration(g_owner) ||
				!PrivateGraphDisjointFromSnapshot(g_owner, lighting))) {
				if (!ReleaseOwnerLocked(g_owner))
					return Status::kReleaseFailed;
			}

			if (!g_owner.renderer) {
				const auto createStatus = CreateGenerationLocked(request, lighting, g_owner);
				if (createStatus != Status::kSuccess)
					return createStatus;
			} else {
				EngineCallResult directionalCall{};
				if (!ApplyDirectionalSnapshotLeaf(g_owner.directionalLight.get(), lighting.directional, directionalCall)) {
					g_owner.poisoned = true;
					if (!ReleaseOwnerLocked(g_owner))
						return Status::kReleaseFailed;
					return Status::kPrivateLightCreationFailed;
				}
				g_owner.directionalLightValues = ExportDirectionalValues(lighting.directional);
				if (!AttestAppliedDirectional(
						g_owner.directionalLight.get(), g_owner.directionalLightValues)) {
					g_owner.poisoned = true;
					if (!ReleaseOwnerLocked(g_owner))
						return Status::kReleaseFailed;
					return Status::kPrivateRendererAttestationFailed;
				}
				if (!SelectedPointValuesEqual(g_owner, lighting)) {
					EngineCallResult pointCall{};
					if (!ApplyPointSnapshotsLeaf(g_owner, lighting, pointCall)) {
						g_owner.poisoned = true;
						if (!ReleaseOwnerLocked(g_owner))
							return Status::kReleaseFailed;
						return Status::kPrivateLightCreationFailed;
					}
					for (std::uint32_t index = 0; index < lighting.pointCount; ++index)
						g_owner.pointLightValues[index] = ExportPointValues(lighting.points[index]);
				}
				if (!AttestExistingGeneration(g_owner)) {
					g_owner.poisoned = true;
					if (!ReleaseOwnerLocked(g_owner))
						return Status::kReleaseFailed;
					return Status::kPrivateRendererAttestationFailed;
				}
			}

			lease.renderer = g_owner.renderer;
			lease.accumulator = g_owner.accumulator;
			lease.modelRoot = g_owner.modelRoot;
			lease.shadowSceneNode = g_owner.shadowSceneNode;
			lease.directionalLight = g_owner.directionalLight;
			lease.directionalLightValues = g_owner.directionalLightValues;
			lease.pointLights = g_owner.pointLights;
			lease.pointLightValues = g_owner.pointLightValues;
			lease.pointLightCount = g_owner.pointLightCount;
			lease.compositeFlag = true;
			lease.generationSerial = g_owner.generationSerial;
			lease.nativeLightingCallbackEntered = false;
			lease.nativeLightingCallbackSucceeded = false;
			lease.nativePostCleanupAttested = false;
			const std::uint64_t token = g_nextLeaseToken.fetch_add(1, std::memory_order_relaxed);
			g_owner.activeToken = token ? token : g_nextLeaseToken.fetch_add(1, std::memory_order_relaxed);
			lease.ownerToken = g_owner.activeToken;
			lease.active = true;
			return Status::kSuccess;
		} catch (...) {
			OwnerExclusiveLock ownerLock{ g_owner.lock };
			g_owner.poisoned = g_owner.renderer != nullptr;
			g_owner.releasePending = g_owner.renderer != nullptr;
			if (g_owner.renderer && g_owner.activeToken == 0 &&
				g_owner.renderThreadId != 0 && GetCurrentThreadId() == g_owner.renderThreadId)
				(void)ReleaseOwnerLocked(g_owner);
			return Status::kCreateFailed;
		}
	}

	void FinishLease(Lease& lease, FinishDisposition disposition) noexcept
	{
		if (!lease.active)
			return;
		OwnerExclusiveLock ownerLock{ g_owner.lock };
		const bool ownsToken = g_owner.activeToken != 0 && g_owner.activeToken == lease.ownerToken;
		if (ownsToken) {
			const bool validDisposition = disposition == FinishDisposition::kReusable ||
				disposition == FinishDisposition::kRelease ||
				disposition == FinishDisposition::kQuarantine;
			const bool identityUnchanged = LeaseGraphIdentityMatchesOwner(lease, g_owner);
			const bool ownerThread = g_owner.renderThreadId != 0 &&
				GetCurrentThreadId() == g_owner.renderThreadId;
			const bool safelyIdle = ownerThread && identityUnchanged &&
				OwnerGenerationSafelyIdleLocked(g_owner);
			const bool cleanPreDrawLightingRejection =
				lease.nativeLightingCallbackEntered && !lease.nativeLightingCallbackSucceeded;
			const bool provenPostCleanup = lease.nativeLightingCallbackEntered &&
				lease.nativeLightingCallbackSucceeded && lease.nativePostCleanupAttested;
			g_owner.activeToken = 0;
			if (!identityUnchanged || !validDisposition)
				disposition = FinishDisposition::kQuarantine;

			if (!ownerThread) {

				if (!QuarantineOwnerGenerationLocked(
						g_owner, "foreign-thread lease finish", false)) {
					g_owner.poisoned = true;
					g_owner.permanentFailure = true;
					g_owner.releasePending = true;
				}
			} else {

				if (disposition == FinishDisposition::kQuarantine && safelyIdle &&
					(cleanPreDrawLightingRejection || provenPostCleanup))
					disposition = FinishDisposition::kReusable;

				if (disposition != FinishDisposition::kQuarantine && !safelyIdle)
					disposition = FinishDisposition::kQuarantine;
				if (disposition == FinishDisposition::kReusable && !DeviceHealthy(g_owner.device.Get()))
					disposition = FinishDisposition::kRelease;

				if (disposition == FinishDisposition::kQuarantine) {

					if (!QuarantineOwnerGenerationLocked(
							g_owner, "native-capture quarantine", false)) {

						g_owner.poisoned = true;
						g_owner.permanentFailure = true;
						g_owner.releasePending = true;
					}
				} else if (disposition == FinishDisposition::kRelease) {

					g_owner.poisoned = true;
					g_owner.releasePending = true;
					(void)ReleaseOwnerLocked(g_owner);
				} else {
					g_owner.poisoned = false;
					g_owner.releasePending = false;
				}
			}
		}
		lease.renderer = nullptr;
		lease.accumulator = nullptr;
		lease.modelRoot.reset();
		lease.shadowSceneNode.reset();
		lease.directionalLight.reset();
		lease.directionalLightValues = {};
		for (auto& light : lease.pointLights)
			light.reset();
		lease.pointLightValues = {};
		lease.pointLightCount = 0;
		lease.compositeFlag = false;
		lease.generationSerial = 0;
		lease.ownerToken = 0;
		lease.nativeLightingCallbackEntered = false;
		lease.nativeLightingCallbackSucceeded = false;
		lease.nativePostCleanupAttested = false;
		lease.active = false;
	}

	void Invalidate(std::uint32_t renderThreadId) noexcept
	{
		OwnerExclusiveLock ownerLock{ g_owner.lock };
		ReclaimAbandonedGenerationsForCurrentThreadLocked(g_owner);
		RecoverPermanentFailureIntoAvailableQuarantineLocked(g_owner);

		if (g_owner.permanentFailure)
			return;
		if (!g_owner.renderer)
			return;
		if (g_owner.activeToken != 0) {
			g_owner.poisoned = true;
			g_owner.releasePending = true;
			return;
		}
		if (renderThreadId == 0 || GetCurrentThreadId() != renderThreadId ||
			g_owner.renderThreadId != renderThreadId) {
			const bool successorRenderThread = renderThreadId != 0 &&
				GetCurrentThreadId() == renderThreadId;
			const bool safelyIdle = successorRenderThread &&
				OwnerGenerationSafelyIdleLocked(g_owner);
			try {
				logger::warn(
					"[FlatReflectionRendererOwner] foreign-thread invalidation detached: ownerThread={} callerThread={} reclaimable={}",
					g_owner.renderThreadId, GetCurrentThreadId(), safelyIdle);
			} catch (...) {
			}
			if (!QuarantineOwnerGenerationLocked(
					g_owner, "foreign-thread invalidation", safelyIdle)) {
				g_owner.poisoned = true;
				g_owner.permanentFailure = true;
				g_owner.releasePending = true;
			}
			return;
		}
		g_owner.poisoned = true;
		g_owner.releasePending = true;
		(void)ReleaseOwnerLocked(g_owner);
	}
}
