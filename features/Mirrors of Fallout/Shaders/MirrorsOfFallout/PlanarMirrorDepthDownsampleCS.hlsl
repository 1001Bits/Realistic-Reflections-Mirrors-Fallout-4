// Present-time parallax source for the optical mirror. The committed pane image is
// re-sampled every frame from the viewer's CURRENT reflected eye by marching this
// depth in PlanarMirrorOverlayCS; a 4x-per-axis reduction of the capture depth is
// plenty for that march and keeps the per-publication copy small. The nearest
// sample of each block wins so the march stops at the front-most surface.

Texture2D<float> CaptureDepth : register(t0);
RWTexture2D<float> ReducedDepth : register(u0);

static const uint kDivisor = 4;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint width = 0;
	uint height = 0;
	ReducedDepth.GetDimensions(width, height);
	if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
		return;
	uint sourceWidth = 0;
	uint sourceHeight = 0;
	CaptureDepth.GetDimensions(sourceWidth, sourceHeight);
	if (sourceWidth == 0 || sourceHeight == 0)
	{
		ReducedDepth[dispatchThreadID.xy] = 1.0;
		return;
	}
	uint2 base = dispatchThreadID.xy * kDivisor;
	uint2 last = uint2(sourceWidth - 1, sourceHeight - 1);
	float nearest = 1.0;
	[unroll]
	for (uint y = 0; y < kDivisor; ++y)
	{
		[unroll]
		for (uint x = 0; x < kDivisor; ++x)
		{
			uint2 pixel = min(base + uint2(x, y), last);
			nearest = min(nearest, CaptureDepth.Load(int3(pixel, 0)));
		}
	}
	ReducedDepth[dispatchThreadID.xy] = nearest;
}
