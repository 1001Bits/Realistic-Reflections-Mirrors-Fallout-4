#define MIRROR_LIGHTING_SLOT b13
#include "FlatMirrorLighting.hlsli"
#include "FlatMirrorPrivateSun.hlsli"
// Native forward eye PS 0x101 material inputs, retained at the exact eye draw.
// t0 comes from the eyeball HDPT/TXST, never the separate lash material.
cbuffer NativeEyeMaterial : register(b1) { float4 EyeMaterial[8]; };
cbuffer NativeEyeGeometry : register(b2) { float4 EyeGeometry[38]; };
Texture2D<float4> EyeDiffuse : register(t0);
Texture2D<float4> EyeNormalMap : register(t1);
Texture2D<float4> EyeSmoothSpec : register(t2);
TextureCube<float4> EyeCube : register(t4);
Texture2DArray<float4> EyeSunShadow : register(t14);
SamplerState EyeSampler : register(s0);
SamplerState EyeNormalSampler : register(s1);
SamplerState EyeSmoothSpecSampler : register(s2);
SamplerState EyeCubeSampler : register(s4);
SamplerState EyeSunShadowSampler : register(s14);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 relativeWorld : TEXCOORD4;
    float3 tbnX : TEXCOORD1;
    float3 tbnY : TEXCOORD2;
    float3 tbnZ : TEXCOORD3;
    float3 viewDirection : TEXCOORD5;
    float4 vertexColor : COLOR0;
    bool frontFace : SV_IsFrontFace;
};

// Native forward PS 0x101 instructions 29..59. This is the draw's shadow atlas
// and per-geometry cascade transform, not a main-screen shadow-mask lookup.
float EyeSunVisibility(float4 relativeWorld)
{
    if (EyeGeometry[10].x != 1.0 || !all(isfinite(relativeWorld)) ||
        !all(isfinite(EyeGeometry[37]))) return 1.0;
    uint cascade = relativeWorld.w > EyeGeometry[37].x ? 1u : 0u;
    if (relativeWorld.w > EyeGeometry[37].y) cascade = 2u;
    if (relativeWorld.w > EyeGeometry[37].z) cascade = 3u;
    if (relativeWorld.w >= EyeGeometry[37].w) return 1.0;
    float4 position = float4(relativeWorld.xyz, 1.0);
    uint row = 25u + 3u * cascade;
    float2 uv = float2(dot(EyeGeometry[row], position), dot(EyeGeometry[row + 1], position));
    float depth = dot(EyeGeometry[row + 2], position);
    uint width,height,layers;
    EyeSunShadow.GetDimensions(width,height,layers);
    if (!width || !height || cascade>=layers || !all(isfinite(uv)) || !isfinite(depth) ||
        depth<0 || depth>1) return 1.0;
    float2 guard=.5/float2(width,height);
    if (any(uv<guard) || any(uv>1-guard)) return 1.0;
    float stored=EyeSunShadow.Sample(EyeSunShadowSampler, float3(uv, cascade)).x;
    return !isfinite(stored) || stored<0 || stored>1 || stored>=depth ? 1.0 : 0.0;
}

float3 EyeIllumination(float3 world, float3 normal, float sunVisibility)
{
    float4 n = float4(normal, 1);
    float3 illumination = pow(abs(float3(dot(Ambient[0], n), dot(Ambient[1], n), dot(Ambient[2], n))), 2.2);
    [loop] for (uint index = 0; index < min(ExtentLightsHistory.z, (uint)MIRROR_MAX_LIGHTS); ++index) {
        Light light = Lights[index];
        float3 toLight = light.positionRadius.xyz;
        float attenuation = sunVisibility;
        if (light.positionRadius.w > 0) {
            toLight -= world;
            float distanceSquared = dot(toLight, toLight);
            float radiusSquared = light.positionRadius.w * light.positionRadius.w;
            attenuation = saturate(1 - distanceSquared / radiusSquared);
            attenuation *= attenuation;
            attenuation *= MirrorSpotFactor(light, toLight);
        }
        illumination += light.color.rgb * (saturate(dot(normal, SafeNormalize(toLight))) * attenuation);
    }
    return illumination;
}

// Vanilla forward envmap PS (DFLight PS 0x00000101, the permutation whose input signature equals VS 0x103's output):
// normalized Blinn-Phong D=(p+2)/(2pi)*NdotH^p with p=exp2(10*smooth+1), Schlick F0=0.2, Cook-Torrance G/NdotV,
// spec=min(15, D*F*G*0.25) * pi * specMask * specScale * light * NdotL.
float3 EyeLightSpecular(float3 normal, float3 view, float3 toLight, float3 color, float specMask, float smooth)
{
    float power = exp2(10.0 * smooth + 1.0);
    float normalization = (power + 2.0) * 0.159155;
    float nv = saturate(dot(normal, view));
    float3 l = SafeNormalize(toLight);
    float3 h = SafeNormalize(l + view);
    float nl = saturate(dot(normal, l)), nh = saturate(dot(normal, h)), vh = max(saturate(dot(view, h)), 1e-4);
    float g = min(1.0, 2.0 * nh * min(nl, nv) / vh) / max(nv, 1e-4);
    float f5 = pow(1.0 - vh, 5.0);
    float fresnel = min(f5 + 0.2 * (1.0 - f5), 1.0);
    float term = min(normalization * pow(nh, power) * fresnel * g * 0.25, 15.0);
    return color * (term * 3.141593 * specMask * max(EyeGeometry[11].y, 0) * nl);
}

float3 VanillaEyeSpecular(float3 relativeWorld, float3 normal, float3 view, float specMask, float smooth, float sunVisibility)
{
    // Use the eye draw's native forward-light selection. The mirror's scene-wide
    // diffuse-light list is not this material's specular-light list.
    float3 specular = EyeLightSpecular(normal, view, EyeGeometry[0].xyz, EyeGeometry[1].rgb, specMask, smooth) * sunVisibility;
    // b2 reserves six position/radius and six color records (13..18, 19..24).
    uint count = (uint)clamp(floor(EyeGeometry[19].w), 0.0, 6.0);
    [loop] for (uint i = 0; i < count; ++i) {
        float4 light = EyeGeometry[13 + i];
        float3 toLight = light.xyz - relativeWorld;
        float attenuation = light.w > 0 ? pow(saturate(1 - dot(toLight, toLight) / (light.w * light.w)), 2.2) : 0;
        // PS 0x101 writes r11 for each point light, then selects it after the
        // loop (instructions 224..230); it does not sum every scene highlight.
        specular = EyeLightSpecular(normal, view, toLight, EyeGeometry[19 + i].rgb, specMask, smooth) * attenuation;
    }
    return specular;
}

// The late iris pass needs to clear the native wet/AO shells (about 0.03..0.09
// game units), but a fixed rasterizer bias is measured in depth-buffer units.
// At a long reflected path it used to pull the eye through the eyelids. Keep
// that existing clearance only up to 0.1 world units, regardless of distance,
// off-axis projection, or the mirror's oblique near plane.
float EyeDepth(PSInput input)
{
    // Native VS 0x102/0x103 exports clip Z in TEXCOORD4.w. Direct3D provides
    // interpolated clip W in SV_Position.w (not OpenGL's reciprocal W).
    float reciprocalW = rcp(input.position.w);
    float nativeDepth = input.relativeWorld.w * reciprocalW;
    float delta = input.position.z - nativeDepth;
    if (delta >= 0) return input.position.z;
    float4 inverseDepth = InverseViewProjection[2];
    float gradient = length(inverseDepth.xyz - input.relativeWorld.xyz * inverseDepth.w);
    const float maximumClearance = 0.1;
    float denominator = gradient + maximumClearance * inverseDepth.w;
    if (!isfinite(denominator) || denominator <= 1.e-12 || input.position.w <= 0)
        return nativeDepth;
    // For dz along this pixel's ray, world displacement is
    // |dz|*gradient/(1/clipW + dz*inverseDepth.w). Solve it for the cap.
    float minimumDelta = -maximumClearance * reciprocalW / denominator;
    return nativeDepth + max(delta, minimumDelta);
}

struct PSOutput
{
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

PSOutput main(PSInput input)
{
    PrivateSunReceiver receiver=(PrivateSunReceiver)0;
    receiver.dx=ddx(input.relativeWorld.xyz);
    receiver.dy=ddy(input.relativeWorld.xyz);
    receiver.positionError=0; // Native vertex positions are not reconstructed from D24.
    receiver.player=true; // leased only to the exact detached-player iris
    float4 authored = EyeDiffuse.Sample(EyeSampler, input.texCoord);
    authored *= input.vertexColor;
    clip(authored.a - EyeGeometry[3].x);
    float3 geometric = SafeNormalize(float3(input.tbnX.z, input.tbnY.z, input.tbnZ.z));
    float2 packedNormal = EyeNormalMap.Sample(EyeNormalSampler, input.texCoord).xy * 2 - 1;
    float3 tangentNormal = float3(packedNormal, sqrt(saturate(1 - dot(packedNormal, packedNormal))));
    float3 normal = SafeNormalize(float3(dot(input.tbnX, tangentNormal), dot(input.tbnY, tangentNormal), dot(input.tbnZ, tangentNormal)));
    if (!all(isfinite(normal)) || dot(normal, normal) < 0.25) normal = geometric;
    normal.y = input.frontFace ? normal.y : -normal.y;
    float2 specSmooth = EyeSmoothSpec.Sample(EyeSmoothSpecSampler, input.texCoord).xy;

    float specMask = specSmooth.x;
    float smooth = saturate(specSmooth.y * EyeGeometry[11].x);
    float3 world = input.relativeWorld.xyz + Eye.xyz;
    // Native VS 0x103 includes the per-geometry eye offset in TEXCOORD5.
    float3 view = SafeNormalize(input.viewDirection);
    float sunVisibility = PrivateSunVisibility(world,receiver);
    float3 illumination = EyeIllumination(world, normal, sunVisibility);

    float3 environment = 0;
    // Vanilla: R = 2(N.V)N - V, sampled at -R (FO4 cube convention), LOD = 6*(1-smoothness),
    // weight = 3*specMask*min(sqrt(saturate(smooth-0.3)),1)*scale, and the result is LIT (multiplied by the same
    // diffuse+ambient illumination as the albedo), not added as light.
    float3 reflected = 2 * dot(normal, view) * normal - view;
    float3 cube = EyeCube.SampleLevel(EyeCubeSampler, -reflected, 6.0 * (1.0 - saturate(specSmooth.y))).rgb;
    environment = cube * (3.0 * specMask * min(sqrt(saturate(specSmooth.y - 0.3)), 1.0) * max(EyeGeometry[11].y, 0) * max(EyeMaterial[2].x, 0));
    float3 color = (max(authored.rgb, 0) + environment) * illumination
        + VanillaEyeSpecular(input.relativeWorld.xyz, normal, view, specMask, smooth, sunVisibility);
    if (ResolveContract.x != MIRROR_LIGHTING_ABI || !all(isfinite(color))) discard;
    PSOutput output;
    output.color = float4(clamp(color, 0, 65504), saturate(EyeGeometry[2].z));
    output.depth = EyeDepth(input);
    return output;
}
