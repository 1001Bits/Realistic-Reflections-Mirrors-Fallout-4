#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace VRMirrorScene
{

	inline constexpr std::size_t kMaximumRoots = 8192u;
	inline constexpr std::size_t kMaximumVisits = 32768u;

	class RootSet
	{
	public:
		enum class Result { Inserted, Duplicate, Full };
		void Clear() noexcept { entries.fill(nullptr); count = 0; }
		Result Insert(const void* value) noexcept
		{
			if (!value) return Result::Duplicate;
			auto hash = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
			hash ^= hash >> 30; hash *= 0xbf58476d1ce4e5b9ull;
			hash ^= hash >> 27; hash *= 0x94d049bb133111ebull; hash ^= hash >> 31;
			for (std::size_t probe = 0; probe < entries.size(); ++probe) {
				auto& entry = entries[(hash + probe) & (entries.size() - 1)];
				if (entry == value) return Result::Duplicate;
				if (!entry) {
					if (count == kMaximumRoots) return Result::Full;
					entry = value; ++count; return Result::Inserted;
				}
			}
			return Result::Full;
		}
	private:
		std::array<const void*, kMaximumRoots * 2> entries{};
		std::size_t count{};
	};
	inline bool PartitionContainer(const char* type) noexcept
	{
		if (!type) return false;
		const std::string_view name{ type };
		return name == "NiNode" || name == "BSMultiBoundNode";
	}

	struct Frustum
	{
		enum class Relation { Outside, Intersecting, Inside };
		std::array<std::array<float, 4>, 7> planes{};
		std::array<float, 7> planeLengths{};
		std::array<float, 3> origin{};
		float slack{};
		void Set(const float (&matrix)[4][4], float x, float y, float z, float padding = 0.0f) noexcept
		{
			origin = { x, y, z };
			slack = std::isfinite(padding) && padding > 0.0f ? padding : 0.0f;
			
			planes[6] = {}; planeLengths[6] = 0.0f;
			
			for (std::size_t row = 0; row < 4; ++row) {
				planes[0][row] = matrix[row][3] + matrix[row][0];
				planes[1][row] = matrix[row][3] - matrix[row][0];
				planes[2][row] = matrix[row][3] + matrix[row][1];
				planes[3][row] = matrix[row][3] - matrix[row][1];
				planes[4][row] = matrix[row][2];
				planes[5][row] = matrix[row][3] - matrix[row][2];
			}

			for (std::size_t index = 0; index < 6; ++index)
				PreparePlane(index);
		}
		void SetHalfSpace(float nx, float ny, float nz, float distance) noexcept
		{
			
			const auto relativeDistance = static_cast<float>(
				static_cast<double>(nx) * origin[0] + static_cast<double>(ny) * origin[1] +
				static_cast<double>(nz) * origin[2] - distance);
			planes[6] = {nx, ny, nz, relativeDistance};
			PreparePlane(6);
		}
		bool HasHalfSpace() const noexcept { return planeLengths[6] > 0.0f; }
		Relation Classify(float x, float y, float z, float radius) const noexcept
		{
			
			if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
				!std::isfinite(radius) || radius <= 1.0f)
				return Relation::Intersecting;
			x -= origin[0]; y -= origin[1]; z -= origin[2];
			bool intersecting = false, tested = false;
			for (std::size_t index = 0; index < planes.size(); ++index) {
				const float length = planeLengths[index];
				if (!(length > 0.0f)) continue;
				tested = true;
				const auto& p = planes[index];
				const float signedDistance = p[0]*x + p[1]*y + p[2]*z + p[3];
				if (!std::isfinite(signedDistance)) return Relation::Intersecting;
				const float reach = (radius + slack) * length;
				if (signedDistance < -reach) return Relation::Outside;
				intersecting = intersecting || signedDistance <= reach;
			}
			return !tested || intersecting ? Relation::Intersecting : Relation::Inside;
		}
		bool Intersects(float x, float y, float z, float radius) const noexcept
		{
			return Classify(x, y, z, radius) != Relation::Outside;
		}
	private:
		void PreparePlane(std::size_t index) noexcept
		{
			const auto& p = planes[index];
			const float length = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
			planeLengths[index] = std::isfinite(length) && std::isfinite(p[3]) && length > 0.0f ? length : 0.0f;
		}
	};

	template <class Object, class Parent, class Children, class Visible, class Descend, class Submit>
	bool SubmitWorld(Object* world, Object* player, Parent parent, Children children,
		Visible visible, Descend descend, Submit submit) noexcept
	{
		if (!world || world == player)
			return false;
		std::array<Object*, 96> branch{};
		std::size_t count = 0;
		auto* cursor = player;
		for (; cursor && cursor != world; cursor = parent(cursor)) {
			if (count == branch.size())
				return false;
			branch[count++] = cursor;
		}
		if (cursor != world)
			count = 0;  
		std::array<Object*, 96> ancestors{};
		std::size_t visited = 0;
		auto visit = [&](auto&& self, Object* object, std::size_t depth) noexcept -> bool {
			if (!object || object == player || !visible(object))
				return true;
			if (depth == ancestors.size() || ++visited > kMaximumVisits)
				return false;
			for (std::size_t index = 0; index < depth; ++index) {
				if (ancestors[index] == object)
					return false;
			}
			bool playerAncestor = false;
			for (std::size_t index = 0; index < count; ++index)
				playerAncestor = playerAncestor || branch[index] == object;
			if (object != world && !playerAncestor && !descend(object))
				return submit(object);
			ancestors[depth] = object;
			return children(object, [&](Object* child) noexcept { return self(self, child, depth + 1u); });
		};
		if (!visit(visit, world, 0))
			return false;
		return !player || submit(player);
	}
}
