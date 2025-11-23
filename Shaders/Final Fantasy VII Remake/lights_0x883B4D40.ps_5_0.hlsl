#include "includes/Common.hlsl"

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture3D<float4> t0 : register(t0);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
   float4 cb3[3];
}

cbuffer cb2 : register(b2)
{
   float4 cb2[22];
}

cbuffer cb1 : register(b1)
{
   float4 cb1[12];
}

cbuffer cb0 : register(b0)
{
   float4 cb0[154];
}

// 3Dmigoto declarations
#define cmp -

void main(float4 v0: TEXCOORD0, float4 v1: TEXCOORD3, float4 v2: TEXCOORD6, float4 v3: TEXCOORD7,
          float4 v4: VertexContextVector0, float4 v5: SV_Position0, uint v6: SV_IsFrontFace0, out float4 o0: SV_Target0,
          out float4 o1: SV_Target1, out float4 o2: SV_Target2, out float4 o3: SV_Target3, out float4 o4: SV_Target4,
          out float4 o5: SV_Target5)
{
   float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xyz = v3.zxy * v2.yzx;
   r0.xyz = v3.yzx * v2.zxy + -r0.xyz;
   r0.xyz = v3.www * r0.xyz;
   r0.w = 0.5 + v4.w;
   r0.w = floor(r0.w);
   r0.w = (int)r0.w;
   r1.xyzw = cb0[37].xyzw * v5.yyyy;
   r1.xyzw = v5.xxxx * cb0[36].xyzw + r1.xyzw;
   r1.xyzw = v5.zzzz * cb0[38].xyzw + r1.xyzw;
   r1.xyzw = cb0[39].xyzw + r1.xyzw;
   r1.xyz = r1.xyz / r1.www;
   r1.w = dot(-r1.xyz, -r1.xyz);
   r1.w = rsqrt(r1.w);
   r2.xyz = -r1.xyz * r1.www;
   r1.w = asuint(cb0[139].z) << 3;
   r3.z = (int)r0.w + (int)r1.w;
   r3.xy = (int2)v5.xy;
   r3.xyz = (int3)r3.xyz & int3(63, 63, 63);
   r3.w = 0;
   r3.xyz = t0.Load(r3.xyzw).xyz;
   r4.xy = t2.Sample(s1_s, v0.xy).xy;
   r4.xy = r4.xy * float2(2, 2) + float2(-1, -1);
   r0.w = dot(r4.xy, r4.xy);
   r0.w = 1 + -r0.w;
   r0.w = max(0, r0.w);
   r4.z = sqrt(r0.w);
   r0.w = dot(r4.xyz, r4.xyz);
   r0.w = rsqrt(r0.w);
   r4.xyz = r4.xyz * r0.www;
   r4.xyz = r4.xyz * cb0[134].www + cb0[134].xyz;
   r0.w = dot(r4.xyz, r4.xyz);
   r0.w = rsqrt(r0.w);
   r4.xyz = r4.xyz * r0.www;
   r0.xyz = r4.yyy * r0.xyz;
   r0.xyz = r4.xxx * v2.xyz + r0.xyz;
   r0.xyz = r4.zzz * v3.xyz + r0.xyz;
   r0.w = dot(r0.xyz, r0.xyz);
   r0.w = rsqrt(r0.w);
   r0.xyz = r0.xyz * r0.www;
   r4.xyz = cb3[2].www * cb3[2].xyz;
   r5.xyz = t3.Sample(s2_s, v0.xy).xyz;
   r4.xyz = r4.xyz * r5.zzz + cb3[1].xyz;
   r6.xyz = t4.Sample(s3_s, v0.xy).xyz;
   r1.w = t5.Sample(s4_s, v0.zw).x;
   r6.xyz = saturate(r6.xyz);
   o3.w = saturate(r1.w);
   r5.xy = saturate(r5.xy);
   r1.w = r5.y * cb0[135].y + cb0[135].x;
   r4.xyz = max(float3(0, 0, 0), r4.xyz);
   r2.w = r3.x * 0.99609375 + 0.001953125;
   r2.w = cmp(r2.w < 0);
   r2.w = r2.w ? 0.000000 : 0;
   r5.yzw = asint(cb1[11].xxx) & int3(1, 2, 4);
   r5.zw = r5.zw ? float2(8.96831017e-44, 1.79366203e-43) : float2(0, 0);
   r2.w = (int)r2.w + (int)r5.z;
   r2.w = (int)r5.w + (int)r2.w;
   r7.xyz = ddx_coarse(r0.xyz);
   r3.w = dot(r7.xyz, r7.xyz);
   r7.xyz = ddy_coarse(r0.xyz);
   r4.w = dot(r7.xyz, r7.xyz);
   r3.w = r4.w + r3.w;
   r3.w = 0.5 * r3.w;
   r3.w = min(0.180000007, r3.w);
   r1.w = r1.w * r1.w + r3.w;
   r1.w = min(1, r1.w);
   r1.w = sqrt(r1.w);
   r7.xyz = v3.xyz * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
   r3.w = 1 + -r5.x;
   r8.xyz = r3.www * r6.xyz;
   r9.xyz = float3(-0.0399999991, -0.0399999991, -0.0399999991) + r6.xyz;
   r9.xyz = r5.xxx * r9.xyz + float3(0.0399999991, 0.0399999991, 0.0399999991);
   r8.xyz = r8.xyz * cb0[132].www + cb0[132].xyz;
   r9.xyz = r9.xyz * cb0[133].www + cb0[133].xyz;
   r8.xyz = r9.xyz * float3(0.449999988, 0.449999988, 0.449999988) + r8.xyz;
   r5.zw = float2(1, 0.5) * v1.xy;
   r10.xy = v1.xy * float2(1, 0.5) + float2(0, 0.5);
   r11.xyzw = t1.Sample(s0_s, r5.zw).xyzw;
   r10.xyzw = t1.Sample(s0_s, r10.xy).xyzw;
   r3.w = r10.w * 0.00392156886 + r11.w;
   r3.w = -0.00196078443 + r3.w;
   r3.w = r3.w * cb2[18].w + cb2[20].w;
   r11.xyz = r11.xyz * r11.xyz;
   r11.xyz = r11.xyz * cb2[18].xyz + cb2[20].xyz;
   r3.w = exp2(r3.w);
   r3.w = -0.0185813606 + r3.w;
   r3.w = max(0, r3.w);
   r11.xyz = r3.www * r11.xyz;
   r10.xyzw = r10.zxyw * cb2[19].zxyw + cb2[21].zxyw;
   r0.w = 1;
   r0.w = dot(r10.xyzw, r0.xyzw);
   r0.w = max(0, r0.w);
   r3.w = max(0, r10.w);
   r10.xyz = r11.xyz * r0.www;
   r11.xyz = r11.xyz * r3.www;
   r10.w = dot(r11.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r11.w = dot(cb0[153].xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r11.xyz = cb0[153].xyz;
   r10.xyzw = r11.xyzw * r10.xyzw;
   r0.w = dot(r0.xyz, r2.xyz);
   r0.w = max(9.99999975e-06, abs(r0.w));
   r2.x = dot(r9.xyz, float3(0.333333343, 0.333333343, 0.333333343));
   r9.xyzw = r1.wwww * float4(-1, -0.0274999999, -0.572000027, 0.0219999999) +
             float4(1, 0.0425000004, 1.03999996, -0.0399999991);
   r2.y = r9.x * r9.x;
   r0.w = -9.27999973 * r0.w;
   r0.w = exp2(r0.w);
   r0.w = min(r2.y, r0.w);
   r0.w = r0.w * r9.x + r9.y;
   r2.yz = r0.ww * float2(-1.03999996, 1.03999996) + r9.zw;
   r0.w = dot(r2.xxx, float3(0.333333343, 0.333333343, 0.333333343));
   r2.x = saturate(50 * r0.w);
   r2.x = r2.x * r2.z;
   r0.w = saturate(r0.w * r2.y + r2.x);
   r0.w = 1 + -r0.w;
   r0.w = r0.w * r0.w;
   r0.w = r5.x * -r0.w + r0.w;
   r4.xyz = r4.xyz * r0.www;
   r0.w = cmp(0 < cb0[140].z);
   r8.w = dot(r8.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r4.w = 0;
   r4.xyzw = r0.wwww ? r8.xyzw : r4.xyzw;
   r0.w = cmp(0 < cb0[136].x);
   if (r0.w != 0)
   {
      r1.xyz = -cb0[62].xyz + r1.xyz;
      r2.xyz = -cb1[8].xyz + r1.xyz;
      r8.xyz = float3(1, 1, 1) + cb1[9].xyz;
      r2.xyz = cmp(r8.xyz < abs(r2.xyz));
      r0.w = (int)r2.y | (int)r2.x;
      r0.w = (int)r2.z | (int)r0.w;
      r1.x = dot(r1.xyz, float3(0.577000022, 0.577000022, 0.577000022));
      r1.x = 0.00200000009 * r1.x;
      r1.x = frac(r1.x);
      r1.x = cmp(0.5 < r1.x);
      r8.xyz = r1.xxx ? float3(0, 1, 1) : float3(1, 1, 0);
      r8.w = dot(r8.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
      r4.xyzw = r0.wwww ? r8.xyzw : r4.xyzw;
   }

   // FIX: Firefly Mitigation (Global Reinhard Compression) - ONLY FOR DLSS (SRType == 0)
   if (true)
   {
      float maxRawDiff = max(r4.r, max(r4.g, r4.b));
      float limit = 10000.0;

      // Standard Reinhard-style compression: x * (Limit / (x + Limit))
      // Smoothly maps [0, Infinity] -> [0, Limit]
      float compressionFactor = limit / (maxRawDiff + limit + 0.000001);
      r4.rgb = r4.rgb * compressionFactor;
   }

   o0.xyzw = cb0[128].xxxx * r4.xyzw;

   r0.xyz = r0.xyz * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
   o1.w = r5.y ? 1 : 0.666666687;
   r0.w = (int)r2.w + 1;
   r0.w = (uint)r0.w;
   o2.w = 0.00392156886 * r0.w;

   // FIX: Firefly Mitigation for Specular (r10) - ONLY FOR DLSS (SRType == 0)
   if (true)
   {
      float maxRawSpec = max(r10.r, max(r10.g, r10.b));
      float limit = 8000.0;

      float compressionFactor = limit / (maxRawSpec + limit + 0.000001);
      r10.rgb = r10.rgb * compressionFactor;
   }

   o5.xyzw = cb0[128].xxxx * r10.xyzw;

   r2.xyzw = r3.xyzx * float4(2, 2, 2, 2) + float4(-1, -1, -1, -1);
   r3.xyzw = cmp(float4(0, 0, 0, 0) < r2.wyzw);
   r4.xyzw = cmp(r2.wyzw < float4(0, 0, 0, 0));
   r3.xyzw = (int4)-r3.xyzw + (int4)r4.xyzw;
   r3.xyzw = (int4)r3.xyzw;
   r2.xyzw = float4(1, 1, 1, 1) + -abs(r2.xyzw);
   r2.xyzw = sqrt(r2.xyzw);
   r2.xyzw = float4(1, 1, 1, 1) + -r2.xyzw;
   r2.xyzw = r3.xyzw * r2.xyzw;
   r1.xyz = r0.xyz * float3(2, 2, 2) + float3(-1, -1, -1);
   r1.xyz = float3(-0.998044968, -0.998044968, -0.998044968) + abs(r1.xyz);
   r1.xyz = cmp(r1.xyz < float3(0, 0, 0));
   r3.xyz = r2.xyz * float3(0.000977517106, 0.000977517106, 0.000977517106) + r0.xyz;
   o1.xyz = saturate(r1.xyz ? r3.xyz : r0.xyz);
   r0.xyz = r7.xyz * float3(2, 2, 2) + float3(-1, -1, -1);
   r0.xyz = float3(-0.992156863, -0.992156863, -0.992156863) + abs(r0.xyz);
   r0.xyz = cmp(r0.xyz < float3(0, 0, 0));
   r0.w = r2.w * 0.00392156886 + r7.x;
   o4.x = saturate(r0.x ? r0.w : r7.x);
   r0.xw = r2.yz * float2(0.00392156886, 0.00392156886) + r7.yz;
   o4.yz = saturate(r0.yz ? r0.xw : r7.yz);
   o2.x = r5.x;
   o2.y = 0.5;
   o2.z = r1.w;
   o3.xyz = r6.xyz;
   o4.w = 0;
   return;
}
