// Merge a complete Fallout-native lit player result over a retained forward-lit reflected environment.
//
// PlayerColor is the output of BSShaderUtil::RenderSceneDeferred, not a private G-buffer approximation. PlayerDepth
// and EnvironmentDepth were rendered from the same pinned reflected camera/projection. Invalid, clear, or occluded
// player pixels leave the resident environment byte-for-byte untouched.

Texture2D<float4> PlayerColor : register(t0);
Texture2D<float> PlayerDepth : register(t1);
Texture2D<float> EnvironmentDepth : register(t2);
RWTexture2D<float4> ResolvedColor : register(u0);

static const float DepthBias = 2.0 / 16777215.0;

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	uint width = 0;
	uint height = 0;
	ResolvedColor.GetDimensions(width, height);
	if (threadID.x >= width || threadID.y >= height)
		return;

	uint2 colorDimensions = 0;
	uint2 playerDepthDimensions = 0;
	uint2 environmentDepthDimensions = 0;
	PlayerColor.GetDimensions(colorDimensions.x, colorDimensions.y);
	PlayerDepth.GetDimensions(playerDepthDimensions.x, playerDepthDimensions.y);
	EnvironmentDepth.GetDimensions(environmentDepthDimensions.x, environmentDepthDimensions.y);
	if (any(colorDimensions == 0) || any(playerDepthDimensions < colorDimensions) ||
		any(environmentDepthDimensions != uint2(width, height)))
		return;

	// Map the planar target's pixel center independently into each native resource. Fallout can render the player
	// prepass into a full-resolution depth surface while its deferred color target has the smaller active extent
	// (measured 1920x1080 depth versus 876x700 color). Reusing the color texel as a depth texel sampled only the
	// top-left portion of that surface and displaced/erased the player mask.
	const uint2 colorPixel = min(
		((threadID.xy * 2u + 1u) * colorDimensions) / (uint2(width, height) * 2u),
		colorDimensions - 1u);
	const uint2 depthPixel = min(
		((threadID.xy * 2u + 1u) * playerDepthDimensions) / (uint2(width, height) * 2u),
		playerDepthDimensions - 1u);
	const int3 environmentPixel = int3(threadID.xy, 0);
	const float playerDepth = PlayerDepth.Load(int3(depthPixel, 0));
	if (!(playerDepth >= 0.0 && playerDepth < 1.0))
		return;
	const float environmentDepth = EnvironmentDepth.Load(environmentPixel);
	if (environmentDepth < 1.0 && playerDepth > environmentDepth + DepthBias)
		return;

	const float4 player = PlayerColor.Load(int3(colorPixel, 0));
	if (any(player.rgb != player.rgb) || any(abs(player.rgb) > 65504.0))
		return;
	const float environmentAlpha = ResolvedColor[threadID.xy].a;
	ResolvedColor[threadID.xy] = float4(player.rgb, environmentAlpha);
}
