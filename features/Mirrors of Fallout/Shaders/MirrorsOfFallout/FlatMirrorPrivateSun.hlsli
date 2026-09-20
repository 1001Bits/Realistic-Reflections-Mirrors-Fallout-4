#ifndef MIRROR_PRIVATE_SUN_INCLUDED
#define MIRROR_PRIVATE_SUN_INCLUDED
#define MIRROR_PRIVATE_SUN_ABI 4
cbuffer MirrorPrivateSunContract : register(b12)
{
    uint4 PrivateSunContract;
    row_major float4x4 PrivateSunWorldToTexture[2];
    float4 PrivateSunSampling;
    float4 PrivateSunRange;
};
Texture2D<float> PrivateSunWide : register(t27);
Texture2D<float> PrivateSunDetail : register(t28);
Texture2D<float> PrivatePlayerDepth : register(t29);
SamplerComparisonState PrivateSunSampler : register(s15);

struct PrivateSunReceiver
{
    float3 dx; float3 dy;
    // Signed tangent changes at the bounds of the measured depth gradient.
    // Both are parallel to the reconstruction's depth axis, not arbitrary
    // component-wise errors. Native vertex derivatives leave them zero.
    float3 dxError; float3 dyError;
    float3 positionError; bool player;
};
float3 PrivateSunMinors(float3 dx,float3 dy)
{
    return float3(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z,dx.x*dy.y-dy.x*dx.y);
}
float2 PrivateSunSlope(float3 dx,float3 dy,float determinant)
{
    return float2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
}
// Derivatives are of relative positions, before adding the large world origin.
// Each tap compares against the receiver plane AT THAT TEXEL, independently of
// mirror resolution. A screen-pixel-sized bias caused Skyrim's moving bands.
float PrivateSunMapVisibility(float3 world, PrivateSunReceiver receiver, uint mapIndex, out bool covered)
{
    covered=false;
    float4 p=mul(float4(world,1),PrivateSunWorldToTexture[mapIndex]);
    if(!all(isfinite(p)) || abs(p.w-1)>1e-4 || p.z<=0 || p.z>=1) return 1;
    float2 texel=(mapIndex==0 ? PrivateSunSampling.x : PrivateSunSampling.y).xx;
    uint width,height;
    if(mapIndex==0) PrivateSunWide.GetDimensions(width,height);
    else PrivateSunDetail.GetDimensions(width,height);
    if(!width || !height || !all(isfinite(PrivateSunSampling)) || PrivateSunSampling.z<0 ||
        any(texel<=0) || any(abs(texel*float2(width,height)-1)>.001) ||
        any(p.xy<texel*2) || any(p.xy>1-texel*2)) return 1;
    float3 dx=mul(float4(receiver.dx,0),PrivateSunWorldToTexture[mapIndex]).xyz;
    float3 dy=mul(float4(receiver.dy,0),PrivateSunWorldToTexture[mapIndex]).xyz;
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float2 slope=0;
    if(abs(determinant)>1e-18)
        slope=PrivateSunSlope(dx,dy,determinant);
    if(!all(isfinite(slope)) || !all(isfinite(receiver.positionError)) || any(receiver.positionError<0)) return 1;
    float2 slopeError=0;
    if(any(receiver.dxError!=0) || any(receiver.dyError!=0)) {
        float3 ex=mul(float4(receiver.dxError,0),PrivateSunWorldToTexture[mapIndex]).xyz;
        float3 ey=mul(float4(receiver.dyError,0),PrivateSunWorldToTexture[mapIndex]).xyz;
        // Evaluate the same four interval corners as vector arithmetic. An
        // unrolled loop with early returns made FXC duplicate large control
        // flow regions and significantly slowed cold shader compilation.
        float3 cx=PrivateSunMinors(ex,dy),cy=PrivateSunMinors(dx,ey),cxy=PrivateSunMinors(ex,ey);
        const float4 sx=float4(-1,1,-1,1),sy=float4(-1,-1,1,1),sxy=sx*sy;
        float4 d=determinant+sx*cx.z+sy*cy.z+sxy*cxy.z;
        if(!all(isfinite(d)) || any(abs(d)<=1e-18) || any(d*determinant<=0)) return 1;
        float3 base=PrivateSunMinors(dx,dy);
        float4 x=(base.x+sx*cx.x+sy*cy.x+sxy*cxy.x)/d;
        float4 y=(base.y+sx*cx.y+sy*cy.y+sxy*cxy.y)/d;
        if(!all(isfinite(x)) || !all(isfinite(y))) return 1;
        x=abs(x-slope.x);y=abs(y-slope.y);
        slopeError=float2(max(max(x.x,x.y),max(x.z,x.w)),max(max(y.x,y.y),max(y.z,y.w)));
    }
    // Bound the receiver's reconstruction error in light depth. This is depth
    // quantization/roundoff, not a screen-pixel footprint or view-dependent
    // cascade boundary. The shadow map itself remains fixed in world space.
    float3 depthAxis=float3(PrivateSunWorldToTexture[mapIndex][0][2],
        PrivateSunWorldToTexture[mapIndex][1][2],PrivateSunWorldToTexture[mapIndex][2][2]);
    float3 uAxis=float3(PrivateSunWorldToTexture[mapIndex][0][0],
        PrivateSunWorldToTexture[mapIndex][1][0],PrivateSunWorldToTexture[mapIndex][2][0]);
    float3 vAxis=float3(PrivateSunWorldToTexture[mapIndex][0][1],
        PrivateSunWorldToTexture[mapIndex][1][1],PrivateSunWorldToTexture[mapIndex][2][1]);
    // D3D11 raster coordinates use eight fractional bits. At lower shadow
    // resolutions a subpixel of caster-plane uncertainty spans more world
    // space; include it without tying the bias to a mirror screen pixel.
    float rasterError=dot(abs(slope)+slopeError,texel)/256;
    float2 uvError=float2(dot(abs(uAxis),receiver.positionError),dot(abs(vAxis),receiver.positionError));
    float bias=PrivateSunSampling.z+dot(abs(depthAxis-slope.x*uAxis-slope.y*vAxis),receiver.positionError)+
        dot(slopeError,uvError)+rasterError;
    float visibility=0;
    if(mapIndex==0) {
        // Integrate a 3x3 bilinear PCF kernel as sixteen unique point samples.
        // The old narrow spline gave a single coarse caster texel up to 56.25%
        // of the result. Sunlight can change that texel's raster coverage even
        // with a stationary camera. These continuous weights cap it at 1/9,
        // with no temporal history, stale maps or extra shadow renders.
        float2 coordinate=p.xy/texel-.5;
        float2 base=floor(coordinate),fraction=frac(coordinate);
        float4 wx=float4(1-fraction.x,1,1,fraction.x);
        float4 wy=float4(1-fraction.y,1,1,fraction.y);
        [unroll] for(int y=-1;y<=2;++y) [unroll] for(int x=-1;x<=2;++x) {
            float2 uv=(base+float2(x,y)+.5)*texel;
            // Compare at each texel's own receiver-plane depth BEFORE blending.
            // A single comparison depth for hardware bilinear PCF reintroduces
            // self-shadow bands on sloped receivers.
            float depth=p.z+dot(slope,uv-p.xy)-bias-dot(slopeError,abs(uv-p.xy));
            visibility+=PrivateSunWide.SampleCmpLevelZero(PrivateSunSampler,uv,depth)*wx[x+1]*wy[y+1]/9;
        }
    } else {
        // Keep the fine player/face map's existing sharper, continuous filter.
        float2 center=(floor(p.xy/texel)+.5)*texel;
        float2 fraction=frac(p.xy/texel);
        float3 wx=float3(.5*(1-fraction.x)*(1-fraction.x),.75-(fraction.x-.5)*(fraction.x-.5),.5*fraction.x*fraction.x);
        float3 wy=float3(.5*(1-fraction.y)*(1-fraction.y),.75-(fraction.y-.5)*(fraction.y-.5),.5*fraction.y*fraction.y);
        [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
            float2 uv=center+float2(x,y)*texel;
            float depth=p.z+dot(slope,uv-p.xy)-bias-dot(slopeError,abs(uv-p.xy));
            visibility+=PrivateSunDetail.SampleCmpLevelZero(PrivateSunSampler,uv,depth)*wx[x+1]*wy[y+1];
        }
    }
    covered=true;
    // Wide coverage ends gradually, with no cascade split tied to the viewer.
    float edge=min(min(p.x,p.y),min(1-p.x,1-p.y));
    return lerp(1,visibility,saturate((edge/texel.x-2)/16));
}
float PrivateSunVisibility(float3 world, PrivateSunReceiver receiver)
{
    if(PrivateSunContract.x!=MIRROR_PRIVATE_SUN_ABI || !PrivateSunContract.y) return 1;
    // The exact detached-player mask (or iris draw lease) identifies the
    // receiver; its cast shadow on world geometry remains independent.
    if(receiver.player && (uint(PrivateSunSampling.w)&1u)) return 1;
    float rangeWeight=1;
    if(PrivateSunRange.w>0) {
        if(!all(isfinite(PrivateSunRange)) || !all(isfinite(world))) return 1;
        float distance=length(world-PrivateSunRange.xyz);
        rangeWeight=saturate((PrivateSunRange.w-distance)/(PrivateSunRange.w*.1));
        if(rangeWeight<=0) return 1;
    }
    bool covered=false;
    if(receiver.player && PrivateSunContract.z) {
        float detail=PrivateSunMapVisibility(world,receiver,1,covered);
        if(covered) return lerp(1,detail,rangeWeight);
    }
    return lerp(1,PrivateSunMapVisibility(world,receiver,0,covered),rangeWeight);
}
bool PrivateSunPlayerPixel(uint2 pixel, float sceneDepth)
{
    if(PrivateSunContract.x!=MIRROR_PRIVATE_SUN_ABI || !PrivateSunContract.w) return false;
    uint width,height; PrivatePlayerDepth.GetDimensions(width,height);
    if(pixel.x>=width || pixel.y>=height) return false;
    float playerDepth=PrivatePlayerDepth.Load(int3(pixel,0));
    // Only this capture's detached player is submitted to this mask. Matching
    // depth identifies visible player pixels; world geometry cannot gain the
    // player map merely by entering a player-centred volume.
    return playerDepth>=0 && playerDepth<.999999 &&
        abs(playerDepth-sceneDepth)<=max(abs(sceneDepth)*2.3841858e-7,1.e-8);
}
#endif
