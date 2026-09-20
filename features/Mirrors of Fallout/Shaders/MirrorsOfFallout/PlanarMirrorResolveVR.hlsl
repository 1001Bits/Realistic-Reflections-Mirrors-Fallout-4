// Independent stereo resolve for Fallout 4 VR's native DFPrepass MRT layout.
// All inputs are from THIS reflected camera. Main-view color and old mirror frames are never sampled.
struct Light { float4 positionRadius; float4 color; };
cbuffer MirrorLighting : register(b0)
{
    row_major float4x4 InverseViewProjection[2];
    row_major float4x4 NormalToWorld;
    float4 EyeOrigin[2];
    float4 AmbientRows[3];
    uint4 ExtentAndLights;
    Light Lights[16];
};
Texture2D<float4> Diffuse : register(t0);
Texture2D<float4> Normal : register(t1);
Texture2D<float4> Flags : register(t2);
Texture2D<float> Depth : register(t3);
Texture2D<float4> Specular : register(t4);
Texture2D<float4> Emissive : register(t5);
RWTexture2D<float4> Color : register(u0);

float3 DecodeNormal(float2 encoded)
{
    // Native DFPrepass: n.xy / sqrt(8 - 8*n.z) + .5. This is NOT an RGB color.
    float2 xy = encoded * 4.0 - 2.0;
    float squared = min(dot(xy, xy), 4.0);
    return float3(xy * sqrt(max(1.0 - squared * .25, 0.0)), squared * .5 - 1.0);
}

float3 SafeNormalize(float3 value) { return value * rsqrt(max(dot(value, value), 1.e-12)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint width = ExtentAndLights.x * 2u;
    if (id.x >= width || id.y >= ExtentAndLights.y) return;
    const int3 pixel = int3(id.xy, 0);
    const float depth = Depth.Load(pixel);
    const float4 diffuse = Diffuse.Load(pixel);
    // A missing diffuse writer cannot be repaired with the player's screen or a stale face.
    if (!(depth < 1.0) || !all(isfinite(diffuse)) || any(diffuse.rgb < 0.0)) {
        Color[id.xy] = float4(0, 0, 0, 1);
        return;
    }
    const uint eye = id.x >= ExtentAndLights.x ? 1u : 0u;
    const float2 uv = (float2(id.x - eye * ExtentAndLights.x, id.y) + .5) /
        float2(ExtentAndLights.xy);
    const float4 position = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), InverseViewProjection[eye]);
    if (!all(isfinite(position)) || abs(position.w) < 1.e-7) {
        Color[id.xy] = float4(0, 0, 0, 1);
        return;
    }
    const float3 relative = position.xyz / position.w;
    const float3 world = relative + EyeOrigin[eye].xyz;
    const float4 encoded = Normal.Load(pixel);
    const float3 normalVS = all(isfinite(encoded.xy)) ? DecodeNormal(encoded.xy) : float3(0, 0, -1);
    const float3 normal = SafeNormalize(mul(float4(normalVS, 0), NormalToWorld).xyz);
    const float4 n = float4(normal, 1);
    // Same published directional-ambient transform and gamma decode consumed by native DFLight.
    const float3 ambient = pow(abs(float3(dot(AmbientRows[0], n), dot(AmbientRows[1], n),
        dot(AmbientRows[2], n))), 2.2);
    float3 illumination = ambient;
    float3 highlight = 0;
    const float3 view = SafeNormalize(-relative);
    const float4 specular = Specular.Load(pixel);
    [loop] for (uint index = 0; index < min(ExtentAndLights.z, 16u); ++index) {
        const Light light = Lights[index];
        float3 toLight = light.positionRadius.xyz;
        float attenuation = 1.0;
        if (light.positionRadius.w > 0.0) {
            toLight -= world;
            const float distanceSquared = dot(toLight, toLight);
            if (distanceSquared >= light.positionRadius.w * light.positionRadius.w) continue;
            attenuation = saturate(1.0 - distanceSquared / (light.positionRadius.w * light.positionRadius.w));
            attenuation *= attenuation;
        }
        const float3 direction = SafeNormalize(toLight);
        const float lambert = saturate(dot(normal, direction));
        illumination += light.color.rgb * (lambert * attenuation);
        // Bounded native gloss/strength response from MRT3, separate from the flags in MRT2.
        const float gloss = max(2.0, saturate(specular.x) * 128.0);
        const float spec = pow(saturate(dot(normal, SafeNormalize(direction + view))), gloss);
        highlight += light.color.rgb * (spec * saturate(specular.y) * lambert * attenuation);
    }
    const float3 emissive = max(Emissive.Load(pixel).rgb, 0);
    const float3 resolved = max(diffuse.rgb, 0) * illumination + highlight + emissive;
    Color[id.xy] = float4(all(isfinite(resolved)) ? clamp(resolved, 0.0, 65504.0) : 0.0, 1.0);
}
