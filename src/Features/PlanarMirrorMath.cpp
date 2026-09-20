#include "PlanarMirrorMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <limits>

namespace
{
	constexpr float kEpsilon = 1.0e-6f;

	bool Finite(float value) noexcept
	{
		return std::isfinite(value);
	}

	bool Finite(const DirectX::XMFLOAT3& value) noexcept
	{
		return Finite(value.x) && Finite(value.y) && Finite(value.z);
	}

	bool Finite(const DirectX::XMFLOAT4& value) noexcept
	{
		return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
	}

	float Dot(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
	}

	DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) noexcept
	{
		return { value.x * scale, value.y * scale, value.z * scale };
	}

	DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return {
			lhs.y * rhs.z - lhs.z * rhs.y,
			lhs.z * rhs.x - lhs.x * rhs.z,
			lhs.x * rhs.y - lhs.y * rhs.x
		};
	}

	bool Normalize(DirectX::XMFLOAT3& value) noexcept
	{
		if (!Finite(value))
			return false;
		const float lengthSquared = Dot(value, value);
		if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
			return false;
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		value = Scale(value, inverseLength);
		return true;
	}

	float SignNotZero(float value) noexcept
	{
		return value < 0.0f ? -1.0f : 1.0f;
	}

	bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
	{
		const float* values = &matrix._11;
		for (std::size_t index = 0; index < 16; ++index) {
			if (!Finite(values[index]))
				return false;
		}
		return true;
	}

	bool TransformPlaneToViewDetailed(
		const PlanarMirrorMath::Plane& plane,
		const DirectX::XMFLOAT4X4& view,
		DirectX::XMFLOAT4& cameraPlane,
		PlanarMirrorMath::ObliqueProjectionDiagnostics* diagnostics) noexcept
	{
		PlanarMirrorMath::Plane normalizedPlane = plane;
		if (!PlanarMirrorMath::NormalizePlane(normalizedPlane)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidWorldPlane;
			return false;
		}
		if (!FiniteMatrix(view)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidViewMatrix;
			return false;
		}

		using namespace DirectX;
		const XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
		XMVECTOR determinant{};
		const XMMATRIX inverseView = XMMatrixInverse(&determinant, viewMatrix);
		const float determinantValue = XMVectorGetX(determinant);
		if (diagnostics)
			diagnostics->viewDeterminant = determinantValue;
		if (!Finite(determinantValue) || std::abs(determinantValue) <= kEpsilon) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kSingularViewMatrix;
			return false;
		}

		const XMVECTOR worldCoefficients = XMVectorSet(
			normalizedPlane.normal.x,
			normalizedPlane.normal.y,
			normalizedPlane.normal.z,
			-normalizedPlane.distance);
		const XMVECTOR transformed = XMVector4Transform(worldCoefficients, XMMatrixTranspose(inverseView));
		XMStoreFloat4(&cameraPlane, transformed);
		if (!Finite(cameraPlane)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidCameraPlane;
			return false;
		}

		const float normalLength = std::sqrt(
			cameraPlane.x * cameraPlane.x + cameraPlane.y * cameraPlane.y + cameraPlane.z * cameraPlane.z);
		if (!Finite(normalLength) || normalLength <= kEpsilon) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kDegenerateCameraPlane;
			return false;
		}
		const float inverseLength = 1.0f / normalLength;
		cameraPlane.x *= inverseLength;
		cameraPlane.y *= inverseLength;
		cameraPlane.z *= inverseLength;
		cameraPlane.w *= inverseLength;
		if (diagnostics)
			diagnostics->cameraPlane = cameraPlane;
		if (!Finite(cameraPlane)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidCameraPlane;
			return false;
		}
		return true;
	}

	bool BuildObliqueNearProjectionDetailed(
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4& cameraPlane,
		DirectX::XMFLOAT4X4& output,
		PlanarMirrorMath::ObliqueProjectionDiagnostics* diagnostics) noexcept
	{
		if (!FiniteMatrix(projection)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidProjectionMatrix;
			return false;
		}
		if (!Finite(cameraPlane)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidCameraPlane;
			return false;
		}
		const float planeNormalLengthSquared =
			cameraPlane.x * cameraPlane.x + cameraPlane.y * cameraPlane.y + cameraPlane.z * cameraPlane.z;
		if (!Finite(planeNormalLengthSquared) || planeNormalLengthSquared <= kEpsilon * kEpsilon) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kDegenerateCameraPlane;
			return false;
		}

		using namespace DirectX;
		const XMMATRIX projectionMatrix = XMLoadFloat4x4(&projection);
		XMVECTOR determinant{};
		const XMMATRIX inverseProjection = XMMatrixInverse(&determinant, projectionMatrix);
		const float determinantValue = XMVectorGetX(determinant);
		if (diagnostics)
			diagnostics->projectionDeterminant = determinantValue;
		if (!Finite(determinantValue) || std::abs(determinantValue) <= kEpsilon) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kSingularProjectionMatrix;
			return false;
		}

		const XMVECTOR clipCorner = XMVectorSet(
			SignNotZero(cameraPlane.x),
			SignNotZero(cameraPlane.y),
			1.0f,
			1.0f);
		const XMVECTOR q = XMVector4Transform(clipCorner, inverseProjection);
		const XMVECTOR planeVector = XMLoadFloat4(&cameraPlane);
		const float denominator = XMVectorGetX(XMVector4Dot(planeVector, q));
		if (diagnostics) {
			diagnostics->denominator = denominator;
			XMStoreFloat4(&diagnostics->clipCorner, q);
		}
		if (!Finite(denominator)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidDenominator;
			return false;
		}
		if (denominator <= kEpsilon) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kNonPositiveDenominator;
			return false;
		}

		DirectX::XMFLOAT4 scaledPlane{};
		XMStoreFloat4(&scaledPlane, XMVectorScale(planeVector, 1.0f / denominator));
		if (!Finite(scaledPlane)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidScaledPlane;
			return false;
		}

		output = projection;
		output._13 = scaledPlane.x;
		output._23 = scaledPlane.y;
		output._33 = scaledPlane.z;
		output._43 = scaledPlane.w;
		if (!FiniteMatrix(output)) {
			if (diagnostics)
				diagnostics->failure = PlanarMirrorMath::ObliqueProjectionFailure::kInvalidObliqueProjection;
			return false;
		}
		return true;
	}
}

namespace PlanarMirrorMath
{
	float PaneDistanceSquared(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye) noexcept
	{
		const auto invalid = std::numeric_limits<float>::infinity();
		auto u = tangent;
		auto v = bitangent;
		if (!Finite(center) || !Finite(eye) || !Finite(halfWidth) || !Finite(halfHeight) ||
			halfWidth <= 0.0f || halfHeight <= 0.0f || !Normalize(u) || !Normalize(v) ||
			std::fabs(Dot(u, v)) > 0.001f)
			return invalid;
		
		v = Subtract(v, Scale(u, Dot(u, v)));
		if (!Normalize(v)) return invalid;
		const auto offset = Subtract(eye, center);
		const float x = std::clamp(Dot(offset, u), -halfWidth, halfWidth);
		const float y = std::clamp(Dot(offset, v), -halfHeight, halfHeight);
		const auto nearest = Subtract(Subtract(offset, Scale(u, x)), Scale(v, y));
		const float distanceSq = Dot(nearest, nearest);
		return Finite(distanceSq) ? distanceSq : invalid;
	}

	const char* ObliqueProjectionFailureName(ObliqueProjectionFailure failure) noexcept
	{
		switch (failure) {
		case ObliqueProjectionFailure::kNone: return "none";
		case ObliqueProjectionFailure::kInvalidCameraOrigin: return "invalid-camera-origin";
		case ObliqueProjectionFailure::kInvalidClipBias: return "invalid-clip-bias";
		case ObliqueProjectionFailure::kInvalidWorldPlane: return "invalid-world-plane";
		case ObliqueProjectionFailure::kInvalidViewMatrix: return "invalid-view-matrix";
		case ObliqueProjectionFailure::kSingularViewMatrix: return "singular-view-matrix";
		case ObliqueProjectionFailure::kInvalidCameraPlane: return "invalid-camera-plane";
		case ObliqueProjectionFailure::kDegenerateCameraPlane: return "degenerate-camera-plane";
		case ObliqueProjectionFailure::kInvalidProjectionMatrix: return "invalid-projection-matrix";
		case ObliqueProjectionFailure::kSingularProjectionMatrix: return "singular-projection-matrix";
		case ObliqueProjectionFailure::kInvalidDenominator: return "invalid-denominator";
		case ObliqueProjectionFailure::kNonPositiveDenominator: return "non-positive-denominator";
		case ObliqueProjectionFailure::kInvalidScaledPlane: return "invalid-scaled-plane";
		case ObliqueProjectionFailure::kInvalidObliqueProjection: return "invalid-oblique-projection";
		case ObliqueProjectionFailure::kInvalidViewProjection: return "invalid-view-projection";
		default: return "unknown";
		}
	}

	bool PaneIntersectsView(
		const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT4X4& matrix,
		float& projectedArea, float clipEdgeMargin) noexcept
	{
		projectedArea = 0.0f;
		using namespace DirectX;
		const auto viewProjection = XMLoadFloat4x4(&matrix);
		XMFLOAT4X4 matrixValues{};
		XMStoreFloat4x4(std::addressof(matrixValues), viewProjection);
		const auto* matrixFloats = reinterpret_cast<const float*>(std::addressof(matrixValues));
		if (!std::all_of(matrixFloats, matrixFloats + 16, [](float value) noexcept {
				return std::isfinite(value);
			}))
			return true;
		if (std::fabs(matrix._14) + std::fabs(matrix._24) + std::fabs(matrix._34) +
			std::fabs(matrix._44) < 1.0e-6f)
			return true;
		const XMFLOAT3 relativeCenter{
			center.x - eye.x,
			center.y - eye.y,
			center.z - eye.z
		};
		std::array<XMFLOAT4, 4> clipCorners{};
		std::size_t cornerIndex = 0;
		for (const float tangentSign : { -1.0f, 1.0f }) {
			for (const float bitangentSign : { -1.0f, 1.0f }) {
				const XMFLOAT3 corner{
					relativeCenter.x + tangentSign * halfWidth * tangent.x +
						bitangentSign * halfHeight * bitangent.x,
					relativeCenter.y + tangentSign * halfWidth * tangent.y +
						bitangentSign * halfHeight * bitangent.y,
					relativeCenter.z + tangentSign * halfWidth * tangent.z +
						bitangentSign * halfHeight * bitangent.z
				};
				XMStoreFloat4(
					std::addressof(clipCorners[cornerIndex++]),
					XMVector4Transform(XMVectorSet(corner.x, corner.y, corner.z, 1.0f), viewProjection));
			}
		}
		if (!std::all_of(clipCorners.begin(), clipCorners.end(), [](const XMFLOAT4& clip) noexcept {
				return std::isfinite(clip.x) && std::isfinite(clip.y) &&
					std::isfinite(clip.z) && std::isfinite(clip.w);
			}))
			return true;

		constexpr float kMinimumPositiveW = 1.0e-4f;
		const float kClipEdgeMargin = std::isfinite(clipEdgeMargin) && clipEdgeMargin >= 1.0f ? clipEdgeMargin : 1.05f;
		if (std::all_of(clipCorners.begin(), clipCorners.end(), [](const XMFLOAT4& clip) noexcept {
				return clip.w <= kMinimumPositiveW;
			}))
			return false;

		auto whollyOutside = [&](auto predicate) noexcept {
			return std::all_of(clipCorners.begin(), clipCorners.end(), [&](const XMFLOAT4& clip) noexcept {
				return clip.w > kMinimumPositiveW && predicate(clip);
			});
		};
		if (whollyOutside([kClipEdgeMargin](const XMFLOAT4& clip) noexcept {
				return clip.x < -kClipEdgeMargin * clip.w;
			}) ||
			whollyOutside([kClipEdgeMargin](const XMFLOAT4& clip) noexcept {
				return clip.x > kClipEdgeMargin * clip.w;
			}) ||
			whollyOutside([kClipEdgeMargin](const XMFLOAT4& clip) noexcept {
				return clip.y < -kClipEdgeMargin * clip.w;
			}) ||
			whollyOutside([kClipEdgeMargin](const XMFLOAT4& clip) noexcept {
				return clip.y > kClipEdgeMargin * clip.w;
			}))
			return false;

		float minimumX = kClipEdgeMargin;
		float maximumX = -kClipEdgeMargin;
		float minimumY = kClipEdgeMargin;
		float maximumY = -kClipEdgeMargin;
		bool projected = false;
		for (const auto& clip : clipCorners) {
			if (clip.w <= kMinimumPositiveW)
				continue;
			const float inverseW = 1.0f / clip.w;
			const float ndcX = std::clamp(clip.x * inverseW, -kClipEdgeMargin, kClipEdgeMargin);
			const float ndcY = std::clamp(clip.y * inverseW, -kClipEdgeMargin, kClipEdgeMargin);
			minimumX = std::min(minimumX, ndcX);
			maximumX = std::max(maximumX, ndcX);
			minimumY = std::min(minimumY, ndcY);
			maximumY = std::max(maximumY, ndcY);
			projected = true;
		}
		if (!projected)
			return false;
		projectedArea = std::max(0.0f, maximumX - minimumX) *
			std::max(0.0f, maximumY - minimumY);
		if (!std::isfinite(projectedArea)) {
			projectedArea = 0.0f;
			return true;
		}
		return true;
	}

	bool NormalizePlane(Plane& plane) noexcept
	{
		if (!Finite(plane.distance))
			return false;
		const float lengthSquared = Dot(plane.normal, plane.normal);
		if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
			return false;
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		plane.normal = Scale(plane.normal, inverseLength);
		plane.distance *= inverseLength;
		return Finite(plane.normal) && Finite(plane.distance);
	}

	float SignedDistance(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept
	{
		return Dot(plane.normal, point) - plane.distance;
	}

	DirectX::XMFLOAT3 ReflectPoint(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept
	{
		return Subtract(point, Scale(plane.normal, 2.0f * SignedDistance(plane, point)));
	}

	DirectX::XMFLOAT3 ReflectVector(const Plane& plane, const DirectX::XMFLOAT3& direction) noexcept
	{
		return Subtract(direction, Scale(plane.normal, 2.0f * Dot(plane.normal, direction)));
	}

	bool SelectPlaneFromBasis(const PlaneSelectionInput& input, PlaneSelection& output) noexcept
	{
		output = {};
		if (!Finite(input.center) || !Finite(input.eye))
			return false;

		std::array<DirectX::XMFLOAT3, 3> axes = input.worldAxes;
		for (auto& axis : axes) {
			if (!Normalize(axis))
				return false;
		}

		DirectX::XMFLOAT3 toEye = Subtract(input.eye, input.center);
		if (!Normalize(toEye))
			return false;

		std::int32_t normalAxis = input.normalAxisHint;
		float flatnessConfidence = 0.0f;
		if (normalAxis < 0 || normalAxis > 2) {
			const bool extentsValid =
				Finite(input.localHalfExtents[0]) && input.localHalfExtents[0] > kEpsilon &&
				Finite(input.localHalfExtents[1]) && input.localHalfExtents[1] > kEpsilon &&
				Finite(input.localHalfExtents[2]) && input.localHalfExtents[2] > kEpsilon;
			if (extentsValid) {
				std::array<std::int32_t, 3> order{ 0, 1, 2 };
				std::sort(order.begin(), order.end(), [&](std::int32_t lhs, std::int32_t rhs) {
					return input.localHalfExtents[lhs] < input.localHalfExtents[rhs];
				});
				const float thickness = input.localHalfExtents[order[0]];
				const float nextExtent = input.localHalfExtents[order[1]];
				const float ratio = thickness / nextExtent;
				if (ratio <= std::clamp(input.maximumThicknessRatio, 0.01f, 0.95f)) {
					normalAxis = order[0];
					flatnessConfidence = 1.0f - ratio;
				}
			}
		}

		if (normalAxis < 0 || normalAxis > 2) {
			float bestFacing = -1.0f;
			for (std::int32_t index = 0; index < 3; ++index) {
				const float facing = std::abs(Dot(axes[index], toEye));
				if (facing > bestFacing) {
					bestFacing = facing;
					normalAxis = index;
				}
			}
		}

		DirectX::XMFLOAT3 normal = axes[normalAxis];
		if (Dot(normal, toEye) < 0.0f)
			normal = Scale(normal, -1.0f);
		const float facingCosine = Dot(normal, toEye);
		if (!Finite(facingCosine) || facingCosine < std::clamp(input.minimumFacingCosine, 0.0f, 0.99f))
			return false;

		std::int32_t tangentAxis = (normalAxis + 1) % 3;
		const std::int32_t otherAxis = (normalAxis + 2) % 3;
		if (input.localHalfExtents[otherAxis] > input.localHalfExtents[tangentAxis])
			tangentAxis = otherAxis;

		DirectX::XMFLOAT3 tangent = Subtract(axes[tangentAxis], Scale(normal, Dot(axes[tangentAxis], normal)));
		if (!Normalize(tangent)) {
			tangentAxis = tangentAxis == otherAxis ? (normalAxis + 1) % 3 : otherAxis;
			tangent = Subtract(axes[tangentAxis], Scale(normal, Dot(axes[tangentAxis], normal)));
			if (!Normalize(tangent))
				return false;
		}
		DirectX::XMFLOAT3 bitangent = Cross(normal, tangent);
		if (!Normalize(bitangent))
			return false;
		const std::int32_t remainingAxis = 3 - normalAxis - tangentAxis;
		if (remainingAxis >= 0 && remainingAxis < 3 && Dot(bitangent, axes[remainingAxis]) < 0.0f)
			bitangent = Scale(bitangent, -1.0f);

		output.plane.normal = normal;
		output.plane.distance = Dot(normal, input.center);
		output.center = input.center;
		output.tangent = tangent;
		output.bitangent = bitangent;
		output.normalAxis = normalAxis;
		output.facingCosine = facingCosine;
		output.confidence = std::clamp(
			flatnessConfidence > 0.0f ? 0.65f * flatnessConfidence + 0.35f * facingCosine : facingCosine,
			0.0f,
			1.0f);
		return NormalizePlane(output.plane);
	}

	bool TransformPlaneToView(
		const Plane& plane,
		const DirectX::XMFLOAT4X4& view,
		DirectX::XMFLOAT4& cameraPlane) noexcept
	{
		return TransformPlaneToViewDetailed(plane, view, cameraPlane, nullptr);
	}

	bool BuildObliqueNearProjection(
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4& cameraPlane,
		DirectX::XMFLOAT4X4& output) noexcept
	{
		return BuildObliqueNearProjectionDetailed(projection, cameraPlane, output, nullptr);
	}

	float FrustumCollectionRadius(const DirectX::XMFLOAT4& slopes, float farDistance,
		const DirectX::XMFLOAT3& cameraOrigin, const DirectX::XMFLOAT3& collectionOrigin) noexcept
	{
		if (!Finite(slopes) || !Finite(farDistance) || farDistance <= 0 ||
			slopes.x >= slopes.y || slopes.z >= slopes.w || !Finite(cameraOrigin) || !Finite(collectionOrigin))
			return INFINITY;
		const double x = (std::max)(std::abs(double(slopes.x)), std::abs(double(slopes.y)));
		const double y = (std::max)(std::abs(double(slopes.z)), std::abs(double(slopes.w)));
		const double dx = double(cameraOrigin.x) - collectionOrigin.x;
		const double dy = double(cameraOrigin.y) - collectionOrigin.y;
		const double dz = double(cameraOrigin.z) - collectionOrigin.z;
		const double radius = double(farDistance) * std::sqrt(1 + x*x + y*y) + std::sqrt(dx*dx + dy*dy + dz*dz);
		return radius < FLT_MAX ? std::nextafter(float(radius), INFINITY) : INFINITY;
	}

	bool FlatPaneCaptureBasis(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, const DirectX::XMFLOAT3& reflectedEye,
		DirectX::XMFLOAT3& forward, DirectX::XMFLOAT3& up, DirectX::XMFLOAT3& right) noexcept
	{
		if (!Finite(center) || !Finite(reflectedEye)) return false;
		auto normal = Cross(tangent, bitangent);
		if (!Normalize(normal)) return false;
		const float side = Dot(Subtract(center, reflectedEye), normal);
		if (!Finite(side) || std::fabs(side) <= 1.e-4f) return false;
		const auto f = Scale(normal, side >= 0 ? 1.f : -1.f);
		auto u = Subtract(bitangent, Scale(f, Dot(bitangent, f)));
		if (!Normalize(u)) return false;
		const auto r = Cross(u, f);

		forward = f; up = Scale(u, -1.f); right = r;
		return true;
	}

	bool WholePaneReflectionView(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& viewerEye, DirectX::XMFLOAT3& reflectedEye,
		DirectX::XMFLOAT4X4& viewProjection) noexcept
	{
		using namespace DirectX;
		auto normal = Cross(tangent, bitangent);
		if (!Normalize(normal)) return false;
		const auto eye = ReflectPoint({normal, Dot(normal, center)}, viewerEye);
		XMFLOAT3 forward{}, up{}, right{};
		if (!FlatPaneCaptureBasis(center, tangent, bitangent, eye, forward, up, right)) return false;
		const auto view = XMMatrixSet(right.x, up.x, forward.x, 0, right.y, up.y, forward.y, 0,
			right.z, up.z, forward.z, 0, 0, 0, 0, 1);

		const XMFLOAT4 source{-1, 1, -1, 1};
		XMFLOAT4X4 original{};
		XMStoreFloat4x4(&original, view * XMMatrixPerspectiveOffCenterLH(-1, 1, -1, 1, 1, 2));
		XMFLOAT4 fitted{};
		if (!FitPaneFrustum(center, tangent, bitangent, halfWidth, halfHeight, eye, original, source, fitted))
			return false;
		XMFLOAT4X4 result{};
		XMStoreFloat4x4(&result, view * XMMatrixPerspectiveOffCenterLH(
			fitted.x, fitted.y, fitted.z, fitted.w, 1, 2));
		if (!FiniteMatrix(result)) return false;
		reflectedEye = eye;
		viewProjection = result;
		return true;
	}

	bool FitPaneFrustum(
		const DirectX::XMFLOAT3& center,
		const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent,
		float halfWidth, float halfHeight,
		const DirectX::XMFLOAT3& eye,
		const DirectX::XMFLOAT4X4& viewProjection,
		const DirectX::XMFLOAT4& sourceSlopes,
		DirectX::XMFLOAT4& fittedSlopes,
		const DirectX::XMFLOAT4X4* visibleViewProjection,
		const DirectX::XMFLOAT3* visibleEye,
		float cropMargin) noexcept
	{
		if (!Finite(center) || !Finite(tangent) || !Finite(bitangent) || !Finite(eye) ||
			!Finite(halfWidth) || !Finite(halfHeight) || halfWidth <= 0.0f || halfHeight <= 0.0f ||
			!FiniteMatrix(viewProjection) || !Finite(sourceSlopes) ||
			sourceSlopes.x >= sourceSlopes.y || sourceSlopes.z >= sourceSlopes.w)
			return false;
		
		std::array<DirectX::XMFLOAT3, 12> points{};
		std::size_t count = 0;
		for (int y : { -1, 1 }) {
			for (int x : { -y, y }) {
				points[count++] = {
					center.x + tangent.x * (x * halfWidth) + bitangent.x * (y * halfHeight),
					center.y + tangent.y * (x * halfWidth) + bitangent.y * (y * halfHeight),
					center.z + tangent.z * (x * halfWidth) + bitangent.z * (y * halfHeight) };
			}
		}
		const auto project = [](const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& origin,
								 const DirectX::XMFLOAT4X4& matrix) {
			DirectX::XMFLOAT4 clip{};
			DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
				DirectX::XMVectorSet(point.x - origin.x, point.y - origin.y, point.z - origin.z, 1.0f),
				DirectX::XMLoadFloat4x4(&matrix)));
			return clip;
		};

		if (visibleViewProjection && visibleEye && Finite(*visibleEye) && FiniteMatrix(*visibleViewProjection)) {
			std::array<DirectX::XMFLOAT4, 12> clip{}, clipScratch{};
			std::array<DirectX::XMFLOAT3, 12> kept{}, keptScratch{};
			std::size_t keptCount = count;
			bool usable = true;
			for (std::size_t index = 0; index < count; ++index) {
				clip[index] = project(points[index], *visibleEye, *visibleViewProjection);
				kept[index] = points[index];
				usable = usable && Finite(clip[index]);
			}
			const double reach = 1.0 + ((std::isfinite(cropMargin) && cropMargin > 0.0f) ? double(cropMargin) : 0.0);
			for (unsigned plane = 0; usable && plane < 5 && keptCount; ++plane) {
				const auto distance = [plane, reach](const DirectX::XMFLOAT4& p) -> double {
					switch (plane) {
					case 0: return double(p.w) - 1.0e-4;
					case 1: return double(p.w) * reach + p.x;
					case 2: return double(p.w) * reach - p.x;
					case 3: return double(p.w) * reach + p.y;
					default: return double(p.w) * reach - p.y;
					}
				};
				std::size_t outputCount = 0;
				auto previousClip = clip[keptCount - 1];
				auto previousPoint = kept[keptCount - 1];
				double previousDistance = distance(previousClip);
				for (std::size_t index = 0; index < keptCount && usable; ++index) {
					const auto currentClip = clip[index];
					const auto currentPoint = kept[index];
					const double currentDistance = distance(currentClip);
					if ((previousDistance >= 0) != (currentDistance >= 0)) {
						if (outputCount == clipScratch.size()) { usable = false; break; }
						
						const double t = previousDistance / (previousDistance - currentDistance);
						const auto lerp = [t](float a, float b) { return float(double(a) + (double(b) - a) * t); };
						clipScratch[outputCount] = { lerp(previousClip.x, currentClip.x), lerp(previousClip.y, currentClip.y),
							lerp(previousClip.z, currentClip.z), lerp(previousClip.w, currentClip.w) };
						keptScratch[outputCount++] = { lerp(previousPoint.x, currentPoint.x),
							lerp(previousPoint.y, currentPoint.y), lerp(previousPoint.z, currentPoint.z) };
					}
					if (currentDistance >= 0) {
						if (outputCount == clipScratch.size()) { usable = false; break; }
						clipScratch[outputCount] = currentClip;
						keptScratch[outputCount++] = currentPoint;
					}
					previousClip = currentClip;
					previousPoint = currentPoint;
					previousDistance = currentDistance;
				}
				clip = clipScratch;
				kept = keptScratch;
				keptCount = outputCount;
			}
			if (usable && keptCount >= 3) {
				points = kept;
				count = keptCount;
			}
		}
		float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
		for (std::size_t index = 0; index < count; ++index) {
			const auto clip = project(points[index], eye, viewProjection);
			if (!Finite(clip) || clip.w <= 1.0e-4f) return false;
			minX = (std::min)(minX, clip.x / clip.w);
			maxX = (std::max)(maxX, clip.x / clip.w);
			minY = (std::min)(minY, clip.y / clip.w);
			maxY = (std::max)(maxY, clip.y / clip.w);
		}
		if (maxX - minX < kEpsilon || maxY - minY < kEpsilon) return false;
		const float borderX = (std::max)((maxX - minX) * 0.04f, 0.002f);
		const float borderY = (std::max)((maxY - minY) * 0.04f, 0.002f);
		const float width = sourceSlopes.y - sourceSlopes.x;
		const float height = sourceSlopes.w - sourceSlopes.z;
		const DirectX::XMFLOAT4 fitted{
			sourceSlopes.x + (minX - borderX + 1.0f) * 0.5f * width,
			sourceSlopes.x + (maxX + borderX + 1.0f) * 0.5f * width,
			sourceSlopes.z + (minY - borderY + 1.0f) * 0.5f * height,
			sourceSlopes.z + (maxY + borderY + 1.0f) * 0.5f * height };
		if (!Finite(fitted) || fitted.y - fitted.x < kEpsilon || fitted.w - fitted.z < kEpsilon)
			return false;
		fittedSlopes = fitted;
		return true;
	}

	bool SphereClearance(const Plane& plane, const DirectX::XMFLOAT3& center,
		float radius, float minimumDistance, float& translation) noexcept
	{
		translation = 0.0f;
		if (!Finite(center) || !Finite(plane.normal) || !std::isfinite(plane.distance) ||
			!std::isfinite(radius) || radius < 0.0f || !std::isfinite(minimumDistance) || minimumDistance < 0.0f ||
			std::abs(Dot(plane.normal, plane.normal) - 1.0f) > 1.0e-3f)
			return false;
		const float amount = minimumDistance + radius - SignedDistance(plane, center);
		if (!std::isfinite(amount)) return false;
		translation = (std::max)(0.0f, amount);
		return true;
	}

	bool BuildObliqueViewProjection(
		const Plane& absoluteWorldPlane,
		float clipBias,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		DirectX::XMFLOAT4X4& outputProjection,
		DirectX::XMFLOAT4X4& outputViewProjection,
		DirectX::XMFLOAT4* outputCameraPlane,
		ObliqueProjectionDiagnostics* diagnostics) noexcept
	{
		if (diagnostics)
			*diagnostics = {};
		if (!Finite(cameraOrigin)) {
			if (diagnostics)
				diagnostics->failure = ObliqueProjectionFailure::kInvalidCameraOrigin;
			return false;
		}
		if (!Finite(clipBias)) {
			if (diagnostics)
				diagnostics->failure = ObliqueProjectionFailure::kInvalidClipBias;
			return false;
		}
		Plane relativePlane = absoluteWorldPlane;
		if (!NormalizePlane(relativePlane)) {
			if (diagnostics)
				diagnostics->failure = ObliqueProjectionFailure::kInvalidWorldPlane;
			return false;
		}

		relativePlane.distance =
			relativePlane.distance + clipBias - Dot(relativePlane.normal, cameraOrigin);

		DirectX::XMFLOAT4 cameraPlane{};
		if (!TransformPlaneToViewDetailed(relativePlane, view, cameraPlane, diagnostics) ||
			!BuildObliqueNearProjectionDetailed(projection, cameraPlane, outputProjection, diagnostics))
			return false;

		using namespace DirectX;
		const XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
		const XMMATRIX projectionMatrix = XMLoadFloat4x4(&outputProjection);
		XMStoreFloat4x4(&outputViewProjection, XMMatrixMultiply(viewMatrix, projectionMatrix));
		if (!FiniteMatrix(outputViewProjection)) {
			if (diagnostics)
				diagnostics->failure = ObliqueProjectionFailure::kInvalidViewProjection;
			return false;
		}
		if (outputCameraPlane)
			*outputCameraPlane = cameraPlane;
		if (diagnostics)
			diagnostics->failure = ObliqueProjectionFailure::kNone;
		return true;
	}
}
