#pragma once

#include <array>
#include <cstdint>

#include <DirectXMath.h>

namespace PlanarMirrorMath
{
	
	struct Plane
	{
		DirectX::XMFLOAT3 normal{ 0.0f, 0.0f, 1.0f };
		float distance{ 0.0f };
	};

	struct PlaneSelectionInput
	{
		DirectX::XMFLOAT3 center{};
		std::array<DirectX::XMFLOAT3, 3> worldAxes{};
		std::array<float, 3> localHalfExtents{};
		DirectX::XMFLOAT3 eye{};
		std::int32_t normalAxisHint{ -1 };
		float maximumThicknessRatio{ 0.35f };
		float minimumFacingCosine{ 0.02f };
	};

	struct PlaneSelection
	{
		Plane plane{};
		DirectX::XMFLOAT3 center{};
		DirectX::XMFLOAT3 tangent{};
		DirectX::XMFLOAT3 bitangent{};
		std::int32_t normalAxis{ -1 };
		float facingCosine{ 0.0f };
		float confidence{ 0.0f };
	};

	enum class ObliqueProjectionFailure : std::uint8_t
	{
		kNone,
		kInvalidCameraOrigin,
		kInvalidClipBias,
		kInvalidWorldPlane,
		kInvalidViewMatrix,
		kSingularViewMatrix,
		kInvalidCameraPlane,
		kDegenerateCameraPlane,
		kInvalidProjectionMatrix,
		kSingularProjectionMatrix,
		kInvalidDenominator,
		kNonPositiveDenominator,
		kInvalidScaledPlane,
		kInvalidObliqueProjection,
		kInvalidViewProjection
	};

	struct ObliqueProjectionDiagnostics
	{
		ObliqueProjectionFailure failure{ ObliqueProjectionFailure::kNone };
		float viewDeterminant{ 0.0f };
		float projectionDeterminant{ 0.0f };
		float denominator{ 0.0f };
		DirectX::XMFLOAT4 cameraPlane{};  
		DirectX::XMFLOAT4 clipCorner{};   
	};

	const char* ObliqueProjectionFailureName(ObliqueProjectionFailure failure) noexcept;

	bool NormalizePlane(Plane& plane) noexcept;

	float SignedDistance(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept;

	bool SphereClearance(const Plane& plane, const DirectX::XMFLOAT3& center,
		float radius, float minimumDistance, float& translation) noexcept;

	DirectX::XMFLOAT3 ReflectPoint(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept;

	DirectX::XMFLOAT3 ReflectVector(const Plane& plane, const DirectX::XMFLOAT3& direction) noexcept;

	bool PaneIntersectsView(
		const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT4X4& viewProjection,
		float& projectedArea, float clipEdgeMargin = 1.05f) noexcept;

	float PaneDistanceSquared(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye) noexcept;

	bool FlatPaneCaptureBasis(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, const DirectX::XMFLOAT3& reflectedEye,
		DirectX::XMFLOAT3& forward, DirectX::XMFLOAT3& up, DirectX::XMFLOAT3& right) noexcept;

	bool WholePaneReflectionView(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& viewerEye, DirectX::XMFLOAT3& reflectedEye,
		DirectX::XMFLOAT4X4& viewProjection) noexcept;

	bool FitPaneFrustum(
		const DirectX::XMFLOAT3& center,
		const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent,
		float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye,
		const DirectX::XMFLOAT4X4& viewProjection,
		const DirectX::XMFLOAT4& sourceSlopes,
		DirectX::XMFLOAT4& fittedSlopes,
		const DirectX::XMFLOAT4X4* visibleViewProjection = nullptr,
		const DirectX::XMFLOAT3* visibleEye = nullptr,
		float cropMargin = 0.0f) noexcept;

	float FrustumCollectionRadius(const DirectX::XMFLOAT4& slopes, float farDistance,
		const DirectX::XMFLOAT3& cameraOrigin, const DirectX::XMFLOAT3& collectionOrigin) noexcept;

	bool SelectPlaneFromBasis(const PlaneSelectionInput& input, PlaneSelection& output) noexcept;

	bool TransformPlaneToView(
		const Plane& plane,
		const DirectX::XMFLOAT4X4& view,
		DirectX::XMFLOAT4& cameraPlane) noexcept;

	bool BuildObliqueNearProjection(
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4& cameraPlane,
		DirectX::XMFLOAT4X4& output) noexcept;

	bool BuildObliqueViewProjection(
		const Plane& absoluteWorldPlane,
		float clipBias,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		DirectX::XMFLOAT4X4& outputProjection,
		DirectX::XMFLOAT4X4& outputViewProjection,
		DirectX::XMFLOAT4* outputCameraPlane = nullptr,
		ObliqueProjectionDiagnostics* diagnostics = nullptr) noexcept;
}
