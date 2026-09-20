// Asynchronous publication proof for one immutable optical-mirror candidate.
// MaterialDiffuse is the current capture's authority: resolved colour can be
// overwritten later by native forward eyes, but a stale/cleared material pixel
// must never be certified merely because prior screen-space colour was bright.

Texture2D<float4> CandidateColor : register(t0);
Texture2D<float> CandidateDepth : register(t1);
Texture2D<float4> MaterialDiffuse : register(t2);
RWByteAddressBuffer ProofCounters : register(u0);

cbuffer ProofRegion : register(b0)
{
	uint4 Region; // xy = inclusive minimum, zw = exclusive maximum
};

groupshared uint GroupSampled;
groupshared uint GroupDepth;
groupshared uint GroupValid;
groupshared uint GroupMissingMaterial;
groupshared uint GroupDarkResolved;
groupshared uint GroupNonFinite;

[numthreads(16, 16, 1)]
void main(
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupID : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	if (groupIndex == 0) {
		GroupSampled = 0;
		GroupDepth = 0;
		GroupValid = 0;
		GroupMissingMaterial = 0;
		GroupDarkResolved = 0;
		GroupNonFinite = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint width = 0;
	uint height = 0;
	CandidateColor.GetDimensions(width, height);
	const uint2 pixel = Region.xy + dispatchThreadID.xy * 2u + 1u;
	if (pixel.x < min(width, Region.z) && pixel.y < min(height, Region.w)) {
		uint ignored = 0;
		InterlockedAdd(GroupSampled, 1u, ignored);
		const float depth = CandidateDepth.Load(int3(pixel, 0));
		const bool depthCovered = depth == depth && depth < 0.999999;
		if (depthCovered)
			InterlockedAdd(GroupDepth, 1u, ignored);

		const float4 current = CandidateColor.Load(int3(pixel, 0));
		const float4 material = MaterialDiffuse.Load(int3(pixel, 0));
		// Match PlanarMaterialResolveCS exactly. The MRT clear sentinel is a
		// negative float4, and a partial RGBA writer is not a complete material
		// pixel even when its RGB channels happen to look plausible.
		const bool finiteCurrent = all(current == current) &&
			all(abs(current) <= 65504.0);
		const bool finiteMaterial = all(material == material) &&
			all(abs(material) <= 65504.0);
		const bool materialWritten = finiteMaterial && all(material >= 0.0);
		// Every accepted material resolve receives the +0.012 RGB floor. Keep the
		// ordinary coverage predicate, but also census visibly near-black pixels:
		// the old <=0.006 test could never see the exact 0.012 result produced by a
		// temporarily black material and therefore certified the reported flash.
		const float currentSignal = max(current.r, max(current.g, current.b));
		const float materialSignal = max(material.r, max(material.g, material.b));
		const float currentLuminance = dot(max(current.rgb, 0.0),
			float3(0.2126, 0.7152, 0.0722));
		const bool currentHasResolvedSignal = currentSignal > 0.006;
		const bool belowResolveFloor = currentSignal < 0.009;
		const bool collapsedBrightMaterial =
			materialSignal >= 0.08 && currentSignal * 4.0 < materialSignal;
		const bool visuallyNearBlack =
			currentSignal < 0.025 && currentLuminance < 0.020;
		const bool currentValid =
			depthCovered && finiteCurrent && materialWritten && currentHasResolvedSignal &&
			!belowResolveFloor && !collapsedBrightMaterial;
		const bool currentMissingMaterial =
			depthCovered && finiteMaterial && any(material < 0.0);
		const bool currentDarkResolved =
			depthCovered && finiteCurrent && materialWritten &&
			(visuallyNearBlack || belowResolveFloor || collapsedBrightMaterial);
		if (!finiteCurrent || !finiteMaterial || !(depth == depth))
			InterlockedAdd(GroupNonFinite, 1u, ignored);
		else if (currentValid)
			InterlockedAdd(GroupValid, 1u, ignored);
		else if (currentMissingMaterial)
			InterlockedAdd(GroupMissingMaterial, 1u, ignored);
		if (currentDarkResolved)
			InterlockedAdd(GroupDarkResolved, 1u, ignored);
	}

	GroupMemoryBarrierWithGroupSync();
	if (groupIndex == 0) {
		uint ignored = 0;
		ProofCounters.InterlockedAdd(0, GroupSampled, ignored);
		ProofCounters.InterlockedAdd(4, GroupDepth, ignored);
		ProofCounters.InterlockedAdd(8, GroupValid, ignored);
		ProofCounters.InterlockedAdd(12, GroupMissingMaterial, ignored);
		ProofCounters.InterlockedAdd(16, GroupDarkResolved, ignored);
		if (GroupSampled != 0 && GroupValid * 4u >= GroupSampled) {
			ProofCounters.InterlockedAdd(20, 1u, ignored);
			const uint2 regionExtent = max(Region.zw - Region.xy, uint2(1u, 1u));
			const uint2 groupCenter = min(groupID.xy * 32u + 16u, regionExtent - 1u);
			const uint2 macro = min((groupCenter * 4u) / regionExtent, uint2(3u, 3u));
			ProofCounters.InterlockedOr(28, 1u << (macro.y * 4u + macro.x), ignored);
		}
		ProofCounters.InterlockedAdd(24, GroupNonFinite, ignored);
	}
}
