// Player-only material resolve for optical mirrors.
//
// Phase A left the complete mode-0x18 material-resolved environment in EnvironmentColor/EnvironmentDepth.
// Phase B submitted only the detached player into Fallout's native mode-0x18 MRT layout:
//   o0 = diffuse or fully-lit forward color, o1 = encoded normal + coverage, o2 = material properties.
// EnvironmentColor is an immutable snapshot and ResolvedColor is a distinct UAV, so reflected lookups never race
// another compute thread writing the same output image.

Texture2D<float4> PlayerDiffuse : register(t0);
Texture2D<float4> PlayerNormalCoverage : register(t1);
Texture2D<float4> PlayerMaterialProperties : register(t2);
Texture2D<float> PlayerDepth : register(t3);
Texture2D<float> EnvironmentDepth : register(t4);
Texture2D<float4> EnvironmentColor : register(t5);
RWTexture2D<float4> ResolvedColor : register(u0);

static const float DepthBias = 2.0 / 16777215.0;
static const float HairMaterialType = 1.0;

float3 DecodeDFLightNormal(float2 encoded)
{
	// Exact package/Shaders/DFLight.hlsl decode used by the shipped deferred-light consumer.
	const float2 f = encoded * 4.0 - 2.0;
	const float d = dot(f, f);
	const float2 auxiliary = -d * float2(0.25, 0.5) + 1.0;
	return normalize(float3(sqrt(saturate(auxiliary.x)) * f, -auxiliary.y));
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	uint width = 0;
	uint height = 0;
	ResolvedColor.GetDimensions(width, height);
	if (threadID.x >= width || threadID.y >= height)
		return;

	const int3 pixel = int3(threadID.xy, 0);
	const float playerDepth = PlayerDepth.Load(pixel);
	if (!(playerDepth < 0.999999))
		return;
	const float environmentDepth = EnvironmentDepth.Load(pixel);
	if (environmentDepth < 0.999999 && playerDepth > environmentDepth + DepthBias)
		return;

	const float4 nativeColor = PlayerDiffuse.Load(pixel);
	const float4 normalCoverage = PlayerNormalCoverage.Load(pixel);
	const float4 properties = PlayerMaterialProperties.Load(pixel);
	if (any(nativeColor < 0.0) || any(nativeColor != nativeColor) ||
		any(normalCoverage != normalCoverage) ||
		any(properties != properties) || any(abs(nativeColor) > 65504.0) ||
		any(abs(normalCoverage) > 65504.0) || any(abs(properties) > 65504.0))
		return;

	// The MRT2 clear sentinel proves no deferred material payload was written. Fully forward-lit techniques (including
	// the shipped player hair PS 0000001F) write only o0; preserve that native result exactly. Native material type 1
	// is hair and is also kept on this path even if another stage happened to populate auxiliary targets.
	const bool auxiliaryAbsent = properties.w < -0.5;
	const bool nativeHair = abs(properties.w * 255.0 - HairMaterialType) < 0.25;
	if (auxiliaryAbsent || nativeHair) {
		ResolvedColor[threadID.xy] = nativeColor;
		return;
	}

	const float3 normal = DecodeDFLightNormal(normalCoverage.xy);
	const float2 uv = (float2(threadID.xy) + 0.5) / float2(width, height);
	const float2 viewXY = uv * 2.0 - 1.0;
	const float3 viewDirection = normalize(float3(viewXY.x, -viewXY.y, 1.0));
	const float3 reflected = reflect(viewDirection, normal);
	const float2 reflectedUV = saturate(float2(0.5, 0.5) + reflected.xy * 0.5);
	const int2 reflectedPixel = min(
		int2(reflectedUV * float2(width, height)), int2(width - 1, height - 1));

	// Shipped DFLight material semantics: x is glossiness and y is the specular/environment mask. Coverage is MRT1.a
	// and deliberately never participates in this response. This is an environment-mask approximation, not a guessed
	// metalness classifier; the native property controls a bounded Fresnel sample of the immutable lit scene.
	const float glossiness = saturate(properties.x);
	const float environmentMask = saturate(properties.y);
	// Fallout's shipped non-hair DFLight BRDF uses a 0.2 base reflectance, not the generic dielectric 0.04. Keeping
	// that native baseline is essential for dark authored metals whose diffuse term intentionally carries little energy.
	const float fresnel = 0.20 + 0.80 * pow(
		1.0 - saturate(abs(dot(normal, -viewDirection))), 5.0);
	const float reflectionWeight = environmentMask * lerp(0.20, 1.0, glossiness) * fresnel;
	float3 reflectedScene = 0.0;
	if (reflectionWeight > 0.0) {
		const float4 reflectedEnvironment = EnvironmentColor.Load(int3(reflectedPixel, 0));
		if (any(reflectedEnvironment != reflectedEnvironment) ||
			any(abs(reflectedEnvironment) > 65504.0))
			return;
		reflectedScene = max(reflectedEnvironment.rgb, 0.0);
	}

	// Phase A is the unobstructed room. Its texel behind a player pixel is neither incident illumination nor player
	// transparency, so it must never modulate the deferred diffuse term. Likewise, the private material prefix has no
	// complete placed-light/shadow resolve: applying a per-channel ambient ratio here changed authored shirt and skin
	// colours into hard grey/white bands. Preserve Fallout's captured linear MRT0 at unit gain. Only MRT2.y may admit
	// the explicit bounded environment response below; forward-only hair remains byte-exact through the branch above.
	const float3 color = min(
		max(nativeColor.rgb, 0.0) + reflectedScene * reflectionWeight,
		32.0);
	ResolvedColor[threadID.xy] = float4(color, nativeColor.a);
}
