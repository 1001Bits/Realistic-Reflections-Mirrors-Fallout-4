// Flat coherent mode-0x18 resolve: current reflected lighting and bounded surface history.
#include "FlatMirrorShadows.hlsli"
#include "FlatMirrorPrivateSun.hlsli"
static float SunVisibility=1;
// Player-mask pixel of this capture (exact detached-player depth); lets the MCM toggle drop local-light
// shadows on the reflected body while the room keeps its shadows.
static bool PlayerPixel=false;
// Set per pixel from the capture depth before the illumination call, exactly as SunVisibility is.
static float AmbientOcclusion=1;
#define MIRROR_AMBIENT_OCCLUSION AmbientOcclusion
#define MIRROR_SHADOW_VISIBILITY(index,world) (Lights[index].positionRadius.w==0 ? SunVisibility : \
    ((PlayerPixel && (uint(PrivateSunSampling.w)&2u)) ? 1 : MirrorLocalShadowVisibility(index,world)))
// Per-tile masks, filled with the same geometry constants as this resolve.
// Eight words represent all 256 lights. An unbound list falls back to the full set.
#define MIRROR_CUSTOM_TILE_LOOP
static uint g_tileBase = 0;
static bool g_tileValid = false;
// GetDimensions distinguishes an unbound list from a valid empty tile mask.
StructuredBuffer<uint> TileLights : register(t31);
// No per-light screen-space occlusion. Fallout shadows local lights with its atlas or not at all, and a
// depth buffer cannot stand in for the missing maps: it stores the nearest surface, so the player - who is
// directly in front of the reflected camera - reads as an occluder for every ray that passes behind him, and
// a fixture against a wall reads as occluded by that wall. Both were observed in game (person-shaped shadow
// tracking the player; Third Rail urinals unlit, 2026-09-16). MIRROR_LIGHT_VISIBILITY therefore keeps its
// default of 1 and the lighting loop spends no taps on it.

#include "FlatMirrorLighting.hlsli"
bool MirrorNextLight(out uint index,inout uint cursor,inout uint mask)
{
    if(!g_tileValid) { index=cursor++;return index<min(ExtentLightsHistory.z,(uint)MIRROR_MAX_LIGHTS); }
    [loop] while(mask==0 && cursor<MIRROR_TILE_WORDS) mask=TileLights[g_tileBase+cursor++];
    index=0;
    if(mask==0)return false;
    index=(cursor-1)*32+firstbitlow(mask);
    mask&=mask-1;
    return true;
}
Texture2D<float4> MaterialDiffuse : register(t0);
Texture2D<float4> MaterialNormal : register(t1);
// Native DFPrepass SV_Target3, captured at its physical slot and sampled here
// at t2: ordinary surfaces use gloss/specular; hair (w*255 == 1) stores a tangent.
// Unbound (two-target layout) reads as zero = no highlight.
Texture2D<float4> MaterialProperties : register(t2);
Texture2D<float> MaterialDepth : register(t3);
// Captured native emissive target (unbound reads zero).
Texture2D<float4> MaterialEmissive : register(t30);
Texture2D<float4> PreviousMirrorColor : register(t5);
Texture2D<uint> PreviousMetadata : register(t6);
RWTexture2D<float4> ResolvedColor : register(u0);
// Upper 24 bits: native depth. Lower 8 bits: accumulated repair age in milliseconds.
RWTexture2D<uint> ResolvedMetadata : register(u1);
static const uint InvalidMetadata = 0xffffffffu;

float3 DecodeNormal(float2 encoded)
{
    float2 xy=encoded*4-2;
    float squared=min(dot(xy,xy),4);
    return float3(xy*sqrt(max(1-squared*.25,0)),squared*.5-1);
}
uint Metadata(float depth,uint age) {return (uint(round(depth*16777214.0))<<8)|age;}
float3 Position(float2 uv,float depth,row_major float4x4 inverse)
{
    float4 p=mul(float4(uv.x*2-1,1-uv.y*2,depth,1),inverse);
    return p.xyz/p.w;
}

// Ambient occlusion over the capture's own depth.
//
// Fallout multiplies its ambient by a screen-space AO buffer before it ever reaches a surface
// (ISLightingComposite: `diffuse * sao + shadowMask * dirDiffuse`, then * albedo). The mirror applied the
// cell's directional ambient FLAT, with nothing occluding it, so an enclosed room received the whole
// cell-wide ambient on every surface, including enclosed surfaces with little ambient exposure.
// Applying the full ambient uniformly over-lights enclosed rooms and removes contact shading.
// Scaling ambient uniformly lowers overall brightness without restoring the spatial variation.
// Occlusion instead attenuates illumination according to nearby geometry and surface orientation.
// The capture's own depth provides the geometry needed to estimate that attenuation.
//
// Horizon-style: a neighbour that rises above this pixel's tangent plane occludes it, weighted by how
// directly it sits along the normal and faded out with distance so distant geometry never shadows.
static const float kAmbientOcclusionRadius = 96.0;   // game units; roughly a metre and a quarter
static const float kAmbientOcclusionStrength = 1.0;
// Below this many capture pixels across the reach the term cannot be resolved and fades out.
static const float kAmbientOcclusionMinimumPixels = 2.0;
static const float kAmbientOcclusionFadePixels = 4.0;
static const float kAmbientOcclusionMaximumPixels = 48.0;
float CaptureAmbientOcclusion(uint2 pixel,float3 relative,float3 normal,float2 extent,float depth)
{
    // The tap radius is the world reach measured in this pixel's own capture pixels. A fixed pixel radius
    // (2% of the capture width) covered a different world distance whenever the capture resolution, the
    // pane-fitted aperture or the object's distance changed. A world-space radius keeps contact shading
    // stable as the camera moves, independent of capture resolution and projected pane size.
    const float2 uv=(float2(pixel)+0.5)/extent;
    const float3 centre=Position(uv,depth,InverseViewProjection);
    const float worldPerPixel=length(Position(uv+float2(1.0/extent.x,0),depth,InverseViewProjection)-centre);
    if(!(worldPerPixel>1.0e-5) || !isfinite(worldPerPixel)) return 1;
    const float reachPixels=kAmbientOcclusionRadius/worldPerPixel;
    const float resolvable=saturate((reachPixels-kAmbientOcclusionMinimumPixels)/kAmbientOcclusionFadePixels);
    if(resolvable<=0) return 1;
    const float pixelRadius=min(reachPixels,kAmbientOcclusionMaximumPixels);
    const int2 limit=int2(extent)-1;
    float occlusion=0,weight=0;
    [unroll] for(int i=0;i<8;++i) {
        const float angle=(float(i)+0.5)*0.78539816;
        const float scale=((i&3)+1)*0.25;
        const float2 offset=float2(cos(angle),sin(angle))*pixelRadius*scale;
        const int2 tap=clamp(int2(pixel)+int2(offset),int2(0,0),limit);
        const float tapDepth=MaterialDepth.Load(int3(tap,0));
        weight+=1;
        if(!(tapDepth>=0 && tapDepth<0.999999)) continue;
        const float2 tapUV=(float2(tap)+0.5)/extent;
        const float3 delta=Position(tapUV,tapDepth,InverseViewProjection)-relative;
        const float distance=length(delta);
        if(distance<1.0e-3 || distance>kAmbientOcclusionRadius) continue;
        // Only a neighbour ABOVE the tangent plane occludes; a coplanar wall must not darken itself.
        occlusion+=saturate(dot(normal,delta/distance))*saturate(1.0-distance/kAmbientOcclusionRadius);
    }
    if(weight<=0) return 1;
    return lerp(1.0,saturate(1.0-kAmbientOcclusionStrength*occlusion/weight),resolvable);
}

float2 ReceiverDepthGradient(uint2 pixel,int2 axis,float depth,float2 extent)
{
    int2 limit=int2(extent)-1;
    int2 a=clamp(int2(pixel)+axis,0,limit), b=clamp(int2(pixel)-axis,0,limit);
    float za=MaterialDepth.Load(int3(a,0)), zb=MaterialDepth.Load(int3(b,0));
    bool validA=za>=0 && za<.999999 && any(a!=int2(pixel));
    bool validB=zb>=0 && zb<.999999 && any(b!=int2(pixel));
    // Clip depth is affine across a triangle. Fit it before the perspective
    // divide, where subtracting two large reconstructed positions magnifies
    // float cancellation and D24 rounding. Prefer the side of the surface
    // closest to this pixel so an unrelated silhouette does not set its slope.
    if(!validA && !validB) return 0;
    int side=(validA && (!validB || abs(za-depth)<=abs(depth-zb)))?1:-1;
    const float uncertainty=2.0/16777215.0; // D24 rounding plus normalized-float conversion
    // Where both sides agree on the same plane, use the direction with room
    // for a longer measurement. Pixels at the pane border can still obtain a
    // precise slope without reading beyond the current receiver image.
    if(validA && validB && abs((za-depth)-(depth-zb))<=2*uncertainty)
        side=dot(limit-int2(pixel),axis)>=dot(int2(pixel),axis)?1:-1;
    float gradient=side==1?za-depth:depth-zb;
    float2 interval=float2(gradient-uncertainty,gradient+uncertainty);
    const int available=dot(side==1?limit-int2(pixel):int2(pixel),axis);
    const int maximumStep=min(available,512);
    [loop] for(int requested=2;requested<=512;requested*=2) {
        const int step=min(requested,maximumStep);
        if(step<2) break;
        int2 samplePixel=int2(pixel)+axis*(side*step);
        if(any(samplePixel<0) || any(samplePixel>limit)) break;
        float z=MaterialDepth.Load(int3(samplePixel,0));
        if(!(z>=0 && z<.999999)) break;
        float value=(z-depth)*(float(side)/step),radius=uncertainty/step;
        float2 tighter=float2(max(interval.x,value-radius),min(interval.y,value+radius));
        // Stop at a different plane/edge instead of smoothing across it.
        if(tighter.x>tighter.y) break;
        interval=tighter;
        if(step==maximumStep) break;
    }
    return float2((interval.x+interval.y)*.5,(interval.y-interval.x)*.5);
}

float4 ReceiverHomogeneous(float4 clip,out float4 error)
{
    // Rebase clip Z at one before multiplying. In perspective W, subtracting
    // two near-unit products otherwise throws away the far receiver's low
    // bits. The rebased product stays small; no double-precision GPU is needed.
    // Bound these actual operations rather than four times the absolute dot
    // product, which erased real distant shadows with a near plane of one.
    const float unitRoundoff=5.9604645e-8;
    precise float4 x=clip.x*InverseViewProjection[0];
    precise float4 y=clip.y*InverseViewProjection[1];
    precise float zRelative=clip.z-1;
    precise float4 z=zRelative*InverseViewProjection[2];
    precise float4 offset=InverseViewProjection[2]+InverseViewProjection[3];
    precise float4 a=z+offset;
    precise float4 b=a+y;
    precise float4 result=b+x;
    error=(abs(x)+abs(y)+abs(z)+abs(offset)+abs(a)+abs(b)+abs(result)+
        abs(InverseViewProjection[2])*abs(zRelative))*(unitRoundoff/(1-unitRoundoff));
    // Half a D24 step, normalized-float conversion and native float depth
    // interpolation. The precise reconstruction above has its own bound.
    error+=abs(InverseViewProjection[2])*(.5/16777215.0+unitRoundoff);
    error+=(abs(InverseViewProjection[0])+abs(InverseViewProjection[1]))*(2*unitRoundoff);
    return result;
}
float3 ReceiverPositionError(float4 homogeneous,float4 error,float3 world)
{
    const float unitRoundoff=5.9604645e-8;
    float w=abs(homogeneous.w);
    float denominator=w-error.w;
    if(!(denominator>0)) return float3(-1,-1,-1); // No finite receiver bound; reject shadow coverage.
    return (error.xyz+abs(homogeneous.xyz/homogeneous.w)*error.w)/denominator+
        abs(world)*(2*unitRoundoff);
}

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width,height; ResolvedColor.GetDimensions(width,height);
    if(id.x>=width||id.y>=height)return;
    ResolvedColor[id.xy]=float4(0,0,0,1);
    ResolvedMetadata[id.xy]=InvalidMetadata;
    float depth=MaterialDepth.Load(int3(id.xy,0));
    // Empty/disoccluded pixels never inherit an object from a prior capture.
    if(ResolveContract.x!=MIRROR_LIGHTING_ABI || width!=ExtentLightsHistory.x || height!=ExtentLightsHistory.y ||
        !(depth>=0 && depth<0.999999))return;
    float2 extent=float2(width,height);
    float2 uv=(float2(id.xy)+.5)/extent;
    float4 clip=float4(uv.x*2-1,1-uv.y*2,depth,1);
    float4 homogeneousError;
    float4 homogeneous=ReceiverHomogeneous(clip,homogeneousError);
    float3 relative=homogeneous.xyz/homogeneous.w;
    if(!all(isfinite(relative)))return;
    float3 world=relative+Eye.xyz;
    float4 material=MaterialDiffuse.Load(int3(id.xy,0));
    if(!all(isfinite(material)) || any(abs(material)>65504) || all(material<0)) {
        if(!ExtentLightsHistory.w || History.x<=0 || History.x>History.y)return;
        float4 previousClip=mul(float4(world-PreviousEye.xyz,1),PreviousViewProjection);
        if(!all(isfinite(previousClip)) || previousClip.w<=0)return;
        float2 previousUV=previousClip.xy/previousClip.w*float2(.5,-.5)+.5;
        if(any(previousUV<0)||any(previousUV>=1))return;
        uint2 previousPixel=uint2(previousUV*extent);
        uint metadata=PreviousMetadata.Load(int3(previousPixel,0));
        if(metadata==InvalidMetadata)return;
        uint age=(metadata&255u)+uint(ceil(History.x));
        if(age>min(uint(History.y),254u))return;
        float previousDepth=float(metadata>>8)/16777214.0;
        float2 centerUV=(float2(previousPixel)+.5)/extent;
        float3 previousRelative=Position(centerUV,previousDepth,PreviousInverseViewProjection);
        float3 previousWorld=previousRelative+PreviousEye.xyz;
        float3 adjacent=Position(centerUV+float2(1/extent.x,0),previousDepth,PreviousInverseViewProjection);
        float tolerance=max(History.z,1.5*length(adjacent-previousRelative));
        if(!all(isfinite(previousWorld)) || distance(previousWorld,world)>tolerance)return;
        float4 old=PreviousMirrorColor.Load(int3(previousPixel,0));
        if(!all(isfinite(old)) || any(abs(old)>65504))return;
        ResolvedColor[id.xy]=old;
        ResolvedMetadata[id.xy]=Metadata(depth,age); // age is preserved, never renewed
        return;
    }
    float2 encoded=MaterialNormal.Load(int3(id.xy,0)).xy;
    if(!all(isfinite(encoded)))return;
    float3 normal=SafeNormalize(mul(float4(DecodeNormal(encoded),0),NormalToWorld).xyz);
    // Sun visibility belongs to this receiver, not to each light iteration.
    // Keeping the large receiver structure local also prevents FXC from
    // propagating mutable global plane state through the lighting loop.
    if(PrivateSunContract.x==MIRROR_PRIVATE_SUN_ABI && PrivateSunContract.y) {
        PrivateSunReceiver receiver=(PrivateSunReceiver)0;
        float2 gradientX=ReceiverDepthGradient(id.xy,int2(1,0),depth,extent);
        float2 gradientY=ReceiverDepthGradient(id.xy,int2(0,1),depth,extent);
        // Exact differential of the homogeneous divide, up to a common scale
        // that cancels when solving the light-space receiver plane.
        float3 tangentX=InverseViewProjection[0].xyz-relative*InverseViewProjection[0].w;
        float3 tangentY=InverseViewProjection[1].xyz-relative*InverseViewProjection[1].w;
        float3 tangentZ=InverseViewProjection[2].xyz-relative*InverseViewProjection[2].w;
        receiver.dx=tangentX*(2/extent.x)+tangentZ*gradientX.x;
        receiver.dy=tangentY*(-2/extent.y)+tangentZ*gradientY.x;
        receiver.dxError=tangentZ*gradientX.y;
        receiver.dyError=tangentZ*gradientY.y;
        receiver.positionError=ReceiverPositionError(homogeneous,homogeneousError,world);
        receiver.player=PrivateSunPlayerPixel(id.xy,depth);
        PlayerPixel=receiver.player;
        SunVisibility=PrivateSunVisibility(world,receiver);
    }
    // Resolve this pixel's tile before shading. A zero-length buffer (cull not dispatched) leaves
    // g_tileValid false and the lighting loop walks every admitted light, exactly as it always did.
    {
        uint tileStride=0, tileElements=0;
        TileLights.GetDimensions(tileElements,tileStride);
        const uint2 tiles=(uint2(extent)+MIRROR_TILE_SIZE-1)/MIRROR_TILE_SIZE;
        const uint tileTotal=tiles.x*tiles.y;
        if(tileElements>=tileTotal*MIRROR_TILE_WORDS && tileTotal>0) {
            const uint2 tile=id.xy/MIRROR_TILE_SIZE;
            const uint tileIndex=tile.y*tiles.x+tile.x;
            g_tileBase=tileIndex*MIRROR_TILE_WORDS;
            g_tileValid=true;
        }
    }
    AmbientOcclusion=CaptureAmbientOcclusion(id.xy,relative,normal,extent,depth);
    float4 properties=MaterialProperties.Load(int3(id.xy,0));
    if(!all(isfinite(properties)))properties=0;
    float3 view=SafeNormalize(Eye.xyz-world);
    float3 highlight;
    float3 illumination=MirrorIlluminationMaterial(world,normal,view,properties,material.a,highlight);
    // DFComposite adds native MRT4 emission after diffuse and specular.
    float3 emission=MaterialEmissive.Load(int3(id.xy,0)).rgb;
    float3 color=max(material.rgb,0)*illumination+highlight+max(emission,0);
    if(!all(isfinite(color)))return;
    // Player-mask debug view (Data\dynref_playermask): green = treated as the player, magenta = the mask has the
    // player here but the exact-depth test rejected the pixel.
    if(PrivateSunContract.x==MIRROR_PRIVATE_SUN_ABI && PrivateSunContract.w && (uint(PrivateSunSampling.w)&4u)) {
        uint maskWidth,maskHeight; PrivatePlayerDepth.GetDimensions(maskWidth,maskHeight);
        if(id.x<maskWidth && id.y<maskHeight) {
            float maskDepth=PrivatePlayerDepth.Load(int3(id.xy,0));
            if(maskDepth>=0 && maskDepth<.999999)
                color=lerp(color,PlayerPixel?float3(0,1,0):float3(1,0,1),.7);
        }
    }
    ResolvedColor[id.xy]=float4(clamp(color,0,65504),material.a);
    ResolvedMetadata[id.xy]=Metadata(depth,0);
}
