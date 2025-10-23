#include "Includes/Common.hlsl"

Texture2D<float4> t4 : register(t4); // Noise/Dither
Texture2D<float4> t3 : register(t3); // Previous TAA output (from this very shader)
Texture2D<float4> t2 : register(t2); // Scene
Texture2D<float2> t1 : register(t1); // Motion Vectors in UV space (32 bit float)
Texture2D<float4> t0 : register(t0); // Depth

SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[21];
}

#define cmp

float4 SafeSceneSample(float2 uv)
{
  float4 color = t2.Sample(s1_s, uv).rgba;
  if (IsNaN_Strict(color.x))
    color.x = 0.0;
  if (IsNaN_Strict(color.y))
    color.y = 0.0;
  if (IsNaN_Strict(color.z))
    color.z = 0.0;
  if (IsNaN_Strict(color.w))
    color.w = 0.0;
  // Note: we could fix invalid luminance here too but we already do it in the tonemapper
  return color;
}

// TAA
// The game relies on it as it has a lot of blue noise passes that would be noisy without TAA
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  r0.x = 0;
  r0.y = cb0[10].y;
  r0.w = cb0[11].y;
  r1.xy = v1.xy - r0.xy;
  r2.x = cb0[10].x;
  r2.yw = 0.0;
  r1.zw = -r2.xy + r1.xy;
  r3.xyzw = t0.Sample(s0_s, r1.zw).yzxw;
  r4.xyzw = t0.Sample(s0_s, r1.xy).yzxw;
  r1.xy = r2.xy + r1.xy;
  r1.xyzw = t0.Sample(s0_s, r1.xy).yzxw;
  r3.yw = v1.xy - r2.xy;
  r5.xyzw = t0.Sample(s0_s, r3.yw).yzxw;
  r6.xyzw = t0.Sample(s0_s, v1.xy).yzxw;
  r3.yw = v1.xy + r2.xy;
  r7.xyzw = t0.Sample(s0_s, r3.yw).yzxw;
  r0.yz = v1.xy + r0.xy;
  r3.yw = r0.yz - r2.xy;
  r8.xyzw = t0.Sample(s0_s, r3.yw).yzxw;
  r9.xyzw = t0.Sample(s0_s, r0.yz).yzxw;
  r0.yz = r0.yz + r2.xy;
  r10.xyzw = t0.Sample(s0_s, r0.yz).xyzw;
  r0.y = cmp(r4.z < r3.z);
  r4.xy = float2(0,-1);
  r3.x = -1;
  r3.xyz = r0.yyy ? r4.xyz : r3.xxz;
  r0.y = cmp(r1.z < r3.z);
  r1.xy = float2(1,-1);
  r1.xyz = r0.yyy ? r1.xyz : r3.xyz;
  r0.y = cmp(r5.z < r1.z);
  r5.xy = float2(-1,0);
  r1.xyz = r0.yyy ? r5.xyz : r1.xyz;
  r0.y = cmp(r6.z < r1.z);
  r6.x = 0;
  r1.xyz = r0.yyy ? r6.xxz : r1.xyz;
  r0.y = cmp(r7.z < r1.z);
  r7.xy = float2(1,0);
  r1.xyz = r0.yyy ? r7.xyz : r1.xyz;
  r0.y = cmp(r8.z < r1.z);
  r8.xy = float2(-1,1);
  r1.xyz = r0.yyy ? r8.xyz : r1.xyz;
  r0.y = cmp(r9.z < r1.z);
  r9.xy = float2(0,1);
  r1.xyz = r0.yyy ? r9.xyz : r1.xyz;
  r0.y = cmp(r10.x < r1.z);
  r0.yz = r0.yy ? float2(1,1) : r1.xy;
  r0.yz = cb0[10].xy * r0.yz + v1.xy;
  r1.xy = t1.Sample(s2_s, r0.yz).xy;
  r0.yz = -cb0[15].xy * cb0[11].xy + v1.xy; // Camera Jitters in CB0[15] * inv resolution CB0[11].xy (zw are direct resolution)
  r3.xyzw = SafeSceneSample(r0.yz).xyzw; // Dejittered sample!?
  r1.zw = v1.xy - r1.xy; // Subtract MVs to get the previous frame UV
  r4.xyzw = t3.Sample(s4_s, r1.zw).xyzw;
#if 1 // Prevent NaNs from the previous frame from spreading around the image
  if (IsNaN_Strict(r4.x))
    r4.x = 0.0;
  if (IsNaN_Strict(r4.y))
    r4.y = 0.0;
  if (IsNaN_Strict(r4.z))
    r4.z = 0.0;
  if (IsNaN_Strict(r4.w))
    r4.w = 0.0;
#elif 0 // Doesn't work as the compiler seemengly optimizes this away?
  if (any(isnan(r4.xyzw)))
  {
    r4.xyzw = 0;
  }
#endif

  r1.zw = r0.yz - r0.xw;
  r2.z = cb0[11].x;
  r2.xy = r1.zw - r2.zw;
  r5.xyzw = SafeSceneSample(r2.xy).xyzw;
  r6.xyzw = SafeSceneSample(r1.zw).xyzw;
  r1.zw = r1.zw + r2.zw;
  r7.xyzw = SafeSceneSample(r1.zw).xyzw;
  r1.zw = r0.yz - r2.zw;
  r8.xyzw = SafeSceneSample(r1.zw).xyzw;
  r1.zw = r0.yz + r2.zw;
  r9.xyzw = SafeSceneSample(r1.zw).xyzw;
  r0.xw = r0.yz + r0.xw;
  r1.zw = r0.xw - r2.zw;
  r10.xyzw = SafeSceneSample(r1.zw).xyzw;
  r11.xyzw = SafeSceneSample(r0.xw).xyzw;
  r0.xw = r0.xw + r2.zw;
  r2.xyzw = SafeSceneSample(r0.xw).xyzw;
  r12.xyzw = min(r11.xyzw, r2.xyzw);
  r12.xyzw = min(r12.xyzw, r10.xyzw);
  r12.xyzw = min(r12.xyzw, r9.xyzw);
  r12.xyzw = min(r12.xyzw, r3.xyzw);
  r12.xyzw = min(r12.xyzw, r8.xyzw);
  r12.xyzw = min(r12.xyzw, r7.xyzw);
  r12.xyzw = min(r12.xyzw, r6.xyzw);
  r12.xyzw = min(r12.xyzw, r5.xyzw);
  r13.xyzw = max(r11.xyzw, r2.xyzw);
  r13.xyzw = max(r13.xyzw, r10.xyzw);
  r13.xyzw = max(r13.xyzw, r9.xyzw);
  r13.xyzw = max(r13.xyzw, r3.xyzw);
  r13.xyzw = max(r13.xyzw, r8.xyzw);
  r13.xyzw = max(r13.xyzw, r7.xyzw);
  r13.xyzw = max(r13.xyzw, r6.xyzw);
  r13.xyzw = max(r13.xyzw, r5.xyzw);
  r0.x = r6.w + r5.w;
  r0.x = r0.x + r7.w;
  r0.x = r0.x + r8.w;
  r0.x = r0.x + r3.w;
  r0.x = r0.x + r9.w;
  r0.x = r0.x + r10.w;
  r0.x = r0.x + r11.w;
  r0.x = r0.x + r2.w;
  r2.xyzw = min(r11.xyzw, r9.xyzw);
  r2.xyzw = min(r3.xyzw, r2.xyzw);
  r2.xyzw = min(r8.xyzw, r2.xyzw);
  r2.xyzw = min(r6.xyzw, r2.xyzw);
  r5.xyzw = max(r11.xyzw, r9.xyzw);
  r5.xyzw = max(r5.xyzw, r3.xyzw);
  r5.xyzw = max(r8.xyzw, r5.xyzw);
  r5.xyzw = max(r6.xyzw, r5.xyzw);
  r0.w = r8.w + r6.w;
  r0.w = r0.w + r3.w;
  r0.w = r0.w + r9.w;
  r0.w = r0.w + r11.w;
  r0.w = 0.2 * r0.w;
  r2.xyzw = r12.xyzw + r2.xyzw;
  r2.xyzw = 0.5 * r2.xyzw;
  r5.xyzw = r13.xyzw + r5.xyzw;
  r1.z = 0.5 * r5.w;
  r0.x = r0.x * 0.111111112 + r0.w;
  r0.x = 0.5 * r0.x;
  r0.x = max(r0.x, r2.w);
  r6.w = min(r0.x, r1.z);
  r7.xyz = r5.xyz * float3(0.5,0.5,0.5) + r2.xyz;
  r6.xyz = float3(0.5,0.5,0.5) * r7.xyz;
  r2.xyz = r5.xyz * float3(0.5,0.5,0.5) - r2.xyz;
  r2.xyz = float3(0.5,0.5,0.5) * r2.xyz;
  r5.xyzw = -r6.xyzw + r4.xyzw;
  r2.xyz = r5.xyz / (r2.xyz >= 0.0 ? max(r2.xyz, FLT_EPSILON) : min(r2.xyz, -FLT_EPSILON)); // Luma: protected
  r0.x = max3(abs(r2.xyz));
  r0.w = cmp(1 < r0.x);
  r2.xyzw = r5.xyzw / max(r0.x, FLT_EPSILON); // Luma: protected
  r2.xyzw = r6.xyzw + r2.xyzw;
  r2.xyzw = r0.w ? r2.xyzw : r4.xyzw;
#if 1
  r4.xyz = cb0[3].xyz * r3.xyz;
#else // Attempted Luma fix that seems to be detrimental. It occasionally causes red bright dots, and possibly NaNs
  r4.xyz = cb0[3].xyz * clamp(r3.xyz, -FLT16_MAX, FLT16_MAX);
#endif
#if 0 // Test: ~passthrough
  o0.xyzw = r2.xyzw;
  o1.xyzw = r2.xyzw;
  return;
#endif

  r0.xw = r4.x + r4.yz;
  r0.x += r3.z * cb0[3].z;
  r0.w *= r4.y;
  r0.w = sqrt(abs(r0.w)) * sign(r0.w); // Luma: protected

  r0.w = dot(cb0[3].ww, r0.ww);
  r0.x = r0.x + r0.w;
  r4.xyz = cb0[3].xyz * r2.xyz;
  r1.zw = r4.x + r4.yz;
  r0.w = r2.z * cb0[3].z + r1.z;
  r1.z = r4.y * r1.w;
  r1.z = sqrt(abs(r1.z)) * sign(r1.z); // Luma: protected
  
  r1.z = dot(cb0[3].ww, r1.zz);
  r0.w = r1.z + r0.w;
  r1.z = r0.x - r0.w;
  r0.x = max(r0.x, max(r0.w, 0.2)); // Doesn't seem to raise blacks so it's ok
  r0.x = abs(r1.z) / r0.x; // Already safe
  r0.x = 1 - r0.x;
  r0.x = r0.x * r0.x;
  r0.w = cb0[20].y - cb0[20].x;
  r0.x = r0.x * r0.w + cb0[20].x;
  r2.xyzw = r2.xyzw - r3.xyzw;
  r2.xyzw = r0.x * r2.xyzw + r3.xyzw;
  r0.xw = v1.xy * cb0[11].zw + cb0[13].xy; // Noise offset in the CB0[13]
  r0.xw = cb0[12].xy * r0.xw;
  float4 noise = t4.SampleLevel(s3_s, r0.xw, 0).xyzw; // Defaults to white? Likely dithering

  int interations = 7;
#if _A141EA3E
  interations = 7;
#elif _DEBF1AC4
  interations = 5;
#endif

  r0.xw = cb0[20].z * r1.xy;
  r1.xy = r0.xw / cb0[11].xy;
  r1.x = length(r1.xy); // MVs length
  r1.x -= 2.0;
  r1.x = max(0.0, r1.x);
  r1.x = min(13.0, r1.x);
  r1.x = -r1.x * 0.0769230798 + 1.0;
  r1.y = -0.5 + noise.x;
  r0.xw *= 1.0 / float(interations - 1);
  r0.yz = r0.xw * r1.y + r0.yz;

  float4 blurredColor = 0.0;
  // Motion blur
  int i = -interations / 2;
  while (true) {
    if (i > (interations / 2)) break;
    float2 uv = float(i) * r0.xw + r0.yz;
    blurredColor += SafeSceneSample(uv).xyzw;
    i++;
  }
  blurredColor /= float(interations);
#if 0 // Test: ~passthrough
  o0.xyzw = blurredColor * 1;
  o1.xyzw = blurredColor * 1;
  return;
#endif

  // TODO: why does NaN happen? There's no unsafe division nor pow/sqrt/log/exp in here
  r2 = IsNaN_Strict(r2) ? 0.0 : r2; // Luma: protect against NaNs on output too

  r0 = lerp(blurredColor, r2, r1.x); // Blur percentage (flipped)
  noise = (noise * 2.0) - 1.0; // From 0|1 to -1|1
  noise /= 255.0; // Turn to 8 bit dithering, the game's rendering was all SDR and mostly 8 bit (with some 10 bit passes) so it'd band a lot
#if !ENABLE_DITHER
  noise = 0.0;
#endif

  // Output two similar colors, index 0 is the history (only read again the next frame, by this very shader) (hence it's not blurred at the end), index 1 is the actual output that continues to be used for this frame's post processing
  o0.xyzw = (r2.xyzw + noise); // Luma: removed saturate
  o1.xyzw = (r0.xyzw + noise); // Luma: removed saturate
}