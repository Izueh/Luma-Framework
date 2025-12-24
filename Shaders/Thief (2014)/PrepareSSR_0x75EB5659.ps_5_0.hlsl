#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Normal maps (for Specular objects only)
Texture2D<float4> t1 : register(t1); // Depth or something
Texture2D<float4> t2 : register(t2); // Previously prepared SSR step (looks like a mix of normal maps and specularity etc) (if we upgraded textures, this has huge values and needs clamping)
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4); // TODO: do we need to clamp these too?

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);
SamplerState s3_s : register(s3);
SamplerState s4_s : register(s4);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4;
  r0.x = t0.Sample(s3_s, v0.xy).w; // Note: w might go beyond 1 with upgraded textures, but it should be fine
  if (0.00999999978 < r0.x) {
    r0.yz = t0.SampleLevel(s3_s, v0.xy, 0).xy;
    r1.xy = r0.yz + r0.yz;
    r0.yz = r0.yz * 2 - 1;
    r0.y = dot(r0.yz, r0.yz);
    r0.y = 1 - r0.y;
    r1.z = sqrt(r0.y);
    r0.y = t1.SampleLevel(s0_s, v0.xy, 0).x;
    r0.z = 1 - cb2[2].y;
    r0.y -= r0.z;
    r0.y = min(-1e-12, r0.y);
    r0.y = -cb2[2].x / r0.y;
    r2.xyz = v1.xyz * r0.y;
    r3.xyz = v1.xyz * r0.y + cb0[10].xyz;
    r1.xyz = float3(-1,-1,-1) + r1.xyz;
    r1.xyz = cb0[9].y * r1.xyz + float3(0,0,1);
    r0.zw = t2.Sample(s2_s, v0.xy).xy;
    r0.zw = saturate(r0.zw); // Luma: fix out of bounds value
    r0.z = r0.w * 256 + r0.z;
    r0.z *= 255;
    r4.xyzw = t3.Sample(s1_s, v0.xy).xyzw;
    if (0.99 < r4.w)
    {
      r0.z = 0;
    }
    r0.w = dot(r2.xyz, r2.xyz);
    r2.xyw = r2.xyz * rsqrt(r0.w);
    r0.w = dot(r2.xyw, r1.xyz) * 2;
    r1.xyz = r1.xyz * -r0.w + r2.xyw;
    r0.w = r1.z * r0.z;
    r1.xyz = r0.z * r1.xyz + r3.xyz;
    r2.xyw = cb0[6].xyw * r1.y;
    r1.xyw = cb0[5].xyw * r1.x + r2.xyw;
    r1.xyw = cb0[7].xyw * r1.z + r1.xyw;
    r1.xyw = cb0[8].xyw + r1.xyw;
    r1.xy = r1.xy / r1.w;
    r2.xyw = cb0[6].xyw * r3.y;
    r2.xyw = cb0[5].xyw * r3.x + r2.xyw;
    r2.xyw = cb0[7].xyw * r3.z + r2.xyw;
    r2.xyw = cb0[8].xyw + r2.xyw;
    r2.xy = r2.xy / r2.w;
    r1.xy = r2.xy - r1.xy;
    r1.x = dot(r1.xy, r1.xy);
    o1.xy = sqrt(r1.x);
    r1.x = max(cb0[13].x, r2.z);
    r1.x = min(cb0[13].y, r1.x);
    r1.y = -cb0[13].z + r1.x;
    r1.y = cb0[13].w * abs(r1.y);
    r2.xyz = cb0[12].xyz - cb0[11].xyz;
    r3.xyz = r1.y * r2.xyz + cb0[11].xyz;
    r1.y = cb0[14].y - cb0[14].x;
    r0.y = saturate(r0.y / r1.y);
    r0.y = 1 - r0.y;
    r1.z = -cb0[10].z + r1.z;
    r1.z = max(cb0[13].x, r1.z);
    r1.z = min(cb0[13].y, r1.z);
    r1.w = -cb0[13].z + r1.z;
    r1.w = cb0[13].w * abs(r1.w);
    r2.xyz = r1.w * r2.xyz + cb0[11].xyz;
    r2.xyz -= r3.xyz;
    r2.xyz = cb0[14].z * r2.xyz + r3.xyz;
    r1.x = r1.z - r1.x;
    r0.z *= r1.x;
    r0.z /= r0.w;
    r0.y = cb0[14].w * r0.y;
    r0.z = saturate(r0.z / r1.y);
    r0.w = r0.y * r0.z;
    r0.y = r0.y * r0.z + 1;
    r0.y = r0.w / r0.y;
    r0.z = 1 - r4.w;

#if 0 // Luma
    if (IsNaN_Strict(r0.z))
    {
      r0.z = 0+1; // 1?
    }
    if (IsNaN_Strict(r0.y))
    {
      r0.y = 0;
    }
#endif
    
    r1.xyz = r2.xyz * r0.z;
    r0.w = t4.Sample(s4_s, v0.xy).w;
    r1.xyz = r1.xyz * r0.w - r4.xyz;
    r1.xyz = r0.y * r1.xyz + r4.xyz;
    o0.xyz = r1.xyz * r0.x;

    //if (r0.z >= 1) // Luma
    //{
    //  r0.z = 0;
    //}

    o0.w = r0.z;

#if 0 // Luma: fix NaN alpha blends
    if (IsNaN_Strict(o0.w))
    {
      o0.w = 0;
    }
    if (any(IsNaN_Strict(r1.rgb)))
    //if (r0.y != r0.y)
    {
      //o0.rgb = 0;
    }
#endif
#if 0 // Luma: fix NaN alpha blends
    o0.w = saturate(o0.w);
    o0.rgb = saturate(o0.rgb);
#endif

    o1.z = r0.x;
    o1.w = 1;
  }
  else
  {
    o0.xyzw = float4(0,0,0,0);
    o1.xyzw = float4(0,0,0,0);
  }
}