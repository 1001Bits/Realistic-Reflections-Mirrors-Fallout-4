#ifndef FLAT_MIRROR_LIGHTING_INCLUDED
#define FLAT_MIRROR_LIGHTING_INCLUDED
#ifndef MIRROR_LIGHTING_SLOT
#define MIRROR_LIGHTING_SLOT b0
#endif
#include "MirrorLightingABI.hlsli"
// Fallout multiplies its ambient by screen-space AO before it reaches a surface. A consumer that can
// compute occlusion defines this; one that cannot leaves ambient unoccluded, as it always was.
#ifndef MIRROR_AMBIENT_OCCLUSION
#define MIRROR_AMBIENT_OCCLUSION 1.0
#endif
// Per-light occlusion for lights with no shadow map. A consumer that can march its capture depth defines
// this; everything else leaves those lights unshadowed, as they were.
#ifndef MIRROR_LIGHT_VISIBILITY
#define MIRROR_LIGHT_VISIBILITY(index, toLight, distance) 1.0
#endif
// Per-tile light culling. A consumer that has run PlanarMirrorLightTilesCS defines these to loop only the
// lights whose sphere reaches this pixel's tile; everything else loops the whole admitted set as before.
// Testing every light at every pixel is what made 432 lights cost 7.25 billion iterations per capture.
// positionRadius: xyz position (sun: direction), w radius (sun 0). color.w = cone exponent.
// spot: xyz direction the spot shines along, w = cos(outer cone); w <= -1.5 = no cone.
// falloff = the light's own attenuation curve as the engine publishes it to cb2[3]:
// x = bias, y = scale, z = exponent. See MirrorIllumination below for the formula.
// falloff.w enables the authored box; volume rows map world position into a
// unit box. Point radius still owns attenuation, exactly as native DFLight.
struct Light { float4 positionRadius; float4 color; float4 spot; float4 falloff; float4 volume[3]; };
cbuffer MirrorResolveContract : register(MIRROR_LIGHTING_SLOT)
{
    float4 ResolveContract; // x=MIRROR_LIGHTING_ABI
    row_major float4x4 InverseViewProjection;
    row_major float4x4 NormalToWorld;
    row_major float4x4 PreviousViewProjection;
    row_major float4x4 PreviousInverseViewProjection;
    row_major float4x4 ViewProjection;   // forward; ABI 4
    float4 Eye;
    float4 PreviousEye;
    float4 Ambient[3];
    uint4 ExtentLightsHistory;
    float4 History; // elapsed ms, maximum age ms, minimum world-distance tolerance
    Light Lights[MIRROR_MAX_LIGHTS];
};

// Copied together at the main world's DFLight draw, before actor-only fill.
// These include native/ENB per-pass changes that GetDirectionalAmbientColors
// cannot see. c0-c2 of cb12 are world-to-view rows, not the reflected view.
cbuffer NativeWorldLighting : register(b3) { float4 NativeLight[28]; };
cbuffer NativeWorldFrameData : register(b4) { float4 NativeFrame[31]; };

#ifdef MIRROR_CUSTOM_TILE_LOOP
bool MirrorNextLight(out uint index,inout uint cursor,inout uint mask);
#else
bool MirrorNextLight(out uint index,inout uint cursor,inout uint mask)
{ index=cursor++;return index<min(ExtentLightsHistory.z,(uint)MIRROR_MAX_LIGHTS); }
#endif

float3 MirrorAmbient(float3 normal)
{
    float4 n=float4(normal,1);
    if (ResolveContract.y>0.5) {
        float4 viewNormal=float4(dot(NativeFrame[0].xyz,normal),dot(NativeFrame[1].xyz,normal),dot(NativeFrame[2].xyz,normal),1);
        return pow(max(float3(dot(NativeLight[6],viewNormal),dot(NativeLight[7],viewNormal),dot(NativeLight[8],viewNormal)),0),2.2);
    }
    return pow(max(float3(dot(Ambient[0],n),dot(Ambient[1],n),dot(Ambient[2],n)),0),2.2);
}

// DFLight 00020201, instructions 66-101: two shifted tangent lobes. Hair
// MRT3.xyz is a view-space tangent, never gloss or a specular-strength mask.
float MirrorHairLobe(float tangentLight,float tangentEye,float shift,float exponent)
{
    float sinLight=sqrt(saturate(1-tangentLight*tangentLight));
    float sinEye=sqrt(saturate(1-tangentEye*tangentEye));
    float shifted=-tangentLight*cos(shift)-sinLight*sin(shift);
    return pow(max(shifted*tangentEye+sqrt(saturate(1-shifted*shifted))*sinEye,0),max(exponent,1.e-4));
}

float3 SafeNormalize(float3 value) { return value * rsqrt(max(dot(value,value),1.e-12)); }
// Native DFLight 00000400 instructions 42-50: cosine ramp raised to the
// BSLight cone exponent. A smoothstep between invented inner/outer angles
// cannot reproduce the authored light, even when its outer extent is right.
float MirrorSpotFactor(Light light, float3 toLight)
{
    if(light.spot.w<=-1.5) return 1;
    float spotCosine=saturate(dot(-SafeNormalize(toLight),light.spot.xyz));
    float cone=saturate(1-(1-spotCosine)/(1-light.spot.w));
    return pow(cone,light.color.w);
}
// Partial reconstruction of native deferred lighting: local diffuse and
// attenuation follow the decompiled DFLight shader. Ambient probe weights,
// exposure and all native light shapes are not yet reproduced completely.
float3 MirrorIlluminationMaterial(float3 world, float3 normal, float3 view, float4 properties, float opacity, out float3 highlight)
{
    bool hair=abs(properties.w*255-1)<.25;
    float gloss=hair?0:saturate(properties.x);
    float specularMask=hair?0:saturate(properties.y);
    float3 tangent=hair?SafeNormalize(mul(float4(properties.xyz,0),NormalToWorld).xyz):0;
    // Ambient is occluded; direct light is not (each light carries its own shadow term below).
    float3 illumination=MirrorAmbient(normal)*MIRROR_AMBIENT_OCCLUSION;
    highlight=0;
    float exponent=max(2.0,saturate(gloss)*128.0);
    float strength=saturate(specularMask);
    // Oren-Nayar terms depend only on the surface, so they are hoisted out of the light loop.
    float roughness=saturate(1.0-saturate(gloss));
    float roughSq=roughness*roughness;
    float orenB=roughSq/(roughSq+0.09)*0.45;
    float orenC=1.0-(roughSq/(roughSq+0.57))*0.5;
    float NdotE=dot(normal,view);
    float sinE=sqrt(max(1.0-NdotE*NdotE,0.0));
    float3 reflEye=view-normal*NdotE;
    uint cursor=0,mask=0,index=0;
    [loop] while(MirrorNextLight(index,cursor,mask)) {
        if(index>=min(ExtentLightsHistory.z,(uint)MIRROR_MAX_LIGHTS))continue;
        Light light=Lights[index];float3 toLight=light.positionRadius.xyz;float attenuation=1;
        bool local=light.positionRadius.w>0;
        if(!local && ResolveContract.y>0.5 && ResolveContract.z>0.5 && ResolveContract.w<0.5)
            light.color.rgb=NativeLight[2].rgb;
        float squared=0;
        if(local) {
            if(light.falloff.w>0) {
                float4 worldPosition=float4(world,1);
                float3 box=float3(dot(worldPosition,light.volume[0]),dot(worldPosition,light.volume[1]),dot(worldPosition,light.volume[2]));
                if(any(abs(box)>1))continue;
            }
            toLight-=world;squared=dot(toLight,toLight);
            float radiusSquared=light.positionRadius.w*light.positionRadius.w;
            if(squared>=radiusSquared)continue;
        }
        float3 direction=SafeNormalize(toLight);
        float cosine=saturate(dot(normal,direction));
        // Back-facing ordinary surfaces contribute neither diffuse nor
        // specular. Reject before the authored attenuation/cone powers.
        if(cosine<=0 && !hair)continue;
        if(local) {
            // The engine's exact curve (DFLight standard deferred permutation, :571-582):
            //   t = saturate(d / radius); a = pow(1 - saturate(scale*pow(t,exponent) + bias), 2.2)
            // The three coefficients are the light's OWN, read from BSLight +0x170/+0x174/+0x178 the way
            // BSDFLightShader::SetupPointLightGeometry fills cb2[3]. This used to hardcode scale 1,
            // bias 0, exponent 2 for every light in the game, so any fixture authored with a different
            // curve was attenuated with the wrong shape.
            const float t=saturate(sqrt(squared)/max(light.positionRadius.w,1.e-6));
            const float curve=saturate(light.falloff.y*pow(max(t,1.e-6),light.falloff.z)+light.falloff.x);
            attenuation=pow(saturate(1-curve),2.2);
            attenuation*=MirrorSpotFactor(light,toLight);
            if(attenuation<=0)continue;
        }
#ifdef MIRROR_SHADOW_VISIBILITY
        if (attenuation>0) attenuation*=MIRROR_SHADOW_VISIBILITY(index,world);
#endif
        // Only local lights are marched: a directional light has no finite distance to march along, and its
        // own private map already covers it.
        // Marching costs eight depth taps and a position reconstruction each, so only lights that would
        // actually show are worth occluding. Below this the light contributes less than a colour step.
        if(local && attenuation>0 && dot(light.color.rgb,float3(.2126,.7152,.0722))*cosine*attenuation>0.002)
            attenuation*=MIRROR_LIGHT_VISIBILITY(index,direction,length(toLight));
        // DFLight :752-766. A rough surface keeps more light at grazing angles than Lambert and less head-on.
        float3 reflView=direction-normal*cosine;
        float sinL=sqrt(max(1.0-cosine*cosine,0.0));
        float geometry=saturate(sinE*sinL)/max(max(cosine,NdotE),1.e-10);
        float diffuse=(max(dot(reflEye,reflView),0)*orenB*geometry+orenC)*cosine;
        if(hair) {
            diffuse=min(opacity,cosine);
            if(ResolveContract.y>0.5) {
                float tl=dot(tangent,direction),te=dot(tangent,view);
                diffuse=min(opacity,saturate(cosine+NativeFrame[28].z*
                    MirrorHairLobe(tl,te,NativeFrame[29].y,NativeFrame[28].w)));
                if(!local) highlight+=light.color.rgb*(cosine*attenuation*NativeFrame[28].x*
                    MirrorHairLobe(tl,te,NativeFrame[29].x,NativeFrame[28].y));
            }
        }
        illumination+=light.color.rgb*(diffuse*attenuation);
        // DFLight :786-788: a local light writes diffuse only. Only the directional branch adds specular.
        if(!local && strength>0) {
            float spec=pow(saturate(dot(normal,SafeNormalize(direction+view))),exponent);
            highlight+=light.color.rgb*(spec*strength*cosine*attenuation);
        }
    }
    // No global brightness correction: the prior 1/3 experiment darkened the
    // lit intro bathroom and was reverted. Native ambient/exposure parity is
    // tracked separately; a room-specific multiplier cannot establish it.
    return illumination;
}
float3 MirrorIlluminationSpecular(float3 world,float3 normal,float3 view,float gloss,float specularMask,out float3 highlight)
{
    return MirrorIlluminationMaterial(world,normal,view,float4(gloss,specularMask,0,0),1,highlight);
}
float3 MirrorIllumination(float3 world, float3 normal)
{
    float3 highlight;
    return MirrorIlluminationSpecular(world,normal,float3(0,0,1),0,0,highlight);
}
#endif
