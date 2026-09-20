// Take the reflected image out of Fallout's composite target and into a mirror pane.
//
// The world re-entry renders through the engine's own pipeline, so its result lands in kMAIN at the main
// view's size while a pane target is typically 1024 square. CopyResource cannot bridge that - it demands
// identical dimensions and format and the runtime silently drops a mismatched call - so the copy has to be
// a real filtered downscale.
//
// SourceRect is NOT the composite texture's size. Fallout renders a viewport into full-size targets (a
// 1280x720 viewport inside a 1920x1080 texture is normal, and upscalers make it routine), so reading the
// whole texture would drag in whatever stale pixels sit outside the live viewport. The caller passes the
// viewport the engine actually rasterized, read back from the rasterizer immediately after the stages.
//
// Filtering is a box average over each destination pixel's source footprint. A single bilinear tap would
// alias badly when shrinking 1920 to 1024, and the footprint is small enough that the tap cap is generous.
cbuffer BlitConstants : register(b0)
{
    // xy = source viewport origin in texels, zw = source viewport extent in texels.
    uint4 SourceRect;
    // xy = destination extent in texels, zw = unused.
    uint4 DestinationExtent;
};

Texture2D<float4> Composite : register(t0);
RWTexture2D<float4> Pane : register(u0);

// Cap on taps per axis. Beyond this the average is close enough and the cost stops being free.
#define MIRROR_BLIT_MAX_TAPS 4

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 destination = DestinationExtent.xy;
    if (destination.x == 0 || destination.y == 0) return;
    if (id.x >= destination.x || id.y >= destination.y) return;

    const uint2 origin = SourceRect.xy;
    const uint2 extent = SourceRect.zw;
    if (extent.x == 0 || extent.y == 0) {
        Pane[id.xy] = float4(0, 0, 0, 1);
        return;
    }

    // Source footprint of this destination pixel, in source texels.
    const float2 scale = float2(extent) / float2(destination);
    const float2 begin = float2(origin) + float2(id.xy) * scale;
    const float2 end = begin + scale;

    const int2 first = int2(floor(begin));
    const int2 last = int2(ceil(end)) - 1;
    const int2 limit = int2(origin + extent) - 1;

    const int2 taps = clamp(last - first + 1, int2(1, 1), int2(MIRROR_BLIT_MAX_TAPS, MIRROR_BLIT_MAX_TAPS));

    float4 total = 0;
    float weight = 0;
    [loop] for (int y = 0; y < taps.y; ++y) {
        [loop] for (int x = 0; x < taps.x; ++x) {
            // Spread the capped taps across the whole footprint rather than bunching them at its corner.
            const int2 offset = int2(
                taps.x > 1 ? (int)round(x * (float)(last.x - first.x) / (float)(taps.x - 1)) : 0,
                taps.y > 1 ? (int)round(y * (float)(last.y - first.y) / (float)(taps.y - 1)) : 0);
            const int2 texel = clamp(first + offset, int2(origin), limit);
            const float4 sample = Composite.Load(int3(texel, 0));
            if (!all(isfinite(sample))) continue;
            total += sample;
            weight += 1;
        }
    }
    // Every tap non-finite means the composite holds nothing usable for this pixel; leave opaque black
    // rather than writing NaN into a pane the presenter will sample.
    Pane[id.xy] = weight > 0 ? float4(total.rgb / weight, 1) : float4(0, 0, 0, 1);
}
