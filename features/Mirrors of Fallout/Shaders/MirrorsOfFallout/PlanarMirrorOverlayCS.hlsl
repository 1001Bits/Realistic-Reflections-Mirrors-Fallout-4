// Optical mirror composite. Normal panes enter the linear HDR world target
// before native transparent effects and imagespace. Explicit diagnostics use
// the late framebuffer before menus. Both project the exact authored pane.

Texture2D<float4> MainDepth : register(t0);
Texture2D<float4> PlanarColor : register(t1);
// Capture depth published with PlanarColor (unbound = zero dimensions = pane-surface motion only).
Texture2D<float> PlanarDepth : register(t2);
// Room reuse: the player captured alone from the current reflected eye (unbound = no player layer).
Texture2D<float4> LayerColor : register(t3);
Texture2D<float> LayerDepth : register(t4);
StructuredBuffer<float2> PaneVertices : register(t0);
SamplerState MirrorSampler : register(s0);

cbuffer CameraData : register(b0)
{
    float4 Frame[41];
};

cbuffer MirrorData : register(b1)
{
    float4 Screen;          // xy = inverse output size, zw = output size
    float4 MirrorCenter;    // xyz = absolute pane center, w = diagnostic mode (0..4)
    float4 MirrorNormal;    // xyz = unit normal, w = half thickness
    float4 MirrorTangent;   // xyz = unit tangent, w = half width
    float4 MirrorBitangent; // xyz = unit bitangent, w = half height
    float4 OutputEncoding;  // x < 0 = HDR, otherwise manual RGB encode; y exposure, z ellipse, w depth margin
    float4 ReflectedEye;    // xyz = absolute eye used to render PlanarColor; w = first table vertex of the route
    row_major float4x4 ReflectedViewProjection;
    float4 PreviousReflectedEye;  // xyz = reflected eye of the capture this pane showed on the previous frame
    row_major float4x4 LayerViewProjection; // room reuse: camera of the player layer
    float4 LayerEye;              // xyz = its reflected eye; w = 1 when PlanarColor is the room without the player
};

// Previous render frame's main camera (same cb12 layout). Unbound = zero rows = no pane velocity written.
cbuffer PreviousCameraData : register(b2)
{
    float4 PreviousFrame[41];
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 paneHitRelative : TEXCOORD0;
    float2 paneCoordinates : TEXCOORD1;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // C++ selects either the shared six-vertex rectangle/ellipse or an exact
    // authored triangle range inside one immutable structured buffer. The range
    // start travels in ReflectedEye.w and every draw starts at vertex 0: on the
    // presentation path StartVertexLocation did not reach SV_VertexID (masked
    // panes drew the table prefix = a full pane), so the offset is added here.
    float2 corner = PaneVertices[vertexID + (uint)(ReflectedEye.w + 0.5)];
    output.paneCoordinates = corner;

    // c8-c11 are Fallout's forward main-camera ViewProj rows and consume
    // camera-relative world positions. Derive both projection and pane position
    // from the same snapshotted cb12; mixing c35 with a separately cached CPU eye
    // can pair two different camera rebuilds around the private reflection pass.
    float3 centerRelative = MirrorCenter.xyz - Frame[35].xyz;
    output.paneHitRelative = centerRelative +
        MirrorTangent.xyz * (corner.x * MirrorTangent.w) +
        MirrorBitangent.xyz * (corner.y * MirrorBitangent.w);
    float4 panePosition = float4(output.paneHitRelative, 1.0);
    output.position = float4(
        dot(Frame[8], panePosition),
        dot(Frame[9], panePosition),
        dot(Frame[10], panePosition),
        dot(Frame[11], panePosition));
    return output;
}

struct PSOutput
{
    float4 color : SV_Target0;
    // Fallout's TAA motion convention (previousUV - currentUV). Alpha 0 = keep the engine's own velocity;
    // the composite's RT1 blend is source-alpha.
    float4 motion : SV_Target1;
};

PSOutput Emit(float4 color, float4 motion)
{
    PSOutput output;
    output.color = color;
    output.motion = motion;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    uint2 pixel = uint2(input.position.xy);
    uint mode = (uint)(MirrorCenter.w + 0.5);
    if (pixel.x >= (uint)Screen.z || pixel.y >= (uint)Screen.w || mode == 0)
        discard;
    if (OutputEncoding.z > 0.5 && dot(input.paneCoordinates, input.paneCoordinates) > 1.0)
        discard;

    // Modes 1 and 2 painted the pane solid green for a debug menu that is no longer part of the product.
    // Nothing selects them any more - PollMirrorDepthRejectionTrigger only ever stores 3 or 4, and the
    // visibility query uses 5 - so a reachable full-green emit is a hazard and not a feature. Green models
    // on loading screens were reported 2026-09-15; whatever paints them, it is not going to be this.
    if (mode == 1)
        discard;

    // Hardware rasterization is the pane classifier. The ordinary interpolant
    // gives the exact perspective-correct camera-relative hit Q for this pixel.
    if (mode == 2)
        discard;

    float2 uv = (float2(pixel) + 0.5) * Screen.xy;

    // Motion-vector rim (2026-09-14, DLSS: "the line where the pane meets the frame shimmers"; gone with the
    // pane motion vectors off). At the pane silhouette and along any foreground edge (the frame) the pane's
    // content velocity meets the frame's own velocity on jittered, alternating pixels, and the temporal filter
    // rejects history there every frame. Within ~1.5 px of the silhouette, and wherever a foreground object
    // covers a neighbour within 2 px, the motion output keeps the engine velocity (alpha 0).
    float motionRim = 1.0;
    {
        float2 rimCoordinates = input.paneCoordinates;
        float rimDistance;
        float rimPixels;
        if (OutputEncoding.z > 0.5)
        {
            float radius = length(rimCoordinates);
            rimDistance = 1.0 - radius;
            rimPixels = max(fwidth(radius), 1.0e-5);
        }
        else
        {
            float2 edge = 1.0 - abs(rimCoordinates);
            float2 edgePixels = max(fwidth(rimCoordinates), 1.0e-5);
            rimDistance = min(edge.x / edgePixels.x, edge.y / edgePixels.y);
            rimPixels = 1.0;
        }
        if (rimDistance < 1.5 * rimPixels)
            motionRim = 0.0;
    }

    // Mode 3 skips foreground-depth rejection. Mode 4 enables it.
    if (mode >= 4)
    {
        uint depthWidth = 0;
        uint depthHeight = 0;
        MainDepth.GetDimensions(depthWidth, depthHeight);
        if (depthWidth == 0 || depthHeight == 0)
            discard;
        // The main depth snapshot shares the scene's pixel grid, so a viewport pixel indexes it one to
        // one. Scaling the viewport's normalized uv by the TEXTURE size instead was correct only while
        // the two matched: under an upscaler Fallout renders a smaller viewport into full-size targets
        // (1280x720 into 1920x1080 with DLSS, 2026-09-15), and the 1.5x stretch moved every depth sample
        // outward from the origin. The player's silhouette then rejected pane pixels it does not cover -
        // a head-shaped hole in the reflection - while over the head itself the lookup ran off the
        // rendered region into cleared depth and the pane drew straight over him.
        uint2 depthPixel = min(pixel, uint2(depthWidth - 1, depthHeight - 1));
        float depth = MainDepth.Load(int3(depthPixel, 0)).x;
        float paneDistance = length(input.paneHitRelative);
        if (paneDistance <= 1.0e-4)
            discard;

        float3 paneRay = input.paneHitRelative / paneDistance;
        float rayNormalAlignment = max(abs(dot(paneRay, MirrorNormal.xyz)), 1.0e-4);
        float foregroundTolerance = max(
            (MirrorNormal.w + OutputEncoding.w) / rayNormalAlignment,
            2.0);
        if (foregroundTolerance >= paneDistance - 1.0e-4)
            discard;

        // Compare in Fallout's packed depth domain instead of reconstructing positions.
        // World depth occupies [.01,1], while first-person depth occupies [0,.01]; a
        // single LESS comparison therefore preserves both kinds of foreground geometry.
        // Move the pane threshold toward the eye by the OBB's ray-projected half
        // thickness plus any registry-specific fixture clearance. The latter keeps a
        // sink mirror's deep frame from punching holes without changing other panes.
        float3 biasedPaneHitRelative = input.paneHitRelative *
            ((paneDistance - foregroundTolerance) / paneDistance);
        float4 biasedPanePosition = float4(biasedPaneHitRelative, 1.0);
        float4 paneClip = float4(
            dot(Frame[8], biasedPanePosition),
            dot(Frame[9], biasedPanePosition),
            dot(Frame[10], biasedPanePosition),
            dot(Frame[11], biasedPanePosition));
        if (!(paneClip.w > 1.0e-6))
            discard;
        float paneCanonicalDepth = paneClip.z / paneClip.w;
        float paneRawDepth = clamp((paneCanonicalDepth + 0.01) / 1.01, 0.01, 1.0);
        // Mode 6 = the one-bounce pass inside a capture: the capture's depth is canonical (0..1 viewport), only
        // the main view packs world depth into [0.01,1] and first-person depth into [0,0.01].
        float compareDepth = (mode == 6) ? paneCanonicalDepth : paneRawDepth;
        const float depthGuard = 2.0 / 16777215.0;
        if (depth + depthGuard < compareDepth)
            discard;
        // Foreground boundary: a neighbour within 2 px that the frame (or any nearer object) covers. It must be
        // nearer than the pane by clearly more than the pane's own thickness tolerance: seams in the mirror
        // mesh sit inside that band and drew a blinking cross over the pane (16:1x) when they counted.
        float4 unbiasedPaneClip = float4(
            dot(Frame[8], float4(input.paneHitRelative, 1.0)),
            dot(Frame[9], float4(input.paneHitRelative, 1.0)),
            dot(Frame[10], float4(input.paneHitRelative, 1.0)),
            dot(Frame[11], float4(input.paneHitRelative, 1.0)));
        float unbiasedCanonical = unbiasedPaneClip.z / max(unbiasedPaneClip.w, 1.0e-6);
        float unbiasedPaneRawDepth = clamp((unbiasedCanonical + 0.01) / 1.01, 0.01, 1.0);
        float unbiasedCompare = (mode == 6) ? unbiasedCanonical : unbiasedPaneRawDepth;
        float toleranceBand = max(unbiasedCompare - compareDepth, depthGuard);
        int2 depthLast = int2(depthWidth - 1, depthHeight - 1);
        int2 centre = int2(depthPixel);
        float neighbourDepth = min(
            min(MainDepth.Load(int3(max(centre.x - 2, 0), centre.y, 0)).x,
                MainDepth.Load(int3(min(centre.x + 2, depthLast.x), centre.y, 0)).x),
            min(MainDepth.Load(int3(centre.x, max(centre.y - 2, 0), 0)).x,
                MainDepth.Load(int3(centre.x, min(centre.y + 2, depthLast.y), 0)).x));
        if (neighbourDepth + 4.0 * toleranceBand < compareDepth)
            motionRim = 0.0;
    }

    // Internal visibility-only query: keep the exact silhouette and foreground
    // test, independent of a reflection's UV coverage or whether it exists yet.
    if (mode == 5)
        return Emit(float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0));

    // Project the exact pane hit Q through the same reflected camera that
    // produced the 4096x4096 planar image. This bypasses the intermediate
    // 512x512 cube conversion while retaining its proven orientation math.
    float3 paneWorld = Frame[35].xyz + input.paneHitRelative;
    float3 planarRay = paneWorld - ReflectedEye.xyz;
    // This is a camera-relative POSITION. A zero w loses the projection's
    // near/oblique depth offset. Keep this paired with the capture camera.
    float4 reflectedClip = mul(float4(planarRay, 1.0), ReflectedViewProjection);
    if (!(reflectedClip.w > 1.0e-5))
        discard;
    float2 reflectedNdc = reflectedClip.xy / reflectedClip.w;
    if (!all(abs(reflectedNdc) <= float2(1.0, 1.0)))
        discard;
    float2 reflectedUV = float2(
        reflectedNdc.x * 0.5 + 0.5,
        0.5 - reflectedNdc.y * 0.5);
    // Present one coherent captured image. Depth warping old silhouettes stretches actors
    // over newly visible background, which a single captured depth layer cannot supply.
    // Mips and anisotropic filtering keep the projected pane stable under minification.
    float2 captureFootprintDx = ddx(reflectedUV);
    float2 captureFootprintDy = ddy(reflectedUV);

    // Room reuse (Data\dynref_roomreuse): PlanarColor is the room captured WITHOUT the player, possibly a few
    // frames old, and LayerColor/LayerDepth hold the player alone, captured from the current reflected eye.
    // The room carries no actor silhouette to stretch, so it is re-projected to the current eye by marching its
    // depth; the player is looked up directly and wins wherever it is nearer than the room.
    bool layered = LayerEye.w > 0.5;
    float3 eyeNow = Frame[35].xyz;
    float3 currentReflectedEye = eyeNow - 2.0 * dot(eyeNow - MirrorCenter.xyz, MirrorNormal.xyz) * MirrorNormal.xyz;
    float3 marchDirection = float3(0.0, 0.0, 0.0);
    uint roomHit = 0;            // 0 = plain sample, 1 = finite hit roomDistance beyond the pane, 2 = far
    float roomDistance = -1.0;
    uint roomDepthWidth = 0;
    uint roomDepthHeight = 0;
    PlanarDepth.GetDimensions(roomDepthWidth, roomDepthHeight);
    if (layered && roomDepthWidth != 0 && roomDepthHeight != 0)
    {
        float3 toPane = paneWorld - currentReflectedEye;
        float toPaneLength = length(toPane);
        float3 eyeDelta = currentReflectedEye - ReflectedEye.xyz;
        if (toPaneLength > 1.0e-4)
            marchDirection = toPane / toPaneLength;
        if (dot(eyeDelta, eyeDelta) > 1.0e-4 && toPaneLength > 1.0e-4)
        {
            // Points on the current ray project into the room capture affinely in t (clipBase + clipStep * t).
            float4 clipBase = reflectedClip;
            float4 clipStep = mul(float4(marchDirection, 0.0), ReflectedViewProjection);
            float2 depthScale = float2(roomDepthWidth, roomDepthHeight);
            uint2 depthLast = uint2(roomDepthWidth - 1, roomDepthHeight - 1);
            const float depthBias = 2.0e-6;
            float tNear = 0.0;
            float tFar = 4.0;
            float2 lastInsideUV = reflectedUV;
            bool lastInsideValid = false;
            bool hit = false;
            bool leftFrustum = false;
            [loop]
            for (uint step = 0; step < 28 && !hit; ++step)
            {
                float4 clip = clipBase + clipStep * tFar;
                if (!(clip.w > 1.0e-5))
                {
                    leftFrustum = true;
                    break;
                }
                float2 ndc = clip.xy / clip.w;
                if (any(abs(ndc) > 1.0))
                {
                    leftFrustum = true;
                    break;
                }
                float2 marchUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                lastInsideUV = marchUV;
                lastInsideValid = true;
                uint2 depthPixel = min(uint2(marchUV * depthScale), depthLast);
                float stored = PlanarDepth.Load(int3(depthPixel, 0));
                if (stored <= clip.z / clip.w + depthBias)
                {
                    hit = true;
                    break;
                }
                tNear = tFar;
                tFar *= 1.35;
            }
            if (hit)
            {
                [unroll]
                for (uint refine = 0; refine < 6; ++refine)
                {
                    float tMid = 0.5 * (tNear + tFar);
                    float4 clip = clipBase + clipStep * tMid;
                    float2 ndc = clip.xy / max(clip.w, 1.0e-5);
                    float2 marchUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                    uint2 depthPixel = min(uint2(saturate(marchUV) * depthScale), depthLast);
                    float stored = PlanarDepth.Load(int3(depthPixel, 0));
                    if (stored <= clip.z / clip.w + depthBias)
                        tFar = tMid;
                    else
                        tNear = tMid;
                }
                float4 clip = clipBase + clipStep * (0.5 * (tNear + tFar));
                float2 ndc = clip.xy / max(clip.w, 1.0e-5);
                float2 marchUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                if (all(abs(ndc) <= 1.0) && all(isfinite(marchUV)))
                    reflectedUV = marchUV;
                roomHit = 1;
                roomDistance = 0.5 * (tNear + tFar);
            }
            else if (!leftFrustum && lastInsideValid)
            {
                reflectedUV = lastInsideUV;
                roomHit = 2;
            }
        }
        if (roomHit == 0)
        {
            // Still eye (or the ray left the capture): the plain sample; place its surface on this ray.
            uint2 plainPixel = min(uint2(saturate(reflectedUV) * float2(roomDepthWidth, roomDepthHeight)),
                uint2(roomDepthWidth - 1, roomDepthHeight - 1));
            float plainDepth = PlanarDepth.Load(int3(plainPixel, 0));
            if (plainDepth < 0.999999)
            {
                float4 plainTranslation = ReflectedViewProjection[3];
                float4 plainDirection = reflectedClip - plainTranslation;
                float plainDenominator = plainDirection.z - plainDepth * plainDirection.w;
                if (abs(plainDenominator) > 1.0e-7)
                {
                    float t = (plainDepth * plainTranslation.w - plainTranslation.z) / plainDenominator;
                    if (t >= 0.999 && isfinite(t))
                        roomDistance = (t - 1.0) * length(planarRay);
                }
            }
        }
    }

    float3 mirrorColor = max(
        PlanarColor.SampleGrad(MirrorSampler, saturate(reflectedUV), captureFootprintDx, captureFootprintDy).rgb,
        0.0);

    // The player layer: bilinear over its four nearest texels, keeping only texels that hold the player in front
    // of the room surface on this ray.
    float playerCoverage = 0.0;
    float3 playerColor = float3(0.0, 0.0, 0.0);
    float3 playerPoint = float3(0.0, 0.0, 0.0);
    if (layered)
    {
        uint layerWidth = 0;
        uint layerHeight = 0;
        LayerDepth.GetDimensions(layerWidth, layerHeight);
        float3 layerRay = paneWorld - LayerEye.xyz;
        float4 layerClip = mul(float4(layerRay, 1.0), LayerViewProjection);
        if (layerWidth != 0 && layerHeight != 0 && layerClip.w > 1.0e-5)
        {
            float2 layerNdc = layerClip.xy / layerClip.w;
            if (all(abs(layerNdc) <= 1.0))
            {
                float2 layerUV = float2(layerNdc.x * 0.5 + 0.5, 0.5 - layerNdc.y * 0.5);
                float2 texel = layerUV * float2(layerWidth, layerHeight) - 0.5;
                int2 base = int2(floor(texel));
                float2 fraction = texel - float2(base);
                int2 last = int2(layerWidth - 1, layerHeight - 1);
                float4 layerTranslation = LayerViewProjection[3];
                float4 layerDirection = layerClip - layerTranslation;
                float layerRayLength = length(layerRay);
                float nearestT = 1.0e30;
                [unroll]
                for (uint tap = 0; tap < 4; ++tap)
                {
                    int2 offset = int2(tap & 1u, (tap >> 1) & 1u);
                    int2 texelPixel = clamp(base + offset, int2(0, 0), last);
                    float weight = (offset.x != 0 ? fraction.x : 1.0 - fraction.x) *
                        (offset.y != 0 ? fraction.y : 1.0 - fraction.y);
                    float layerDepth = LayerDepth.Load(int3(texelPixel, 0));
                    if (!(weight > 0.0) || !(layerDepth < 0.999999))
                        continue;
                    float denominator = layerDirection.z - layerDepth * layerDirection.w;
                    if (abs(denominator) <= 1.0e-7)
                        continue;
                    float t = (layerDepth * layerTranslation.w - layerTranslation.z) / denominator;
                    if (!(t >= 0.999) || !isfinite(t))
                        continue;
                    // A room surface clearly in front of the player (a table between him and the glass) hides him.
                    float beyond = (t - 1.0) * layerRayLength;
                    if (roomDistance >= 0.0 && roomDistance + 2.0 < beyond)
                        continue;
                    playerCoverage += weight;
                    playerColor += weight * max(LayerColor.Load(int3(texelPixel, 0)).rgb, 0.0);
                    if (t < nearestT)
                    {
                        nearestT = t;
                        playerPoint = LayerEye.xyz + layerRay * t;
                    }
                }
                if (playerCoverage > 1.0e-4)
                    playerColor /= playerCoverage;
            }
        }
    }
    mirrorColor = lerp(mirrorColor, playerColor, saturate(playerCoverage)) * max(OutputEncoding.y, 0.0);
    // Before native Forward the target is HDR. Fallout applies imagespace once
    // after blending flames/smoke; a late tone-mapped pane would cover them.

    float4 motion = float4(0.0, 0.0, 0.0, 0.0);
    {
        // Where this pixel's content was on the previous frame. The pane shows the capture as a picture seen
        // from ReflectedEye; the previous frame showed the capture seen from PreviousReflectedEye (the same eye
        // when no new capture arrived). The capture depth places the reflected point on this pixel's ray; the
        // line from the previous eye to that point crosses the pane where the point was shown, and the previous
        // main camera projects that pane point. With no new capture this is the pane surface itself; with a
        // capture every frame it is the true reflected point (rotation and parallax). No depth: pane surface.
        // Room reuse re-projects every frame, so its previous eye is the previous camera mirrored in the pane.
        float3 previousEye = PreviousReflectedEye.xyz;
        if (layered)
        {
            float3 previousCamera = PreviousFrame[35].xyz;
            previousEye = previousCamera -
                2.0 * dot(previousCamera - MirrorCenter.xyz, MirrorNormal.xyz) * MirrorNormal.xyz;
        }
        float3 previousPanePoint = paneWorld;
        float3 towardPoint = planarRay;
        bool pointKnown = false;
        if (layered && playerCoverage >= 0.5)
        {
            towardPoint = playerPoint - previousEye;
            pointKnown = true;
        }
        else if (layered && roomHit == 1)
        {
            towardPoint = paneWorld + marchDirection * roomDistance - previousEye;
            pointKnown = true;
        }
        else if (layered && roomHit == 2)
        {
            towardPoint = marchDirection;
            pointKnown = true;
        }
        else
        {
            uint depthWidth = 0, depthHeight = 0;
            PlanarDepth.GetDimensions(depthWidth, depthHeight);
            if (depthWidth != 0 && depthHeight != 0)
            {
                uint2 capturePixel = min(uint2(saturate(reflectedUV) * float2(depthWidth, depthHeight)),
                    uint2(depthWidth - 1, depthHeight - 1));
                // Captures render through a full 0..1 viewport (mode 6 above), so the stored value is NDC depth.
                float captureDepth = PlanarDepth.Load(int3(capturePixel, 0));
                // A cleared texel (sky, or past the capture's range) is a point at infinity along this pixel's
                // ray, seen along the same direction from anywhere.
                pointKnown = captureDepth >= 0.999999;
                if (!pointKnown)
                {
                    // clip(t) = t * direction + translation for ReflectedEye + planarRay * t (t = 1 on the pane).
                    float4 translation = ReflectedViewProjection[3];
                    float4 direction = reflectedClip - translation;
                    float denominator = direction.z - captureDepth * direction.w;
                    bool solvable = abs(denominator) > 1.0e-7;
                    float t = solvable ?
                        (captureDepth * translation.w - translation.z) / (solvable ? denominator : 1.0) : 0.0;
                    if (t >= 0.999 && isfinite(t))
                    {
                        towardPoint = ReflectedEye.xyz + planarRay * t - previousEye;
                        pointKnown = true;
                    }
                }
            }
        }
        float approach = dot(towardPoint, MirrorNormal.xyz);
        if (pointKnown && abs(approach) > 1.0e-6)
        {
            float s = dot(paneWorld - previousEye, MirrorNormal.xyz) / approach;
            if (s > 0.0 && isfinite(s))
                previousPanePoint = previousEye + towardPoint * s;
        }
        float4 previousRelative = float4(previousPanePoint - PreviousFrame[35].xyz, 1.0);
        // Fallout's native prepass velocity uses the unjittered current matrix
        // at cb12[37..40]. Raster rows 8..11 include temporal jitter. Comparing
        // those raster positions made a stationary reflection move under DLSS.
        // Each snapshot carries its own current matrix and matching origin.
        float4 previousClip = float4(
            dot(PreviousFrame[37], previousRelative),
            dot(PreviousFrame[38], previousRelative),
            dot(PreviousFrame[39], previousRelative),
            dot(PreviousFrame[40], previousRelative));
        float4 currentRelative = float4(paneWorld - Frame[35].xyz, 1.0);
        float3 currentClip = float3(dot(Frame[37], currentRelative),
            dot(Frame[38], currentRelative), dot(Frame[40], currentRelative));
        if (previousClip.w > 1.0e-5 && currentClip.z > 1.0e-5)
        {
            float2 previousNdc = previousClip.xy / previousClip.w;
            float2 currentNdc = currentClip.xy / currentClip.z;
            if (all(abs(previousNdc) <= 1.5) && all(isfinite(previousNdc)) && all(isfinite(currentNdc)))
            {
                float2 previousUV = float2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
                float2 currentUV = float2(currentNdc.x * 0.5 + 0.5, 0.5 - currentNdc.y * 0.5);
                motion = float4(previousUV - currentUV, 0.0, motionRim);
            }
        }
    }
    if (OutputEncoding.x < 0.0)
        return Emit(float4(mirrorColor, 1.0), motion);
    mirrorColor = saturate(mirrorColor / (1.0 + mirrorColor));
    if (OutputEncoding.x > 0.5)
        mirrorColor = pow(mirrorColor, 1.0 / 2.2);
    return Emit(float4(mirrorColor, 1.0), motion);
}
