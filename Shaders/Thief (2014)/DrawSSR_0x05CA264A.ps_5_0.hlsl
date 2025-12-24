#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Normal map buffer
Texture2D<float4> t1 : register(t1); // Depth or something
Texture2D<float4> t2 : register(t2); // Some buffer with some highlights

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xyzw = t0.SampleLevel(s2_s, v0.xy, 0).xyzw;
  if (r0.w >= 0.01)
  {
    r0.xy = r0.xy * 2 - 1;
    r1.x = dot(r0.xy, r0.xy);
    r1.x = 1 - r1.x;
    r1.x = sqrt(r1.x);
    r1.y = t1.SampleLevel(s0_s, v0.xy, 0).x;
    r1.z = 1 - cb2[2].y;
    r1.y -= r1.z;
    r1.y = min(-1e-12, r1.y);
    r1.y = -cb2[2].x / r1.y;
    r2.xyz = v1.xyz * r1.y + cb0[5].xyz;
    r0.xy *= 64.0;
    r1.w = 1 - cb0[6].y;
    r3.xy = r1.w * r0.xy;
    r3.z = 0;
    r3.xyz += r2.xyz;
    r4.xyz = cb0[8].xyw * r2.y;
    r2.xyw = cb0[7].xyw * r2.x + r4.xyz;
    r2.xyz = cb0[9].xyw * r2.z + r2.xyw;
    r2.xyz = cb0[10].xyw + r2.xyz;
    r4.xyz = cb0[8].xyw * r3.y;
    r3.xyw = cb0[7].xyw * r3.x + r4.xyz;
    r3.xyz = cb0[9].xyw * r3.z + r3.xyw;
    r3.xyz = cb0[10].xyw + r3.xyz;
    r0.xy = r2.xy / r2.z;
    r2.xy = r3.xy / r3.z;
    r0.xy = r2.xy - r0.xy;
    r2.xy = v0.xy + r0.xy;
    r1.w = t1.SampleLevel(s0_s, r2.xy, 0).x;
    r1.z = r1.w - r1.z;
    r1.z = min(-1e-12, r1.z);
    r1.z = -cb2[2].x / r1.z;
    r1.y -= r1.z;
    r1.z *= 0.1;
    r0.z = r1.x - r0.z;
    r0.z = 100 * abs(r0.z);
    r0.z = min(1, r0.z);
    r0.xy *= r0.z;
    if (r1.z < r1.y)
    {
      r0.xy = 0.0;
    }
  }
  else
  {
    r0.xy = 0.0;
  }
  r1.xyzw = v0.xyxy + r0.xyxy;
  r2.xyzw = r1.zwzw + cb0[11].xyzw * 1.5;
  r3.x = t0.SampleLevel(s2_s, r2.xy, 0).w;
  r3.y = t0.SampleLevel(s2_s, r2.zw, 0).w;
  r2.xyzw = r1.zwzw - cb0[11].xyzw * 1.5;
  r3.z = t0.SampleLevel(s2_s, r2.xy, 0).w;
  r3.w = t0.SampleLevel(s2_s, r2.zw, 0).w;
  r2.xyzw = r3.xyzw * r3.xyzw;
  r0.xy = max(r2.xz, r2.yw);
  r0.y = max(r0.x, r0.y);
  r0.z = min(r2.z, r2.w);
  r0.x = min(r0.x, r0.z);
  r0.x -= r0.y;
  r3.xyzw = t2.Sample(s1_s, r1.zw).xyzw;
  r4.xyzw = r1.zwzw + cb0[11].xyzw * 0.5;
  r5.xyzw = t2.Sample(s1_s, r4.xy).xyzw;
  r4.xyzw = t2.Sample(s1_s, r4.zw).xyzw;
  r1.xyzw -= cb0[11].xyzw * 0.5;
  r6.xyzw = t2.Sample(s1_s, r1.xy).xyzw;
  r1.xyzw = t2.Sample(s1_s, r1.zw).xyzw;
  bool skipIt = false;
  if (abs(r0.x) >= 0.01)
  {
    r2.xyzw /= dot(r2, 1) + 0.001; // Sum normalization, where the sum of values equals 1.
    //r2.xyzw = clamp(r2.xyzw, 0.5, 1.5); // Test
    r4.xyzw *= r2.y;
    r4.xyzw += r2.x * r5.xyzw;
    r4.xyzw += r2.z * r6.xyzw;
    r3.xyzw = r2.w * r1.xyzw + r4.xyzw;

    // Luma
    float4 test = r4.xyzw;
    if (test.x != test.x || test.y != test.y || test.z != test.z || test.w != test.w) // NaN
    {
      // o0 = float4(10, 0, 0, 1);
      // return;
    }
  }
  else
  {
  }

  // Luma
  if (r3.x != r3.x || r3.y != r3.y || r3.z != r3.z || r3.w != r3.w) // NaN
  //if (r4.x != r4.x || r4.y != r4.y || r4.z != r4.z || r4.w != r4.w) // NaN
  //if (r1.x != r1.x || r1.y != r1.y || r1.z != r1.z || r1.w != r1.w) // NaN
  //if (r6.x != r6.x || r6.y != r6.y || r6.z != r6.z || r6.w != r6.w) // NaN
  //if (r5.x != r5.x || r5.y != r5.y || r5.z != r5.z || r5.w != r5.w) // NaN
  //if (r2.w != r2.w) // NaN
  {
    //skipIt = true;

    //o0 = float4(10, 0, 0, 1);
    //return;
  }
  
  r0.x = r0.w;
  r0.y = 1;
  r1.xyz = r3.xyz * r0.x;
  r0.x = r3.w * r0.y - 1;
  o0.w = saturate(r0.w * r0.x + 1);
  o0.xyz = r1.xyz;

  // Luma
  if (skipIt) // NaN
  {
    o0.xyz = float3(0, 0, 0);
    o0 = 0;
  }
  if (any(IsNaN_Strict(o0.xyz)))
  {
    //o0.xyz = float3(0, 0, 0);
  }
  //o0 = float4(1, 0, 0, 1);
  //o0.xyz = 0;
  //o0.xyz = saturate(o0.xyz);
}