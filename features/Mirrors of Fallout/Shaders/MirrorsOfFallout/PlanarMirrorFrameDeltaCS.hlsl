// Diagnostic-only publication tripwire for the coherent bathroom mirror.
//
// Compares the candidate image that is about to be published against the image
// the pane currently shows, on a coarse point-sampled lattice, and keeps a small
// thumbnail of the candidate. The CPU reads the counters back asynchronously a
// few drives later; a spike logs that drive's pass census and saves the retained
// thumbnails. Nothing here influences publication: the candidate is published
// exactly as before, corrupt or not.
//
// Counters (RWByteAddressBuffer, uint):
//   0  sum of quantized luma delta (0..255 per sample)
//   4  samples whose luma delta exceeds Thresholds.x
//   8  samples that are black in the candidate but not in the committed image
//  12  samples compared
//  16  black samples in the candidate
//  20  black samples in the committed image
//  24  maximum changed samples in any 8x8 thumbnail tile
//  28  maximum newly-black samples in any 8x8 thumbnail tile

cbuffer FrameDeltaConstants : register(b0)
{
	// x = source width, y = source height, z = thumbnail width, w = thumbnail height
	uint4 Dimensions;
	// x/y = sampled source origin, z/w = sampled source extent
	uint4 Region;
	// x = changed-sample maximum RGB-channel threshold, y = black luma threshold
	float4 Thresholds;
	// Reserved for CPU-side local-tile policy; keeps this diagnostic CB explicitly versioned.
	uint4 LocalThresholds;
};

Texture2D<float4> Candidate : register(t0);
Texture2D<float4> Committed : register(t1);
RWByteAddressBuffer Counters : register(u0);
RWTexture2D<float4> Thumbnail : register(u1);

groupshared uint gSumDelta;
groupshared uint gChanged;
groupshared uint gNewlyBlack;
groupshared uint gSamples;
groupshared uint gCandidateBlack;
groupshared uint gCommittedBlack;

float Luma(float3 color)
{
	float3 safe = max(color, 0.0);
	float value = dot(safe, float3(0.299, 0.587, 0.114));
	// NaN is never "unchanged"; treat it as black so it counts.
	return (value == value) ? value : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	if (groupIndex == 0)
	{
		gSumDelta = 0;
		gChanged = 0;
		gNewlyBlack = 0;
		gSamples = 0;
		gCandidateBlack = 0;
		gCommittedBlack = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	const uint sourceWidth = max(Dimensions.x, 1u);
	const uint sourceHeight = max(Dimensions.y, 1u);
	const uint thumbWidth = max(Dimensions.z, 1u);
	const uint thumbHeight = max(Dimensions.w, 1u);
	if (threadID.x < thumbWidth && threadID.y < thumbHeight)
	{
		const uint originX = min(Region.x, sourceWidth - 1u);
		const uint originY = min(Region.y, sourceHeight - 1u);
		const uint regionWidth = max(min(Region.z, sourceWidth - originX), 1u);
		const uint regionHeight = max(min(Region.w, sourceHeight - originY), 1u);
		// Centre-sample every thumbnail cell using a rational mapping. Unlike the old
		// integer step, this covers the complete right/bottom remainder for dimensions
		// which are not exact multiples of 512.
		const uint sourceX = originX + min(
			((threadID.x * 2u + 1u) * regionWidth) / (thumbWidth * 2u), regionWidth - 1u);
		const uint sourceY = originY + min(
			((threadID.y * 2u + 1u) * regionHeight) / (thumbHeight * 2u), regionHeight - 1u);
		const int3 sourcePixel = int3(sourceX, sourceY, 0);
		const float4 candidate = Candidate.Load(sourcePixel);
		const float4 committed = Committed.Load(sourcePixel);
		const float candidateLuma = Luma(candidate.rgb);
		const float committedLuma = Luma(committed.rgb);
		const float delta = abs(candidateLuma - committedLuma);
		const float channelDelta = max(
			abs(candidate.r - committed.r),
			max(abs(candidate.g - committed.g), abs(candidate.b - committed.b)));
		const uint quantized = (uint)round(saturate(delta) * 255.0);
		const bool candidateBlack = candidateLuma < Thresholds.y;
		const bool committedBlack = committedLuma < Thresholds.y;

		InterlockedAdd(gSumDelta, quantized);
		InterlockedAdd(gSamples, 1u);
		if (channelDelta > Thresholds.x)
			InterlockedAdd(gChanged, 1u);
		if (candidateBlack && !committedBlack)
			InterlockedAdd(gNewlyBlack, 1u);
		if (candidateBlack)
			InterlockedAdd(gCandidateBlack, 1u);
		if (committedBlack)
			InterlockedAdd(gCommittedBlack, 1u);

		// Display-referred thumbnail so the PNG is readable without tooling.
		float3 display = pow(saturate(candidate.rgb), 1.0 / 2.2);
		display = (display == display) ? display : 0.0;
		Thumbnail[threadID.xy] = float4(display, 1.0);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 0)
	{
		uint ignored;
		Counters.InterlockedAdd(0, gSumDelta, ignored);
		Counters.InterlockedAdd(4, gChanged, ignored);
		Counters.InterlockedAdd(8, gNewlyBlack, ignored);
		Counters.InterlockedAdd(12, gSamples, ignored);
		Counters.InterlockedAdd(16, gCandidateBlack, ignored);
		Counters.InterlockedAdd(20, gCommittedBlack, ignored);
		Counters.InterlockedMax(24, gChanged, ignored);
		Counters.InterlockedMax(28, gNewlyBlack, ignored);
	}
}
