#include "includes/Common.hlsl"

Texture3D<float4> t8 : register(t8);

Texture3D<float4> t7 : register(t7);

Texture3D<float4> t6 : register(t6);

Texture3D<float4> t5 : register(t5);

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

cbuffer cb4 : register(b4)
{
   float4 cb4[3];
}

cbuffer cb3 : register(b3)
{
   float4 cb3[6];
}

cbuffer cb2 : register(b2)
{
   float4 cb2[12];
}

cbuffer cb1 : register(b1)
{
   float4 cb1[154];
}

cbuffer cb0 : register(b0)
{
   float4 cb0[16];
}

// 3Dmigoto declarations
#define cmp -

void main(float4 v0: TEXCOORD0, float4 v1: TEXCOORD1, float4 v2: TEXCOORD5, float4 v3: TEXCOORD6,
          float4 v4: VertexContextVector0, float4 v5: SV_Position0, uint v6: SV_IsFrontFace0, out float4 o0: SV_Target0,
          out float4 o1: SV_Target1, out float4 o2: SV_Target2, out float4 o3: SV_Target3, out float4 o4: SV_Target4,
          out float4 o5: SV_Target5)
{
   float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xyz = v3.zxy * v2.yzx;
   r0.xyz = v3.yzx * v2.zxy + -r0.xyz;
   r0.xyz = v3.www * r0.xyz;
   r0.w = 0.5 + v4.w;
   r0.w = floor(r0.w);
   r0.w = (int)r0.w;
   r1.xyzw = cb1[37].xyzw * v5.yyyy;
   r1.xyzw = v5.xxxx * cb1[36].xyzw + r1.xyzw;
   r1.xyzw = v5.zzzz * cb1[38].xyzw + r1.xyzw;
   r1.xyzw = cb1[39].xyzw + r1.xyzw;
   r1.xyz = r1.xyz / r1.www;
   r2.xyz = -cb1[62].xyz + r1.xyz;
   r1.w = dot(-r1.xyz, -r1.xyz);
   r1.w = rsqrt(r1.w);
   r1.xyz = -r1.xyz * r1.www;
   r1.w = asuint(cb1[139].z) << 3;
   r3.z = (int)r0.w + (int)r1.w;
   r3.xy = (int2)v5.xy;
   r3.xyz = (int3)r3.xyz & int3(63, 63, 63);
   r3.w = 0;
   r3.xyz = t0.Load(r3.xyzw).xyz;
   r4.xy = t1.Sample(s1_s, v0.xy).xy;
   r4.xy = r4.xy * float2(2, 2) + float2(-1, -1);
   r0.w = dot(r4.xy, r4.xy);
   r0.w = 1 + -r0.w;
   r0.w = max(0, r0.w);
   r4.z = sqrt(r0.w);
   r0.w = dot(r4.xyz, r4.xyz);
   r0.w = rsqrt(r0.w);
   r4.xyz = r4.xyz * r0.www;
   r4.xyz = r4.xyz * cb1[134].www + cb1[134].xyz;
   r0.w = dot(r4.xyz, r4.xyz);
   r0.w = rsqrt(r0.w);
   r4.xyz = r4.xyz * r0.www;
   r0.xyz = r4.yyy * r0.xyz;
   r0.xyz = r4.xxx * v2.xyz + r0.xyz;
   r0.xyz = r4.zzz * v3.xyz + r0.xyz;
   r0.w = dot(r0.xyz, r0.xyz);
   r0.w = rsqrt(r0.w);
   r0.xyz = r0.xyz * r0.www;
   r4.xyz = cb4[2].www * cb4[2].xyz;
   r5.xyz = t2.Sample(s2_s, v0.xy).xyz;
   r4.xyz = r4.xyz * r5.zzz + cb4[1].xyz;
   r6.xyz = t3.Sample(s3_s, v0.xy).xyz;
   r0.w = t4.Sample(s4_s, v1.xy).x;
   r7.xyz = saturate(r2.xyz * cb0[10].xyz + cb0[11].xyz);
   r7.xyz = cb0[12].xyz * r7.xyz;
   r6.xyz = saturate(r6.xyz);
   o3.w = saturate(r0.w);
   r5.xy = saturate(r5.xy);
   r0.w = r5.y * cb1[135].y + cb1[135].x;
   r4.xyz = max(float3(0, 0, 0), r4.xyz);
   r1.w = cmp(0 < cb2[11].y);
   r1.w = r1.w ? cb3[5].x : 1;
   r2.w = t8.SampleLevel(s0_s, r7.xyz, 0).x;
   r1.w = r2.w * r1.w;
   r2.w = r3.x * 0.99609375 + 0.001953125;
   r1.w = cmp(r2.w < r1.w);
   r1.w = r1.w ? 0.000000 : 0;
   r5.yzw = asint(cb2[11].xxx) & int3(1, 2, 4);
   r5.zw = r5.zw ? float2(8.96831017e-44, 1.79366203e-43) : float2(0, 0);
   r1.w = (int)r1.w + (int)r5.z;
   r1.w = (int)r5.w + (int)r1.w;
   r8.xyz = ddx_coarse(r0.xyz);
   r2.w = dot(r8.xyz, r8.xyz);
   r8.xyz = ddy_coarse(r0.xyz);
   r3.w = dot(r8.xyz, r8.xyz);
   r2.w = r3.w + r2.w;
   r2.w = 0.5 * r2.w;
   r2.w = min(0.180000007, r2.w);
   r0.w = r0.w * r0.w + r2.w;
   r0.w = min(1, r0.w);
   r0.w = sqrt(r0.w);
   r8.xyz = v3.xyz * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
   r2.w = 1 + -r5.x;
   r9.xyz = r2.www * r6.xyz;
   r10.xyz = float3(-0.0399999991, -0.0399999991, -0.0399999991) + r6.xyz;
   r10.xyz = r5.xxx * r10.xyz + float3(0.0399999991, 0.0399999991, 0.0399999991);
   r9.xyz = r9.xyz * cb1[132].www + cb1[132].xyz;
   r10.xyz = r10.xyz * cb1[133].www + cb1[133].xyz;
   r9.xyz = r10.xyz * float3(0.449999988, 0.449999988, 0.449999988) + r9.xyz;
   r11.xyz = t5.SampleLevel(s0_s, r7.xyz, 0).xyz;
   r12.xyz = float3(0.318309873, 0.318309873, 0.318309873) * r11.xyz;
   r13.xyz = t6.SampleLevel(s0_s, r7.xyz, 0).xyz;
   r13.xyz = float3(0.318309873, 0.318309873, 0.318309873) * r13.xyz;
   r7.xyz = t7.SampleLevel(s0_s, r7.xyz, 0).xyz;
   r7.xyz = float3(0.318309873, 0.318309873, 0.318309873) * r7.xyz;
   r2.w = dot(r12.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r2.w = max(9.99999975e-05, r2.w);
   r14.xyz = r12.xyz / r2.www;
   r15.xyz = saturate(r0.xyz);
   r15.xyz = r15.xyz * r15.xyz;
   r16.xyz = saturate(-r0.xyz);
   r16.xyz = r16.xyz * r16.xyz;
   r2.w = dot(r13.xyz, r15.xyz);
   r3.w = dot(r7.xyz, r16.xyz);
   r2.w = r3.w + r2.w;
   r7.xyz = r14.xyz * r2.www;
   r2.w = cmp(0 < cb0[13].w);
   if (r2.w != 0)
   {
      r2.w = dot(cb0[13].xyz, float3(0.212599993, 0.715200007, 0.0722000003));
      r2.w = max(9.99999975e-05, r2.w);
      r13.xyz = cb0[13].xyz / r2.www;
      r2.w = dot(cb0[14].xyz, r15.xyz);
      r3.w = dot(cb0[15].xyz, r16.xyz);
      r2.w = r3.w + r2.w;
      r11.xyz = -r11.xyz * float3(0.318309873, 0.318309873, 0.318309873) + cb0[13].xyz;
      r12.xyz = cb0[13].www * r11.xyz + r12.xyz;
      r11.xyz = r13.xyz * r2.www + -r7.xyz;
      r7.xyz = cb0[13].www * r11.xyz + r7.xyz;
   }
   r7.w = dot(r12.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r11.w = dot(cb1[153].xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r11.xyz = cb1[153].xyz;
   r7.xyzw = r11.xyzw * r7.xyzw;
   r1.x = dot(r0.xyz, r1.xyz);
   r1.x = max(9.99999975e-06, abs(r1.x));
   r1.y = dot(r10.xyz, float3(0.333333343, 0.333333343, 0.333333343));
   r10.xyzw = r0.wwww * float4(-1, -0.0274999999, -0.572000027, 0.0219999999) +
              float4(1, 0.0425000004, 1.03999996, -0.0399999991);
   r1.z = r10.x * r10.x;
   r1.x = -9.27999973 * r1.x;
   r1.x = exp2(r1.x);
   r1.x = min(r1.z, r1.x);
   r1.x = r1.x * r10.x + r10.y;
   r1.xz = r1.xx * float2(-1.03999996, 1.03999996) + r10.zw;
   r1.y = dot(r1.yyy, float3(0.333333343, 0.333333343, 0.333333343));
   r2.w = saturate(50 * r1.y);
   r1.z = r2.w * r1.z;
   r1.x = saturate(r1.y * r1.x + r1.z);
   r1.x = 1 + -r1.x;
   r1.x = r1.x * r1.x;
   r1.x = r5.x * -r1.x + r1.x;
   r4.xyz = r4.xyz * r1.xxx;
   r1.x = cmp(0 < cb1[140].z);
   r9.w = dot(r9.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
   r4.w = 0;
   r4.xyzw = r1.xxxx ? r9.xyzw : r4.xyzw;
   r1.x = cmp(0 < cb1[136].x);
   if (r1.x != 0)
   {
      r1.xyz = -cb2[8].xyz + r2.xyz;
      r9.xyz = float3(1, 1, 1) + cb2[9].xyz;
      r1.xyz = cmp(r9.xyz < abs(r1.xyz));
      r1.x = (int)r1.y | (int)r1.x;
      r1.x = (int)r1.z | (int)r1.x;
      r1.y = dot(r2.xyz, float3(0.577000022, 0.577000022, 0.577000022));
      r1.y = 0.00200000009 * r1.y;
      r1.y = frac(r1.y);
      r1.y = cmp(0.5 < r1.y);
      r2.xyz = r1.yyy ? float3(0, 1, 1) : float3(1, 1, 0);
      r2.w = dot(r2.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
      r4.xyzw = r1.xxxx ? r2.xyzw : r4.xyzw;
   }

   // FIX: Firefly Mitigation (Global Reinhard Compression) - ONLY FOR DLSS (SRType == 0)
   if (true)
   {
      float maxRawDiff = max(r4.r, max(r4.g, r4.b));
      float limit = 8000.0;

      float compressionFactor = limit / (maxRawDiff + limit + 0.000001);
      r4.rgb = r4.rgb * compressionFactor;
   }

   o0.xyzw = cb1[128].xxxx * r4.xyzw;

   r0.xyz = r0.xyz * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
   o1.w = r5.y ? 0.333333343 : 0;
   r1.x = (int)r1.w + 1;
   r1.x = (uint)r1.x;
   o2.w = 0.00392156886 * r1.x;

   // FIX: Firefly Mitigation for Specular (r7) - ONLY FOR DLSS (SRType == 0)
   if (true)
   {
      float maxRawSpec = max(r7.r, max(r7.g, r7.b));
      float limit = 8000.0;

      float compressionFactor = limit / (maxRawSpec + limit + 0.000001);
      r7.rgb = r7.rgb * compressionFactor;
   }

   o5.xyzw = cb1[128].xxxx * r7.xyzw;

   r1.xyzw = r3.xyzx * float4(2, 2, 2, 2) + float4(-1, -1, -1, -1);
   r2.xyzw = cmp(float4(0, 0, 0, 0) < r1.wyzw);
   r3.xyzw = cmp(r1.wyzw < float4(0, 0, 0, 0));
   r2.xyzw = (int4)-r2.xyzw + (int4)r3.xyzw;
   r2.xyzw = (int4)r2.xyzw;
   r1.xyzw = float4(1, 1, 1, 1) + -abs(r1.xyzw);
   r1.xyzw = sqrt(r1.xyzw);
   r1.xyzw = float4(1, 1, 1, 1) + -r1.xyzw;
   r1.xyzw = r2.xyzw * r1.xyzw;
   r2.xyz = r0.xyz * float3(2, 2, 2) + float3(-1, -1, -1);
   r2.xyz = float3(-0.998044968, -0.998044968, -0.998044968) + abs(r2.xyz);
   r2.xyz = cmp(r2.xyz < float3(0, 0, 0));
   r3.xyz = r1.xyz * float3(0.000977517106, 0.000977517106, 0.000977517106) + r0.xyz;
   o1.xyz = saturate(r2.xyz ? r3.xyz : r0.xyz);
   r0.xyz = r8.xyz * float3(2, 2, 2) + float3(-1, -1, -1);
   r0.xyz = float3(-0.992156863, -0.992156863, -0.992156863) + abs(r0.xyz);
   r0.xyz = cmp(r0.xyz < float3(0, 0, 0));
   r1.x = r1.w * 0.00392156886 + r8.x;
   o4.x = saturate(r0.x ? r1.x : r8.x);
   r1.xy = r1.yz * float2(0.00392156886, 0.00392156886) + r8.yz;
   o4.yz = saturate(r0.yz ? r1.xy : r8.yz);
   o2.x = r5.x;
   o2.y = 0.5;
   o2.z = r0.w;
   o3.xyz = r6.xyz;
   o4.w = 0;
   return;
}
