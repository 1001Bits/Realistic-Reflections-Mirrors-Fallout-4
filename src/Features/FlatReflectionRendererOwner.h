#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>

#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiBound.h"
#include "RE/NetImmerse/NiSmartPointer.h"

namespace RE::Interface3D
{
	class Renderer;
}

namespace RE
{
	class NiCamera;
}

namespace FlatDeferredPlayerCapture
{
	struct NativeWorldLightingPreparationView;
	struct NativeWorldOwnerAttestationView;
}

namespace FlatReflectionRendererOwner
{
	inline constexpr std::size_t kMaximumPrivatePointLights = 8;

	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kUnsupportedRuntime,
		kContractMismatch,
		kInvalidRequest,
		kWrongThread,
		kBusy,
		kLiveLightingSnapshotFailed,
		kNoLightingAuthority,
		kRegistryCollision,
		kCreateFailed,
		kPrivateLightCreationFailed,
		kPrivateRendererAttestationFailed,
		kReleaseFailed
	};

	enum class FinishDisposition : std::uint32_t
	{
		
		kReusable = 0,
		
		kRelease,

		kQuarantine
	};

	struct AcquireRequest
	{
		
		void* liveShadowSceneNode{};
		
		RE::NiBound playerWorldBound{};

		const RE::NiBound* admittedWorldBounds{};
		std::size_t admittedWorldBoundCount{};
		ID3D11Device* device{};
		std::uint64_t loadGeneration{};
		std::uint32_t renderThreadId{};
	};

	struct PrivatePointLightValues
	{
		std::array<float, 3> translation{};
		std::array<float, 3> diffuse{};
		float radius{};
		float dimmer{};
	};

	struct PrivateDirectionalLightValues
	{
		
		std::array<float, 12> localRotation{};
		
		std::array<float, 10> lightPOD{};
	};
	static_assert(sizeof(PrivatePointLightValues) == 0x20);
	static_assert(sizeof(PrivateDirectionalLightValues) == 0x58);

	enum class PointLightSnapshotStatus : std::uint32_t
	{
		kSuccess = 0,
		kUnsupportedRuntime,
		kContractMismatch,
		kInvalidRequest,
		kWrongThread,
		kLiveLightingSnapshotFailed
	};

	struct PointLightSnapshotRequest
	{
		
		void* liveShadowSceneNode{};
		
		const RE::NiBound* admittedWorldBounds{};
		std::size_t admittedWorldBoundCount{};
		std::uint32_t renderThreadId{};
	};

	struct PointLightValuesSnapshot
	{
		std::array<PrivatePointLightValues, kMaximumPrivatePointLights> pointLights{};
		std::uint32_t pointLightCount{};
	};

	class Lease
	{
	public:
		static constexpr std::size_t kMaximumPointLights = kMaximumPrivatePointLights;

		Lease() noexcept = default;
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		Lease(Lease&& other) noexcept;
		Lease& operator=(Lease&& other) noexcept;
		~Lease() noexcept;

		[[nodiscard]] explicit operator bool() const noexcept { return active; }

		void Finish(FinishDisposition disposition) noexcept;

		RE::Interface3D::Renderer* renderer{};
		void* accumulator{};
		
		RE::NiPointer<RE::NiAVObject> modelRoot{};
		RE::NiPointer<RE::NiAVObject> shadowSceneNode{};
		RE::NiPointer<RE::NiAVObject> directionalLight{};
		PrivateDirectionalLightValues directionalLightValues{};
		std::array<RE::NiPointer<RE::NiAVObject>, kMaximumPointLights> pointLights{};
		std::array<PrivatePointLightValues, kMaximumPointLights> pointLightValues{};
		std::uint32_t pointLightCount{};
		
		bool compositeFlag{};
		std::uint64_t generationSerial{};

	private:
		friend Status Acquire(const AcquireRequest&, Lease&) noexcept;
		friend void FinishLease(Lease&, FinishDisposition) noexcept;
		friend bool PrepareNativeWorldLightingCallback(
			void*, const FlatDeferredPlayerCapture::NativeWorldLightingPreparationView&,
			std::uint32_t*) noexcept;
		friend bool AttestNativeWorldOwnerCallback(
			void*, const FlatDeferredPlayerCapture::NativeWorldOwnerAttestationView&,
			std::uint32_t*) noexcept;
		void MoveFrom(Lease& other) noexcept;

		std::uint64_t ownerToken{};

		bool nativeLightingCallbackEntered{};
		bool nativeLightingCallbackSucceeded{};
		bool nativePostCleanupAttested{};
		bool active{};
	};

	struct NativeWorldCallbackContext
	{
		Lease* lease{};
		void* exclusiveAccumulator{};
		RE::NiCamera* captureCamera{};
		RE::NiAVObject* const* submittedRoots{};
		std::uint32_t submittedRootCount{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t frameSerial{};

		RE::NiAVObject* const* sealedSubmittedRoots{};
		std::uint32_t sealedSubmittedRootCount{};
		bool lightingPreparationAttempted{};
		bool rendererPrefixPublished{};
		bool lightingPreparationCompleted{};
	};

	[[nodiscard]] bool PrepareNativeWorldLightingCallback(
		void* opaqueContext,
		const FlatDeferredPlayerCapture::NativeWorldLightingPreparationView& view,
		std::uint32_t* failureCode) noexcept;

	[[nodiscard]] bool AttestNativeWorldOwnerCallback(
		void* opaqueContext,
		const FlatDeferredPlayerCapture::NativeWorldOwnerAttestationView& view,
		std::uint32_t* failureCode) noexcept;

	[[nodiscard]] Status Acquire(const AcquireRequest& request, Lease& lease) noexcept;

	[[nodiscard]] PointLightSnapshotStatus SnapshotPointLights(
		const PointLightSnapshotRequest& request,
		PointLightValuesSnapshot& snapshot) noexcept;

	void Invalidate(std::uint32_t renderThreadId) noexcept;
}
