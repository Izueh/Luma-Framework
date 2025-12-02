cbuffer cbDOFConsts : register(b1)
{
  float4 DOFConsts[7] : packoffset(c0);
}

SamplerState RenderTarget_s : register(s0);
SamplerState DepthTexture_s : register(s1);
SamplerState NearDOFMaskTexture_s : register(s2);
SamplerState HeatHazeTexture_s : register(s3);
Texture2D<float4> RenderTarget : register(t0);
Texture2D<float4> DepthTexture : register(t1);
Texture2D<float4> NearDOFMaskTexture : register(t2);
Texture2D<float4> HeatHazeTexture : register(t3);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4;
  int4 r0i, r1i;
  r0i.x = (int)DOFConsts[5].z;
  r0.y = dot(v2.xyz, v2.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v2.xzy * r0.yyy;
  r1.x = saturate(r0.w);
  r1.x = r1.x * r1.x;
  r1.y = r1.x * DOFConsts[2].x + 1;
  r2.xyz = float3(0,0,0);
  r1.zw = float2(0,0);
  r1i.w = 0;
  while (true) {
    if (r1i.w >= r0i.x) break;
    r3.xy = DOFConsts[5 + r1i.w].xy + v1.xy;
    r4.xyz = RenderTarget.Sample(RenderTarget_s, r3.xy).xyz;
#if 1 // Luma
    r4.xyz = max(r4.xyz, 0.0); // Fix Nans and remove negative values (they'd be trash)
#endif
    r2.xyz = DOFConsts[5].www * r4.xyz + r2.xyz;
    r2.w = DepthTexture.Sample(DepthTexture_s, r3.xy).x;
    r2.w = r2.w * DOFConsts[0].x + DOFConsts[0].y;
    r2.w = 1 / r2.w;
    r1.z = DOFConsts[5].w * r2.w + r1.z;
    r1i.w++;
  }
  o0.xyz = r2.xyz;
  r0.x = saturate(r1.z * DOFConsts[1].x + -DOFConsts[1].y);
  r0.x = DOFConsts[2].z * r0.x;
  r0.x = saturate(r0.x / r1.y);
  r1.y = NearDOFMaskTexture.SampleLevel(NearDOFMaskTexture_s, v1.xy, 0).x;
  r1.y = 1.5 * r1.y;
  r1.y = rsqrt(r1.y);
  r1.y = 1 / r1.y;
  r1.w = min(abs(r0.y), abs(r0.z));
  r2.x = max(abs(r0.y), abs(r0.z));
  r2.x = 1 / r2.x;
  r1.w = r2.x * r1.w;
  r2.x = r1.w * r1.w;
  r2.y = r2.x * 0.0208350997 + -0.0851330012;
  r2.y = r2.x * r2.y + 0.180141002;
  r2.y = r2.x * r2.y + -0.330299497;
  r2.x = r2.x * r2.y + 0.999866009;
  r2.y = r2.x * r1.w;
  r2.z = cmp(abs(r0.z) < abs(r0.y));
  r2.y = r2.y * -2 + 1.57079637;
  r2.y = r2.z ? r2.y : 0;
  r1.w = r1.w * r2.x + r2.y;
  r2.x = cmp(r0.z < -r0.z);
  r2.x = r2.x ? -3.141593 : 0;
  r1.w = r2.x + r1.w;
  r2.x = min(r0.y, r0.z);
  r0.y = max(r0.y, r0.z);
  r0.z = cmp(r2.x < -r2.x);
  r0.y = cmp(r0.y >= -r0.y);
  r0.y = r0.y ? r0.z : 0;
  r0.y = r0.y ? -r1.w : r1.w;
  r0.y = 3.14159012 + r0.y;
  r2.x = 0.159155071 * r0.y;
  r0.y = abs(r0.w) * 0.5 + 0.5;
  r2.y = r0.w * r0.y;
  r0.y = saturate(DOFConsts[4].z * r1.z);
  r0.z = r1.x * DOFConsts[4].y + 1;
  r0.z = 1 / r0.z;
  r2.xyzw = -DOFConsts[3].xyzw + r2.xyxy;
  r2.xyzw = float4(8,3,25,8) * r2.xyzw;
  r1.xz = HeatHazeTexture.Sample(HeatHazeTexture_s, r2.zw).yw;
  r1.xz = float2(-0.5,-0.5) + r1.xz;
  r3.yz = float2(0.75,0.75) * r1.xz;
  r1.xzw = HeatHazeTexture.Sample(HeatHazeTexture_s, r2.xy).xyw;
  r3.w = -0;
  r1.xzw = r1.xzw + r3.wyz;
  r1.xzw = float3(0,-0.5,-0.5) + r1.xzw;
  r0.w = dot(r1.zw, r1.zw);
  r0.w = sqrt(r0.w);
  r0.w = r0.z * 0.3 + r0.w;
  r0.y = r0.w * r0.y;
  r0.y = r0.y * r0.z;
  r0.y = DOFConsts[4].w * r0.y;
  r0.y = saturate(r0.y * r1.x);
  r0.x = r1.y + r0.x;
  r0.y = saturate(DOFConsts[2].y + r0.y);
  r0.x = r0.x + r0.y;
  o1.xyzw = min(float4(1,1,1,1), r0.xxxx);
  o0.w = 1;
}