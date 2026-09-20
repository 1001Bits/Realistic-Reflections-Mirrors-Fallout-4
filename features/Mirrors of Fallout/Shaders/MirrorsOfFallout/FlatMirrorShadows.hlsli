#ifndef FLAT_MIRROR_SHADOWS_INCLUDED
#define FLAT_MIRROR_SHADOWS_INCLUDED
#include "MirrorLightingABI.hlsli"
struct MirrorShadowMap
{
    row_major float4x4 worldToShadow;
    float4 bounds;
    float4 sampling;
    uint4 resource;
    float4 sourceEyeFar;
    float4 sourceForwardMin;
    float4 sourceRange;
    uint4 casterHull;
    float4 casterPlanes[6];
};
struct MirrorSunView
{
    float4 eyeFar;
    float4 forwardBlend;
    float4 splits;
    uint4 metadata;
};
cbuffer MirrorShadowContract : register(b2)
{
    uint4 ShadowContract;
    uint4 ShadowLights[MIRROR_MAX_LIGHTS];
    MirrorShadowMap ShadowMaps[36];
    MirrorSunView ShadowSunViews[MIRROR_MAX_LIGHTS];
};
// SM5 resource indexing must be static. This switch selects one texture; it
// does not sample the other lights' maps. Four-tap hardware PCF uses one fetch.
Texture2DArray<float> ShadowTextures[20] : register(t7);
SamplerComparisonState ShadowSampler : register(s1);
float ShadowCompare(uint slot, float3 uv, float depth)
{
#define SHADOW_CASE(N) case (uint)N: { uint width,height,layers; ShadowTextures[N].GetDimensions(width,height,layers); \
    if (!width || !height || uv.z>=layers) return 1; \
    return ShadowTextures[N].SampleCmpLevelZero(ShadowSampler, uv, depth); }
    switch (slot) {
        SHADOW_CASE(0) SHADOW_CASE(1) SHADOW_CASE(2) SHADOW_CASE(3)
        SHADOW_CASE(4) SHADOW_CASE(5) SHADOW_CASE(6) SHADOW_CASE(7)
        SHADOW_CASE(8) SHADOW_CASE(9) SHADOW_CASE(10) SHADOW_CASE(11)
        SHADOW_CASE(12) SHADOW_CASE(13) SHADOW_CASE(14) SHADOW_CASE(15)
        SHADOW_CASE(16) SHADOW_CASE(17) SHADOW_CASE(18) SHADOW_CASE(19)
        default: return 1;
    }
#undef SHADOW_CASE
}
bool ShadowMapValid(MirrorShadowMap map)
{
    return map.resource.w != 0 && map.resource.x < min(ShadowContract.z, 20u) && map.resource.y < 4 &&
        all(isfinite(map.bounds)) && all(isfinite(map.sampling)) &&
        all(map.bounds.xy < map.bounds.zw) && all(map.sampling.zw > 0) && map.sampling.x >= 0;
}
float SampleMirrorShadow(MirrorShadowMap map, float2 uv, float depth)
{
    float2 guard = map.sampling.zw * .5;
    if (!ShadowMapValid(map) || !all(isfinite(uv)) || !isfinite(depth) ||
        depth < 0 || depth > 1 || any(uv < map.bounds.xy + guard) || any(uv > map.bounds.zw - guard)) return 1;
    return ShadowCompare(map.resource.x, float3(uv, map.resource.y), max(depth - map.sampling.x, 0));
}
// Compare the same PCF taps at both ends of the admitted caster interval.
// Only depth texels inside that interval may occlude the receiver. Usually the
// lower bound is zero and this costs the same single comparison fetch as before.
bool ShadowIntervalCompare(uint slot,float3 uv,float lower,float upper,out float visibility)
{
    visibility=1;
#define SHADOW_INTERVAL(N) case (uint)N: { uint width,height,layers; ShadowTextures[N].GetDimensions(width,height,layers); \
    if (!width || !height || uv.z>=layers) return false; \
    float begin=1; [branch] if (lower>0) begin=ShadowTextures[N].SampleCmpLevelZero(ShadowSampler,uv,lower); \
    float end=ShadowTextures[N].SampleCmpLevelZero(ShadowSampler,uv,upper); \
    visibility=saturate(1-begin+end); return true; }
    switch (slot) {
        SHADOW_INTERVAL(0) SHADOW_INTERVAL(1) SHADOW_INTERVAL(2) SHADOW_INTERVAL(3)
        SHADOW_INTERVAL(4) SHADOW_INTERVAL(5) SHADOW_INTERVAL(6) SHADOW_INTERVAL(7)
        SHADOW_INTERVAL(8) SHADOW_INTERVAL(9) SHADOW_INTERVAL(10) SHADOW_INTERVAL(11)
        SHADOW_INTERVAL(12) SHADOW_INTERVAL(13) SHADOW_INTERVAL(14) SHADOW_INTERVAL(15)
        SHADOW_INTERVAL(16) SHADOW_INTERVAL(17) SHADOW_INTERVAL(18) SHADOW_INTERVAL(19)
        default: return false;
    }
#undef SHADOW_INTERVAL
}
bool SampleMirrorSun(MirrorShadowMap map,float3 world,out float visibility)
{
    visibility=1;
    uint mask=map.casterHull.x;
    if (!ShadowMapValid(map) || map.resource.z!=1 || (mask & ~63u) || countbits(mask)<4 ||
        !isfinite(map.sourceRange.y) || map.sourceRange.y<=0) return false;
    float4 p=mul(float4(world,1),map.worldToShadow);
    // Native sun projection is orthographic. Its depth gradient points away
    // from the light and provides world-units -> normalized-depth conversion.
    float3 gradient=float3(map.worldToShadow._13,map.worldToShadow._23,map.worldToShadow._33);
    float scale=length(gradient);
    if (!all(isfinite(p)) || !isfinite(scale) || scale<1.e-12 || abs(p.w-1)>1.e-5 ||
        abs(map.worldToShadow._14)+abs(map.worldToShadow._24)+abs(map.worldToShadow._34)>1.e-6) return false;
    float2 guard=map.sampling.zw*.5;
    if (any(p.xy<map.bounds.xy+guard) || any(p.xy>map.bounds.zw-guard) || p.z<=0) return false;
    float3 towardLight=-gradient/scale;
    // Intersect the receiver-to-light ray with the captured caster volume and
    // shadow depth range. The receiver itself may be outside either volume:
    // casters inside them can still cast shadows onto it further downstream.
    float begin=max(0,(p.z-1)/scale),end=p.z/scale;
    [loop] for (uint plane=0;plane<6;++plane) {
        if (!(mask & (1u<<plane))) continue;
        float4 planeData=map.casterPlanes[plane];
        // Keep the raw exponent check: FXC /O3 elides isfinite() on this dynamic
        // plane load while optimizing the normal-length/ray intersection loop.
        if (any((asuint(planeData)&0x7f800000u)==0x7f800000u) ||
            abs(dot(planeData.xyz,planeData.xyz)-1)>.02) return false;
        float distance=dot(planeData.xyz,world)-planeData.w;
        float direction=dot(planeData.xyz,towardLight);
        if (abs(direction)<1.e-6) { if (distance<0) return false; }
        else if (direction>0) begin=max(begin,-distance/direction);
        else end=min(end,-distance/direction);
    }
    if (!isfinite(begin) || !isfinite(end) || end<=begin) return false;
    float lower=max(p.z-end*scale,0);
    float upper=min(p.z-begin*scale,p.z-map.sampling.x);
    if (upper<=lower) return false;
    return ShadowIntervalCompare(map.resource.x,float3(p.xy,map.resource.y),lower,min(upper,1),visibility);
}
float MirrorSunVisibility(uint light,uint first,uint count,float3 world)
{
    MirrorSunView view=ShadowSunViews[light];
    uint cascades=view.metadata.x;
    if (!view.metadata.y || !cascades || cascades>4 || !all(isfinite(view.eyeFar)) ||
        !all(isfinite(view.forwardBlend)) || !all(isfinite(view.splits)) || view.eyeFar.w<=0 ||
        view.eyeFar.w>1.e8 || view.forwardBlend.w<0 ||
        abs(dot(view.forwardBlend.xyz,view.forwardBlend.xyz)-1)>.02) return 1;
    float previous=0,footprint=0;
    [unroll] for (uint i=0;i<4;++i) {
        if (i<cascades) {
            if (view.splits[i]<=previous || view.splits[i]>view.eyeFar.w) return 1;
            previous=view.splits[i];
        }
    }
    uint mapIndices[4]={36,36,36,36};
    [loop] for (uint m=0;m<min(count,4u);++m) {
        float width=ShadowMaps[first+m].sourceRange.y;
        if (isfinite(width) && width>0) footprint=max(footprint,width);
        uint ordinal=ShadowMaps[first+m].casterHull.y;
        if (ordinal<cascades && ShadowMapValid(ShadowMaps[first+m])) mapIndices[ordinal]=first+m;
    }
    // Honor native overlap; even when its blend is disabled, a bounded texel
    // footprint transition avoids a discontinuity where both maps prove coverage.
    float blend=min(max(view.forwardBlend.w,max(footprint,.001)),view.splits.x*.25);
    float3 fromEye=world-view.eyeFar.xyz;
    float depth=dot(fromEye,view.forwardBlend.xyz);
    uint preferred=4,nextCascade=4;
    [unroll] for (uint candidate=0;candidate<4;++candidate) {
        if (candidate<cascades && mapIndices[candidate]<36) {
            if (preferred==4) preferred=candidate;
            else if (depth>=view.splits[preferred]+blend) preferred=candidate;
        }
    }
    if (preferred==4) return 1;
    [unroll] for (uint next=0;next<4;++next)
        if (next>preferred && next<cascades && mapIndices[next]<36 && nextCascade==4) nextCascade=next;
    float visibility=1;
    bool valid=false,blending=false;
    uint wanted=preferred,tried=0;
    // One sampling call site keeps the SM5 static resource switch compact.
    // Normally one map is fetched, two only in the transition. Failed coverage
    // may try a remaining map, but each native cascade is visited at most once.
    [loop] for (uint attempt=0;attempt<4;++attempt) {
        uint mapIndex=mapIndices[wanted];
        float sample=1;
        bool found=false;
        if (mapIndex<36) found=SampleMirrorSun(ShadowMaps[mapIndex],world,sample);
        tried|=1u<<wanted;
        if (blending) {
            if (found) visibility=lerp(visibility,sample,
                smoothstep(view.splits[preferred],view.splits[preferred]+blend,depth));
            break;
        }
        if (found) {
            visibility=sample;valid=true;
            if (wanted==preferred && nextCascade<4 && depth>view.splits[preferred]) {
                blending=true;wanted=nextCascade;continue;
            }
            break;
        }
        // Bands express resolution preference, never receiver coverage. Every
        // fallback still proves its ray/hull/projector/resource validity.
        wanted=4;
        [unroll] for (uint fallback=0;fallback<4;++fallback)
            if (fallback<cascades && !(tried&(1u<<fallback))) wanted=fallback;
        if (wanted==4) break;
    }
    if (!valid) return 1;
    float fade=saturate(dot(fromEye,fromEye)/(view.eyeFar.w*view.eyeFar.w));
    fade*=fade;fade*=fade;fade*=fade;
    return lerp(visibility,1,fade);
}
float MirrorShadowVisibilityInternal(uint light, float3 world, bool allowNativeSun)
{
    if (ShadowContract.x != MIRROR_SHADOW_ABI || light >= MIRROR_MAX_LIGHTS || !all(isfinite(world))) return 1;
    uint first = ShadowLights[light].x, count = ShadowLights[light].y;
    if (!count || first >= min(ShadowContract.y,36u) || count > min(ShadowContract.y,36u) - first) return 1;
    MirrorShadowMap initial = ShadowMaps[first];
    float4 position = float4(world,1);
    if (initial.resource.z == 1) return allowNativeSun ? MirrorSunVisibility(light,first,count,world) : 1;
    if (!ShadowMapValid(initial)) return 1;
    // Shipped DFLight PS 0x08/0x10: dual/single parabolic point-light maps.
    // Cached native matrix is world -> light space, not an ordinary perspective
    // projection. The rear hemisphere has its own atlas slice and X orientation.
    if (initial.resource.z != 2 && initial.resource.z != 3) return 1;
    float4 p=mul(position,initial.worldToShadow);
    if (!all(isfinite(p)) || abs(p.w)<1.e-8) return 1;
    float3 q=p.xyz/p.w;
    float distance=length(q);
    if (distance<1.e-5) return 1;
    bool back=(p.z*.5+.5)<0;
    if (back && (count<2 || initial.resource.z != 3)) return 1;
    MirrorShadowMap map=ShadowMaps[first+(back?1:0)];
    float3 direction=q/distance;
    float denominator=direction.z+(back?-1:1);
    if (abs(denominator)<1.e-6) return 1;
    float2 parabola=direction.xy/denominator*.5+.5;
    // Native point-map viewport is anchored at the lower-left of each slice.
    float scale=map.bounds.z-map.bounds.x;
    float2 uv=float2(map.bounds.x+parabola.x*scale, map.bounds.w-(1-parabola.y)*scale);
    if (back) uv.y=map.bounds.w-parabola.y*scale;
    return SampleMirrorShadow(map,uv,saturate(distance*map.sampling.y));
}
float MirrorShadowVisibility(uint light,float3 world) {return MirrorShadowVisibilityInternal(light,world,true);}
float MirrorLocalShadowVisibility(uint light,float3 world) {return MirrorShadowVisibilityInternal(light,world,false);}
#endif
