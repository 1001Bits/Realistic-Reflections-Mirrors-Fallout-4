// Fallout 4 VR final mirror presentation. VR renders both eyes into one
// side-by-side Texture2D (left eye in the left half, right eye in the right
// half). One DrawInstanced(vertexCount, 2, ...) projects the authored pane for
// both eyes and samples the matching half of the private planar target.

Texture2D<float4> MainDepth : register(t0);
Texture2D<float4> PlanarColor : register(t1);
StructuredBuffer<float2> PaneVertices : register(t0);
SamplerState MirrorSampler : register(s0);

// C++ mirror of this buffer is 432 bytes. Every member is float4-aligned:
//  0 Screen, 16 MirrorCenter, 32 MirrorNormal, 48 MirrorTangent,
// 64 MirrorBitangent, 80 OutputEncoding, 96 MainEye[2],
// 128 MainViewProjection[2], 256 ReflectedEye[2],
// 288 ReflectedViewProjection[2], 416 MainDepthRange.
cbuffer StereoMirrorData : register(b0)
{
    float4 Screen;          // xy = inverse total size; zw = total side-by-side size
    float4 MirrorCenter;    // xyz = absolute pane center; w = diagnostic mode
    float4 MirrorNormal;    // xyz = unit normal; w = half thickness
    float4 MirrorTangent;   // xyz = unit tangent; w = half width
    float4 MirrorBitangent; // xyz = unit bitangent; w = half height
    float4 OutputEncoding;  // x = -1 linear HDR, 0 sRGB RTV, 1 encoded SDR; y = exposure; z = ellipse; w = depth margin
    float4 MainEye[2];
    row_major float4x4 MainViewProjection[2];
    float4 ReflectedEye[2];
    row_major float4x4 ReflectedViewProjection[2];
    float4 MainDepthRange; // xy = native main viewport MinDepth/MaxDepth; z = first table vertex of the route
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 paneHitRelative : TEXCOORD0;
    float2 paneCoordinates : TEXCOORD1;
    nointerpolation uint eye : EYEINDEX;
    float2 eyeClip : SV_ClipDistance0;
};

VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    const uint eye = instanceID & 1u;
    // The silhouette range start travels in MainDepthRange.z; the draw starts at vertex 0 (see the flat VS).
    const float2 corner = PaneVertices[vertexID + (uint)(MainDepthRange.z + 0.5)];
    const float3 paneWorld = MirrorCenter.xyz +
        MirrorTangent.xyz * (corner.x * MirrorTangent.w) +
        MirrorBitangent.xyz * (corner.y * MirrorBitangent.w);
    const float3 paneRelative = paneWorld - MainEye[eye].xyz;
    float4 clip = mul(float4(paneRelative, 1.0), MainViewProjection[eye]);
    // Preserve each eye's original horizontal clip planes before packing X into the shared viewport.
    // Without these, an offscreen left-eye triangle can rasterize in the right eye, and vice versa.
    output.eyeClip = float2(clip.x + clip.w, clip.w - clip.x);

    // Eye-local NDC [-1,+1] occupies one half of the shared render target.
    clip.x = 0.5 * (clip.x + (eye != 0u ? clip.w : -clip.w));
    output.position = clip;
    output.paneHitRelative = paneRelative;
    output.paneCoordinates = corner;
    output.eye = eye;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const uint eye = input.eye & 1u;
    const uint2 pixel = uint2(input.position.xy);
    const uint mode = (uint)(MirrorCenter.w + 0.5);
    if (pixel.x >= (uint)Screen.z || pixel.y >= (uint)Screen.w || mode == 0u)
        discard;
    if ((pixel.x >= (uint)Screen.z / 2u) != (eye != 0u))
        discard;
    if (OutputEncoding.z > 0.5 && dot(input.paneCoordinates, input.paneCoordinates) > 1.0)
        discard;
    if (mode <= 2u)
        return float4(0.0, 1.0, 0.0, 1.0);

    const float2 framebufferUV = (float2(pixel) + 0.5) * Screen.xy;
    const float3 paneWorld = MainEye[eye].xyz + input.paneHitRelative;

    if (mode >= 4u)
    {
        uint depthWidth = 0;
        uint depthHeight = 0;
        MainDepth.GetDimensions(depthWidth, depthHeight);
        if (depthWidth == 0u || depthHeight == 0u)
            discard;
        const uint2 depthPixel = min(
            uint2(framebufferUV * float2(depthWidth, depthHeight)),
            uint2(depthWidth - 1u, depthHeight - 1u));
        const float depth = MainDepth.Load(int3(depthPixel, 0)).x;
        const float paneDistance = length(input.paneHitRelative);
        if (paneDistance <= 1.0e-4)
            discard;

        const float3 paneRay = input.paneHitRelative / paneDistance;
        const float rayNormalAlignment = max(abs(dot(paneRay, MirrorNormal.xyz)), 1.0e-4);
        // Keep the tolerance in front of the pane even when the headset is closer than
        // its authored thickness/margin. Rejecting that case made the whole mirror flash off.
        const float foregroundTolerance = min(max(
            (MirrorNormal.w + OutputEncoding.w) / rayNormalAlignment, 2.0), 0.5 * paneDistance);

        const float3 biasedPaneRelative = input.paneHitRelative *
            ((paneDistance - foregroundTolerance) / paneDistance);
        const float4 paneClip = mul(
            float4(biasedPaneRelative, 1.0), MainViewProjection[eye]);
        if (!(paneClip.w > 1.0e-6))
            discard;
        const float paneCanonicalDepth = paneClip.z / paneClip.w;
        // Native CalculateCameraViewProj produces D3D [0,1] depth. Convert with
        // the captured MAIN viewport, not an assumed offset or our private viewport.
        const float paneRawDepth = lerp(MainDepthRange.x, MainDepthRange.y, saturate(paneCanonicalDepth));
        const float depthGuard = 2.0 / 16777215.0;
        if (depth + depthGuard < paneRawDepth)
            discard;
    }

    const float3 planarRay = paneWorld - ReflectedEye[eye].xyz;
    const float4 reflectedClip = mul(
        float4(planarRay, 0.0), ReflectedViewProjection[eye]);
    if (!(reflectedClip.w > 1.0e-5))
        discard;
    const float2 reflectedNdc = reflectedClip.xy / reflectedClip.w;
    if (!all(abs(reflectedNdc) <= float2(1.0, 1.0)))
        discard;
    const float2 eyeUV = float2(
        reflectedNdc.x * 0.5 + 0.5,
        0.5 - reflectedNdc.y * 0.5);
    const float2 planarUV = float2((eyeUV.x + (float)eye) * 0.5, eyeUV.y);
    uint captureWidth, captureHeight;
    PlanarColor.GetDimensions(captureWidth, captureHeight);
    const float2 texelGuard = 0.5 / float2(captureWidth, captureHeight);
    const float2 safeUV = clamp(planarUV,
        float2((float)eye * 0.5 + texelGuard.x, texelGuard.y),
        float2((float)(eye + 1u) * 0.5 - texelGuard.x, 1.0 - texelGuard.y));
    float3 mirrorColor = max(
        PlanarColor.SampleLevel(MirrorSampler, safeUV, 0.0).rgb,
        0.0) * max(OutputEncoding.y, 0.0);
    // Render_PreUI still owns the linear HDR scene. Fallout applies exposure and tone mapping afterwards.
    // Gamma/auto-exposure here raised dark pixels and compressed highlights a second time, washing out the pane.
    if (OutputEncoding.x < 0.0)
        return float4(mirrorColor, 1.0);
    mirrorColor = saturate(mirrorColor / (1.0 + mirrorColor));
    if (OutputEncoding.x > 0.5)
        mirrorColor = pow(mirrorColor, 1.0 / 2.2);
    return float4(mirrorColor, 1.0);
}
