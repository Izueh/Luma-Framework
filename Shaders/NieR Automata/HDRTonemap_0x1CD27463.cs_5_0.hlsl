cbuffer cb : register(b0)
{
  float4 maxTVoutputNits : packoffset(c0); // x is 0.1, yz are your swapchain resolution, w is 1 (at least for my monitor???)
}

Texture2D<float4> g_Rec709 : register(t1);
RWTexture2D<float4> g_ST2084 : register(u0);

#define cmp

[numthreads(16, 4, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3;
  r0.xyz = g_Rec709.Load(int3(vThreadID.xy, 0)).xyz;
#if 0 // Skip BT.2020 conversion and all the other display mapping stuff
  r1.x = dot(float3(0.627403975,0.329281986,0.0433136001), r0.xyz);
  r1.y = dot(float3(0.0690969974,0.919539988,0.0113612004), r0.xyz);
  r1.z = dot(float3(0.0163915996,0.088013202,0.895595014), r0.xyz);

  r0.xyz = r1.xyz * 0.02 - 0.02;
  r0.xyz = saturate(r0.xyz * 1.02040815);
  r2.xy = float2(-0.02,-1) + maxTVoutputNits.xw;
  r2.xzw = r0.xyz * r2.xxx + 0.02;
  r0.xyz = ceil(r0.xyz);
  r2.xzw = -r1.xyz * 0.02 + r2.xzw;
  r1.xyz = 0.02 * r1.xyz;
  r0.xyz = r0.xyz * r2.xzw + r1.xyz;
  r0.w = maxTVoutputNits.z * 1.77777777 - r2.y;
  r0.w = maxTVoutputNits.y - r0.w;
  r1.x = 0.5 * r0.w;
  r0.w = -r0.w * 0.5 + maxTVoutputNits.y;
  r1.yz = (uint2)vThreadID.yx; // utof
  r1.x = cmp(r1.z < r1.x);
  r0.w = cmp(r0.w < r1.z);
  r0.w = asfloat(asint(r0.w) | asint(r1.x));
  r1.xzw = r0.www ? 0.0 : r0.xyz;
  r0.w = maxTVoutputNits.y / maxTVoutputNits.z;
  r2.x = cmp(1.77777777 < r0.w);
  r2.zw = cmp(r0.ww < float2(1.77777777,1.25));
  r1.xzw = r2.xxx ? r1.xzw : r0.xyz;
  r3.xy = float2(0.5625,0.703125) * maxTVoutputNits.yz;
  r0.w = r2.w ? r3.y : r3.x;
  r0.w = r0.w - r2.y;
  r0.w = maxTVoutputNits.z - r0.w;
  r2.x = 0.5 * r0.w;
  r0.w = -r0.w * 0.5 + maxTVoutputNits.z;
  r0.w = cmp(r0.w < r1.y);
  r1.y = cmp(r1.y < r2.x);
  r0.w = asfloat(asint(r0.w) | asint(r1.y));
  r2.xyw = r0.www ? float3(0,0,0) : r0.xyz;
  r1.xyz = r2.zzz ? r2.xyw : r1.xzw;
  r0.w = cmp(0 < maxTVoutputNits.w);
  r0.xyz = r0.www ? r1.xyz : r0.xyz;
#endif
  r0.w = 1;
  g_ST2084[vThreadID.xy] = r0.xyzw;
}