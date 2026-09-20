// Per-tile light culling for the mirror resolve.
//
// The resolve used to test every admitted light at every pixel. That was tolerable at 32 lights and became
// unusable the moment the set was allowed to hold every light a room has: Sandy Coves admits 432, and at a
// 4096 capture that is 7.25 BILLION light-iterations per capture, ~290 billion per second across the fleet.
// Measured 2026-09-16: FPS 3.34-5.88, frameAvg 170-299 ms, with our CPU cost only 17 ms per five seconds -
// entirely GPU, and entirely this loop.
//
// Fallout does not shade this way. Its deferred renderer draws a light VOLUME per light, so a light only ever
// touches the pixels it actually covers. This pass is the compute equivalent: one thread group per 32x32 tile
// reduces that tile's depth range, builds the tile's world-space bounds, and keeps only the lights whose
// sphere reaches them. A pixel then loops its own tile's short list instead of the whole room.
//
// Cost of the cull itself is trivial by comparison: tiles x lights, not pixels x lights.
#define MIRROR_LIGHTING_SLOT b0
#include "FlatMirrorLighting.hlsli"

Texture2D<float> TileDepth : register(t3);          // same capture depth the resolve reads
RWStructuredBuffer<uint> TileLights : register(u0);

// Eight 32-bit masks represent every admitted light without truncation or an
// overflow fallback to the whole room. The consumer iterates only set bits.
#define MIRROR_TILE_THREADS 8   // 8x8 threads per group, each covering a 4x4 block of the tile

groupshared uint g_minDepth;
groupshared uint g_maxDepth;
groupshared uint g_masks[MIRROR_TILE_WORDS];
groupshared float3 g_boundsMin;
groupshared float3 g_boundsMax;
groupshared bool g_unbounded;

[numthreads(MIRROR_TILE_THREADS, MIRROR_TILE_THREADS, 1)]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID, uint flatThread : SV_GroupIndex)
{
    const uint2 extent = ExtentLightsHistory.xy;
    if (extent.x == 0 || extent.y == 0) return;
    const uint2 tiles = (extent + MIRROR_TILE_SIZE - 1) / MIRROR_TILE_SIZE;
    if (groupID.x >= tiles.x || groupID.y >= tiles.y) return;
    const uint tileIndex = groupID.y * tiles.x + groupID.x;

    if (flatThread == 0) { g_minDepth = 0x7F7FFFFFu; g_maxDepth = 0u; }
    if (flatThread < MIRROR_TILE_WORDS) g_masks[flatThread]=0;
    GroupMemoryBarrierWithGroupSync();

    // Depth range of this tile. Empty texels (cleared depth) are skipped so a tile that is mostly sky does not
    // stretch its bounds to the far plane and re-admit every light in the cell.
    const uint2 tileOrigin = groupID.xy * MIRROR_TILE_SIZE;
    const uint step = MIRROR_TILE_SIZE / MIRROR_TILE_THREADS;   // 4
    uint localMin = 0x7F7FFFFFu, localMax = 0u;
    for (uint y = 0; y < step; ++y) {
        for (uint x = 0; x < step; ++x) {
            const uint2 texel = tileOrigin + threadID.xy * step + uint2(x, y);
            if (texel.x >= extent.x || texel.y >= extent.y) continue;
            const float depth = TileDepth.Load(int3(texel, 0));
            if (!(depth >= 0 && depth < 0.999999)) continue;
            const uint bits = asuint(depth);
            localMin = min(localMin, bits);
            localMax = max(localMax, bits);
        }
    }
    InterlockedMin(g_minDepth, localMin);
    InterlockedMax(g_maxDepth, localMax);
    GroupMemoryBarrierWithGroupSync();

    const float nearDepth = asfloat(g_minDepth);
    const float farDepth = asfloat(g_maxDepth);
    const bool empty = g_minDepth > g_maxDepth;

    // World bounds of the tile, camera-relative exactly as the resolve reconstructs its pixels. Eight corners
    // of the tile's depth slab; their AABB is conservative and cheap to test a sphere against.
    // The depth slab is identical for all 64 threads. Build it once, then
    // share it with the parallel light tests.
    if (flatThread == 0) {
        g_boundsMin = 1.0e30;
        g_boundsMax = -1.0e30;
        g_unbounded = false;
      if (!empty) {
        const float2 uvMin = float2(tileOrigin) / float2(extent);
        const float2 uvMax = float2(min(tileOrigin + MIRROR_TILE_SIZE, extent)) / float2(extent);
        float minimumW = 1.0e30, maximumW = -1.0e30;
        [unroll] for (uint corner = 0; corner < 8; ++corner) {
            const float2 uv = float2((corner & 1) ? uvMax.x : uvMin.x, (corner & 2) ? uvMax.y : uvMin.y);
            const float depth = (corner & 4) ? farDepth : nearDepth;
            const float4 clip = float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1);
            const float4 h = mul(clip, InverseViewProjection);
            minimumW = min(minimumW, h.w);
            maximumW = max(maximumW, h.w);
            if (!all(isfinite(h)) || abs(h.w) < 1.0e-9) { g_unbounded = true; continue; }
            const float3 p = h.xyz / h.w;
            g_boundsMin = min(g_boundsMin, p);
            g_boundsMax = max(g_boundsMax, p);
        }
        // A projective pole inside the slab has no finite corner AABB. Keep
        // all lights in this uncommon case instead of culling real light.
        g_unbounded = g_unbounded || (minimumW <= 0 && maximumW >= 0);
      }
    }
    GroupMemoryBarrierWithGroupSync();

    // One pass over the admitted set, strided across the group's 64 threads.
    const uint lightCount = min(ExtentLightsHistory.z, (uint)MIRROR_MAX_LIGHTS);
    if (!empty) {
        for (uint index = flatThread; index < lightCount; index += MIRROR_TILE_THREADS * MIRROR_TILE_THREADS) {
            const Light light = Lights[index];
            bool reaches = true;
            if (light.positionRadius.w > 0 && !g_unbounded) {
                // The resolve works in camera-relative space; Eye.xyz converts the light's world position.
                const float3 centre = light.positionRadius.xyz - Eye.xyz;
                const float3 closest = clamp(centre, g_boundsMin, g_boundsMax);
                const float3 delta = centre - closest;
                reaches = dot(delta, delta) < light.positionRadius.w * light.positionRadius.w;
            }
            // A directional light (radius 0) reaches everything and is always kept.
            if (!reaches) continue;
            InterlockedOr(g_masks[index>>5],1u<<(index&31));
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (flatThread < MIRROR_TILE_WORDS)
        TileLights[tileIndex*MIRROR_TILE_WORDS+flatThread]=g_masks[flatThread];
}
