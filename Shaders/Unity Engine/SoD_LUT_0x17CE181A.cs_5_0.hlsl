#include "../Includes/Common.hlsl"
#include "../Includes/ACES.hlsl"
#include "../Includes/DICE.hlsl"
//#include "../Includes/Oklab.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "Includes/Settings.hlsl"

Texture2D<float4> t7 : register(t7);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

RWTexture3D<float4> _OutputTexture : register(u0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[18];
}

#define cmp

static const float3x3 AP1_2_AP0_MAT = {
     0.6954522414, 0.1406786965, 0.1638690622,
     0.0447945634, 0.8596711185, 0.0955343182,
    -0.0055258826, 0.0040252103, 1.0015006723
};
static const float3x3 XYZ_2_REC709_MAT = {
     3.2409699419, -1.5373831776, -0.4986107603,
    -0.9692436363,  1.8759675015,  0.0415550574,
     0.0556300797, -0.2039769589,  1.0569715142
};
static const float3x3 AP1_2_XYZ_MAT = {
     0.6624541811, 0.1340042065, 0.1561876870,
     0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003
};
static const float3x3 D60_2_D65_CAT = {
     0.98722400, -0.00611327, 0.0159533,
    -0.00759836,  1.00186000, 0.0053302,
     0.00307257, -0.00509595, 1.0816800
};

static const float DIM_SURROUND_GAMMA = 0.9811;
#define HALF_MAX 65504.0

float3 Unity_ACES(float3 color)
{
  float4 r0,r1,r2,r3;
#if 0
  color = max(float3(0,0,0), color);
#endif
  r1.y = dot(float3(0.695452213,0.140678704,0.163869068), color);
  r1.z = dot(float3(0.0447945632,0.859671116,0.0955343172), color);
  r1.w = dot(float3(-0.00552588282,0.00402521016,1.00150073), color);
  r0.x = min(r1.y, r1.z);
  r0.x = min(r0.x, r1.w);
  r0.y = max(r1.y, r1.z);
  r0.y = max(r0.y, r1.w);
  r0.xyz = max(float3(9.99999975e-005,9.99999975e-005,0.00999999978), r0.xyy);
  r0.x = r0.y + -r0.x;
  r0.x = r0.x / r0.z;
  r0.yzw = r1.wzy + -r1.zyw;
  r0.yz = r1.wz * r0.yz;
  r0.y = r0.y + r0.z;
  r0.y = r1.y * r0.w + r0.y;
  r0.y = max(0, r0.y);
  r0.y = sqrt(r0.y);
  r0.z = r1.w + r1.z;
  r0.z = r0.z + r1.y;
  r0.y = r0.y * 1.75 + r0.z;
  r0.w = -0.4 + r0.x;
  r1.x = 2.5 * r0.w;
  r1.x = 1 + -abs(r1.x);
  r1.x = max(0, r1.x);
  r0.w = cmp(r0.w >= 0);
  r0.w = r0.w ? 1 : -1;
  r1.x = -r1.x * r1.x + 1;
  r0.w = r0.w * r1.x + 1;
  r0.zw = float2(0.333333343,0.025) * r0.yw;
  r1.x = cmp(0.16 >= r0.y);
  r0.y = cmp(r0.y >= 0.48);
  r0.z = 0.08 / r0.z;
  r0.z = -0.5 + r0.z;
  r0.z = r0.w * r0.z;
  r0.y = r0.y ? 0 : r0.z;
  r0.y = r1.x ? r0.w : r0.y;
  r0.y = 1 + r0.y;
  r2.yzw = r1.yzw * r0.yyy;
  r0.zw = cmp(r2.zw == r2.yz);
  r0.z = r0.w ? r0.z : 0;
  r0.w = r1.z * r0.y + -r2.w;
  r0.w = 1.73205078 * r0.w;
  r1.x = r2.y * 2 + -r2.z;
  r1.x = -r1.w * r0.y + r1.x;
  r1.z = min(abs(r1.x), abs(r0.w));
  r1.w = max(abs(r1.x), abs(r0.w));
  r1.w = 1 / r1.w;
  r1.z = r1.z * r1.w;
  r1.w = r1.z * r1.z;
  r3.x = r1.w * 0.0208350997 + -0.0851330012;
  r3.x = r1.w * r3.x + 0.180141002;
  r3.x = r1.w * r3.x + -0.330299497;
  r1.w = r1.w * r3.x + 0.999866009;
  r3.x = r1.z * r1.w;
  r3.y = cmp(abs(r1.x) < abs(r0.w));
  r3.x = r3.x * -2 + 1.57079637;
  r3.x = r3.y ? r3.x : 0;
  r1.z = r1.z * r1.w + r3.x;
  r1.w = cmp(r1.x < -r1.x);
  r1.w = r1.w ? -3.141593 : 0;
  r1.z = r1.z + r1.w;
  r1.w = min(r1.x, r0.w);
  r0.w = max(r1.x, r0.w);
  r1.x = cmp(r1.w < -r1.w);
  r0.w = cmp(r0.w >= -r0.w);
  r0.w = r0.w ? r1.x : 0;
  r0.w = r0.w ? -r1.z : r1.z;
  r0.w = 57.2957802 * r0.w;
  r0.z = r0.z ? 0 : r0.w;
  r0.w = cmp(r0.z < 0);
  r1.x = 360 + r0.z;
  r0.z = r0.w ? r1.x : r0.z;
  r0.w = cmp(r0.z < -180);
  r1.x = cmp(180 < r0.z);
  r1.zw = float2(360,-360) + r0.zz;
  r0.z = r1.x ? r1.w : r0.z;
  r0.z = r0.w ? r1.z : r0.z;
  r0.z = 0.0148148146 * r0.z;
  r0.z = 1 + -abs(r0.z);
  r0.z = max(0, r0.z);
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r0.z = r0.z * r0.z;
  r0.x = r0.z * r0.x;
  r0.y = -r1.y * r0.y + 0.03;
  r0.x = r0.x * r0.y;
  r2.x = r0.x * 0.18 + r2.y;
  r0.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r2.xzw);
  r0.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r2.xzw);
  r0.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r2.xzw);
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r0.w = dot(r0.xyz, float3(0.272228986,0.674081981,0.0536894985));
  r0.xyz = r0.xyz + -r0.www;
  r0.xyz = r0.xyz * float3(0.959999979,0.959999979,0.959999979) + r0.www;
  r1.xyz = r0.xyz * float3(2.78508496,2.78508496,2.78508496) + float3(0.107772,0.107772,0.107772);
  r1.xyz = r1.xyz * r0.xyz;
  r2.xyz = r0.xyz * float3(2.93604493,2.93604493,2.93604493) + float3(0.887121975,0.887121975,0.887121975);
  r0.xyz = r0.xyz * r2.xyz + float3(0.806888998,0.806888998,0.806888998);
  r0.xyz = r1.xyz / r0.xyz;
  r1.x = dot(float3(0.662454188,0.134004205,0.156187683), r0.xyz);
  r1.y = dot(float3(0.272228718,0.674081743,0.0536895171), r0.xyz);
  r1.z = dot(float3(-0.00557464967,0.0040607336,1.01033914), r0.xyz);
  r0.x = dot(r1.xyz, float3(1,1,1));
  r0.x = max(9.99999975e-005, r0.x);
  r0.xy = r1.xy / r0.x;
#if 1
  r1.y = clamp(r1.y, 0.0, HALF_MAX);
  r1.y = pow(r1.y, DIM_SURROUND_GAMMA);
#else
  r1.y = pow(abs(r1.y), DIM_SURROUND_GAMMA) * sign(r1.y);
#endif
  r0.w = max(9.99999975e-005, r0.y);
  r0.w = r1.y / r0.w;
  r1.w = 1 - r0.x;
  r0.z = r1.w - r0.y;
  r1.xz = r0.xz * r0.ww;
  r0.x = dot(float3(1.6410234,-0.324803293,-0.236424699), r1.xyz);
  r0.y = dot(float3(-0.663662851,1.61533165,0.0167563483), r1.xyz);
  r0.z = dot(float3(0.0117218941,-0.00828444213,0.988394856), r1.xyz);
  r0.w = dot(r0.xyz, float3(0.272228986,0.674081981,0.0536894985));
  r0.xyz = r0.xyz - r0.w;
  r0.xyz = r0.xyz * 0.93 + r0.w;
  r1.x = dot(float3(0.662454188,0.134004205,0.156187683), r0.xyz);
  r1.y = dot(float3(0.272228718,0.674081743,0.0536895171), r0.xyz);
  r1.z = dot(float3(-0.00557464967,0.0040607336,1.01033914), r0.xyz);
  r0.x = dot(float3(0.987223983,-0.00611326983,0.0159533005), r1.xyz);
  r0.y = dot(float3(-0.00759836007,1.00186002,0.00533019984), r1.xyz);
  r0.z = dot(float3(0.00307257008,-0.00509594986,1.08168006), r1.xyz);
  r1.x = dot(float3(3.2409699,-1.5373832,-0.498610765), r0.xyz);
  r1.y = dot(float3(-0.969243646,1.8759675,0.0415550582), r0.xyz);
  r1.z = dot(float3(0.0556300804,-0.203976959,1.05697155), r0.xyz);
  return r1.xyz;
}

[numthreads(4, 4, 4)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  r0.xyz = (uint3)vThreadID.xyz;
  r0.w = cmp(0 < cb0[17].x);
  if (r0.w != 0) {
    r1.xyz = r0.xyz * cb0[0].y + float3(-0.386036009,-0.386036009,-0.386036009);
    r1.xyz = float3(13.6054821,13.6054821,13.6054821) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = float3(-0.0479959995,-0.0479959995,-0.0479959995) + r1.xyz;
    r1.xyz = float3(0.179999992,0.179999992,0.179999992) * r1.xyz;
    r2.x = dot(float3(0.390404999,0.549941003,0.00892631989), r1.xyz);
    r2.y = dot(float3(0.070841603,0.963172019,0.00135775004), r1.xyz);
    r2.z = dot(float3(0.0231081992,0.128021002,0.936245024), r1.xyz);
    r1.xyz = cb0[2].xyz * r2.xyz;
    r2.x = dot(float3(2.85846996,-1.62879002,-0.0248910002), r1.xyz);
    r2.y = dot(float3(-0.210181996,1.15820003,0.000324280991), r1.xyz);
    r2.z = dot(float3(-0.0418119989,-0.118169002,1.06867003), r1.xyz);
    r1.x = dot(float3(0.439700991,0.382977992,0.177334994), r2.xyz);
    r1.y = dot(float3(0.0897922963,0.813422978,0.0967615992), r2.xyz);
    r1.z = dot(float3(0.0175439995,0.111543998,0.870703995), r2.xyz);
  #if 0
    r1.xyz = max(0, r1.xyz);
    r1.xyz = min(float3(65504,65504,65504), r1.xyz);
  #endif
    r2.xyz = cmp(r1.xyz < float3(3.05175708e-005,3.05175708e-005,3.05175708e-005));
    r3.xyz = r1.xyz * float3(0.5,0.5,0.5) + float3(1.525878e-005,1.525878e-005,1.525878e-005);
    r3.xyz = log2(r3.xyz);
    r3.xyz = float3(9.72000027,9.72000027,9.72000027) + r3.xyz;
    r3.xyz = float3(0.0570776239,0.0570776239,0.0570776239) * r3.xyz;
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(9.72000027,9.72000027,9.72000027) + r1.xyz;
    r1.xyz = float3(0.0570776239,0.0570776239,0.0570776239) * r1.xyz;
    r1.xyz = r2.xyz ? r3.xyz : r1.xyz;
    r1.xyz = float3(-0.413588405,-0.413588405,-0.413588405) + r1.xyz;
    r1.xyz = r1.xyz * cb0[7].zzz + float3(0.413588405,0.413588405,0.413588405);
    r2.xyz = r1.xyz * float3(17.5200005,17.5200005,17.5200005) + float3(-9.72000027,-9.72000027,-9.72000027);
    r2.xyz = exp2(r2.xyz);
    r3.xyz = float3(-1.52587891e-005,-1.52587891e-005,-1.52587891e-005) + r2.xyz;
    r3.xyz = r3.xyz + r3.xyz;
    r4.xyzw = cmp(r1.xxyy < float4(-0.301369876,1.46799636,-0.301369876,1.46799636));
    r1.xy = r4.yw ? r2.xy : float2(65504,65504);
    r4.xy = r4.xz ? r3.xy : r1.xy;
    r1.xy = cmp(r1.zz < float2(-0.301369876,1.46799636));
    r0.w = r1.y ? r2.z : 65504;
    r4.z = r1.x ? r3.z : r0.w;
    r1.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r4.xyz);
    r1.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r4.xyz);
    r1.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r4.xyz);
    r1.xyz = cb0[3].xyz * r1.xyz;
    r1.xyz = max(float3(0,0,0), r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(0.454545468,0.454545468,0.454545468) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r2.xyz = min(float3(1,1,1), r1.xyz);
    r0.w = dot(r2.xyz, float3(0.272228986,0.674081981,0.0536894985));
    r0.w = saturate(cb0[15].w + r0.w);
    r1.w = 1 + -r0.w;
    r2.xyz = float3(-0.5,-0.5,-0.5) + cb0[15].xyz;
    r2.xyz = r1.www * r2.xyz + float3(0.5,0.5,0.5);
    r3.xyz = float3(-0.5,-0.5,-0.5) + cb0[16].xyz;
    r3.xyz = r0.www * r3.xyz + float3(0.5,0.5,0.5);
    r4.xyz = r1.xyz + r1.xyz;
    r5.xyz = r1.xyz * r1.xyz;
    r6.xyz = -r2.xyz * float3(2,2,2) + float3(1,1,1);
    r5.xyz = r6.xyz * r5.xyz;
    r5.xyz = r4.xyz * r2.xyz + r5.xyz;
    r1.xyz = sqrt(r1.xyz);
    r6.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r7.xyz = float3(1,1,1) + -r2.xyz;
    r4.xyz = r7.xyz * r4.xyz;
    r1.xyz = r1.xyz * r6.xyz + r4.xyz;
    r2.xyz = cmp(r2.xyz >= float3(0.5,0.5,0.5));
    r4.xyz = r2.xyz ? float3(1,1,1) : 0;
    r2.xyz = r2.xyz ? float3(0,0,0) : float3(1,1,1);
    r2.xyz = r2.xyz * r5.xyz;
    r1.xyz = r1.xyz * r4.xyz + r2.xyz;
    r2.xyz = r1.xyz + r1.xyz;
    r4.xyz = r1.xyz * r1.xyz;
    r5.xyz = -r3.xyz * float3(2,2,2) + float3(1,1,1);
    r4.xyz = r5.xyz * r4.xyz;
    r4.xyz = r2.xyz * r3.xyz + r4.xyz;
    r1.xyz = sqrt(r1.xyz);
    r5.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r6.xyz = float3(1,1,1) + -r3.xyz;
    r2.xyz = r6.xyz * r2.xyz;
    r1.xyz = r1.xyz * r5.xyz + r2.xyz;
    r2.xyz = cmp(r3.xyz >= float3(0.5,0.5,0.5));
    r3.xyz = r2.xyz ? float3(1,1,1) : 0;
    r2.xyz = r2.xyz ? float3(0,0,0) : float3(1,1,1);
    r2.xyz = r2.xyz * r4.xyz;
    r1.xyz = r1.xyz * r3.xyz + r2.xyz;
    r1.xyz = pow(abs(r1.xyz), 2.2) * sign(r1.xyz); // Gamma 2.2
    r2.x = dot(r1.xyz, cb0[4].xyz);
    r2.y = dot(r1.xyz, cb0[5].xyz);
    r2.z = dot(r1.xyz, cb0[6].xyz);
    r0.w = dot(r2.xyz, float3(0.272228986,0.674081981,0.0536894985));
    r1.xy = cb0[14].yw + -cb0[14].xz;
    r1.zw = -cb0[14].xz + r0.ww;
    r1.xy = float2(1,1) / r1.xy;
    r1.xy = saturate(r1.zw * r1.xy);
    r1.zw = r1.xy * float2(-2,-2) + float2(3,3);
    r1.xy = r1.xy * r1.xy;
    r0.w = r1.w * r1.y;
    r1.x = -r1.z * r1.x + 1;
    r1.z = 1 + -r1.x;
    r1.y = -r1.w * r1.y + r1.z;
    r3.xyz = cb0[11].xyz * r2.xyz;
    r4.xyz = cb0[12].xyz * r2.xyz;
    r1.yzw = r4.xyz * r1.yyy;
    r1.xyz = r3.xyz * r1.xxx + r1.yzw;
    r2.xyz = cb0[13].xyz * r2.xyz;
    r1.xyz = r2.xyz * r0.www + r1.xyz;
    r1.xyz = r1.xyz * cb0[10].xyz + cb0[8].xyz;
    r2.xyz = sign(r1.xyz);
    r1.xyz = pow(abs(r1.xyz), cb0[9].xyz);
    r3.xyz = r2.xyz * r1.xyz;
    r0.w = cmp(r3.y >= r3.z);
    r0.w = r0.w ? 1.0 : 0;
    r4.xy = r3.zy;
    r4.zw = float2(-1,0.666666687);
    r1.xy = r2.yz * r1.yz + -r4.xy;
    r1.zw = float2(1,-1);
    r1.xyzw = r0.wwww * r1.xyzw + r4.xyzw;
    r0.w = cmp(r3.x >= r1.x);
    r0.w = r0.w ? 1.0 : 0;
    r2.xyz = r1.xyw;
    r2.w = r3.x;
    r1.xyw = r2.wyx;
    r1.xyzw = r1.xyzw + -r2.xyzw;
    r1.xyzw = r0.wwww * r1.xyzw + r2.xyzw;
    r0.w = min(r1.w, r1.y);
    r0.w = r1.x + -r0.w;
    r1.y = r1.w + -r1.y;
    r1.w = r0.w * 6 + 9.99999975e-005;
    r1.y = r1.y / r1.w;
    r1.y = r1.z + r1.y;
    r2.x = abs(r1.y);
    r1.y = 9.99999975e-005 + r1.x;
    r2.z = r0.w / r1.y;
    r2.yw = float2(0,0);
    r0.w = t5.SampleLevel(s0_s, r2.xy, 0).x;
    r0.w = saturate(r0.w);
    r0.w = r0.w + r0.w;
    r1.y = t6.SampleLevel(s0_s, r2.zw, 0).x;
    r1.y = saturate(r1.y);
    r1.y = r1.y + r1.y;
    r0.w = r1.y * r0.w;
    r3.x = dot(r3.xyz, float3(0.212672904,0.715152204,0.0721750036));
    r3.yw = float2(0,0);
    r1.y = t7.SampleLevel(s0_s, r3.xy, 0).x;
    r1.y = saturate(r1.y);
    r1.y = r1.y + r1.y;
    r0.w = r1.y * r0.w;
    r3.z = cb0[7].x + r2.x;
    r1.y = t4.SampleLevel(s0_s, r3.zw, 0).x;
    r1.y = saturate(r1.y);
    r1.y = r1.y + r3.z;
    r1.yzw = float3(-0.5,0.5,-1.5) + r1.yyy;
    r2.x = cmp(r1.y < 0);
    r2.y = cmp(1 < r1.y);
    r1.y = r2.y ? r1.w : r1.y;
    r1.y = r2.x ? r1.z : r1.y;
    r1.yzw = float3(1,0.666666687,0.333333343) + r1.yyy;
    r1.yzw = frac(r1.yzw);
    r1.yzw = r1.yzw * float3(6,6,6) + float3(-3,-3,-3);
    r1.yzw = saturate(float3(-1,-1,-1) + abs(r1.yzw));
    r1.yzw = float3(-1,-1,-1) + r1.yzw;
    r1.yzw = r2.zzz * r1.yzw + float3(1,1,1);
    r2.xyz = r1.xxx * r1.yzw;
    r2.x = dot(r2.xyz, float3(0.272228986,0.674081981,0.0536894985));
    r0.w = cb0[7].y * r0.w;
    r1.xyz = r1.xxx * r1.yzw + -r2.xxx;
    r1.xyz = r0.www * r1.xyz + r2.xxx;
    r0.w = max(r1.x, r1.y);
    r0.w = max(r0.w, r1.z);
    r0.w = 1 + r0.w;
    r0.w = rcp(r0.w);
    r1.xyz = r1.xyz * r0.www + float3(0.00390625,0.00390625,0.00390625);
    r1.w = 0;
    r2.x = t0.SampleLevel(s0_s, r1.xw, 0).x;
    r2.x = saturate(r2.x);
    r2.y = t0.SampleLevel(s0_s, r1.yw, 0).x;
    r2.y = saturate(r2.y);
    r2.z = t0.SampleLevel(s0_s, r1.zw, 0).x;
    r2.z = saturate(r2.z);
    r1.xyz = float3(0.00390625,0.00390625,0.00390625) + r2.xyz;
    r1.w = 0;
    r2.x = t1.SampleLevel(s0_s, r1.xw, 0).x;
    r2.x = saturate(r2.x);
    r2.y = t2.SampleLevel(s0_s, r1.yw, 0).x;
    r2.y = saturate(r2.y);
    r2.z = t3.SampleLevel(s0_s, r1.zw, 0).x;
    r2.z = saturate(r2.z);
    r0.w = max(r2.x, r2.y);
    r0.w = max(r0.w, r2.z);
    r0.w = 1 + -r0.w;
    r0.w = rcp(r0.w);
    r1.xyz = r2.xyz * r0.www;
#if 0
    r1.xyz = max(float3(0,0,0), r1.xyz);
#endif
  } else {
    r0.xyz = r0.xyz * cb0[0].yyy + float3(-0.386036009,-0.386036009,-0.386036009);
    r0.xyz = float3(13.6054821,13.6054821,13.6054821) * r0.xyz;
    r0.xyz = exp2(r0.xyz);
    r0.xyz = float3(-0.0479959995,-0.0479959995,-0.0479959995) + r0.xyz;
    r0.xyz = float3(0.179999992,0.179999992,0.179999992) * r0.xyz;
    r2.x = dot(float3(0.439700991,0.382977992,0.177334994), r0.xyz);
    r2.y = dot(float3(0.0897922963,0.813422978,0.0967615992), r0.xyz);
    r2.z = dot(float3(0.0175439995,0.111543998,0.870703995), r0.xyz);
    r1.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r2.xyz);
    r1.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r2.xyz);
    r1.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r2.xyz);
  }

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

  float3 untonemapped = r1.rgb; // In AP1
  float3 vanillaTM = Unity_ACES(untonemapped); // Outputs Rec.709
  float3 tonemapped = vanillaTM;

#if TONEMAP_TYPE == 1 // Proper ACES

  ACESSettings acesSettings = DefaultACESSettings();
  acesSettings.mid_grey = MidGray;
  acesSettings.dark_to_dim = true; // Arguably more accurate to SDR reference (maybe)
  acesSettings.legacy_desat = false; // More accurate to SDR reference, but it then looks SDRish saturation wise
  acesSettings.input_to_ap1_matrix = IdentityMatrix;
  tonemapped = ACESTonemap(untonemapped, paperWhite, peakWhite, acesSettings); // Outputs Rec.709

#elif TONEMAP_TYPE >= 2

  // Convert from AP1 to Rec.709
  float3 XYZ = mul(AP1_2_XYZ_MAT, untonemapped);
  XYZ = mul(D60_2_D65_CAT, XYZ);
  untonemapped = mul(XYZ_2_REC709_MAT, XYZ);

#if 0
  // Unity ACES returns mid gray (18% (0.18) of SDR) for the manually found value of 0.259 (there's no dynamic parameters for it so the value is constant)
  float3 ACES_MidGray_Out = Unity_ACES(0.259); // Result is 0.18
#endif
  untonemapped *= 0.18 / 0.259;

	DICESettings diceSettings = DefaultDICESettings();
#if TONEMAP_TYPE == 2

#if 1
	// Restore SDR TM below mid gray and restore hue/chroma of SDR at a fixed percentage to retain the features of the SDR tonemapper (e.g. desaturation etc)
	float3 oklch = Oklab::linear_srgb_to_oklch(untonemapped);
	float3 vanillaTMOklab = Oklab::linear_srgb_to_oklch(vanillaTM);
	oklch[0] = lerp(vanillaTMOklab[0], oklch[0], saturate(pow(vanillaTMOklab[0], 3.0) * 2.0)); // We get raised blacks without this
	oklch[1] = lerp(oklch[1], vanillaTMOklab[1], 0.75);
	if (abs(vanillaTMOklab[2] - oklch[2]) > PI)
	{
		if (oklch[2] <= vanillaTMOklab[2])
		{
			oklch[2] += PI * 2.0;
		}
		else
		{
			vanillaTMOklab[2] += PI * 2.0;
		}
	}
	oklch[2] = lerp(oklch[2], vanillaTMOklab[2], pow(saturate(vanillaTMOklab[0]), 0.75) * 0.667); // This restores the hue distortion from the vanilla tonemapper
	// Reapply some pure saturation (chroma) after desaturating
	oklch[1] = lerp(oklch[1], max(oklch[1], 1.0), saturate((vanillaTMOklab[0] * 2.0) - 1.0) * 0.0275);
	untonemapped = Oklab::oklch_to_linear_srgb(oklch);
#else
  float restoreBrightness = 1.0; // We get raised blacks without this
	untonemapped = RestoreHueAndChrominance(untonemapped, saturate(vanillaTM), 0.75, 0.0, 0.0, FLT_MAX, restoreBrightness);
#endif
  
#elif TONEMAP_TYPE == 3 || TONEMAP_TYPE == 4

  // Restore SDR TM below mid gray. This can have some nice colors and good saturation, but shows some random colors that would have completely clipped in SDR so they can be out of place
#if 0 // By channel preserves shadow saturation more accurately
  untonemapped = lerp(vanillaTM, untonemapped, saturate(pow(vanillaTM / MidGray, 2.0)));
#elif 0 // This method reduces saturation too much
  untonemapped = lerp(RestoreLuminance(untonemapped, vanillaTM), untonemapped, saturate(pow(vanillaTM / MidGray, 2.0)));
#else // A mix of the two above
  untonemapped = lerp(lerp(vanillaTM, untonemapped, saturate(pow(vanillaTM / MidGray, 2.0))), lerp(RestoreLuminance(untonemapped, vanillaTM), untonemapped, saturate(pow(vanillaTM / MidGray, 2.0))), 0.666);
#endif

#endif // TONEMAP_TYPE == 2 || TONEMAP_TYPE == 3 || TONEMAP_TYPE == 4

	FixColorGradingLUTNegativeLuminance(untonemapped);

#if TONEMAP_TYPE <= 4
#if TONEMAP_TYPE == 3
  // This helps bringing the saturation down a bit
  diceSettings.Type = DICE_TYPE_BY_CHANNEL_PQ;
#else // TONEMAP_TYPE != 3
  diceSettings.Type = DICE_TYPE_BY_LUMINANCE_PQ;
#endif // TONEMAP_TYPE == 3

  //diceSettings.ShoulderStart = paperWhite * MidGray / peakWhite; // This is already set to a >0 value
	tonemapped = DICETonemap(untonemapped * paperWhite, peakWhite, diceSettings) / paperWhite;

#if TONEMAP_TYPE == 4
  // Do a second DICE pass by PQ channel and then restore that chrominance, to smoothly desaturate highlights
  diceSettings.Type = DICE_TYPE_BY_CHANNEL_PQ;
	float3 tonemappedAlt = DICETonemap(untonemapped * paperWhite, peakWhite, diceSettings) / paperWhite;
	tonemapped = RestoreHueAndChrominance(tonemapped, tonemappedAlt, 0.0, 1.0);
#endif // TONEMAP_TYPE == 4

#else // TONEMAP_TYPE > 4
  tonemapped = untonemapped;
#endif // TONEMAP_TYPE <= 4
   
	if (any(isnan(tonemapped)))
		tonemapped = 0;
  
#endif // TONEMAP_TYPE >= 2

#if 0 // This allows gamut to go beyond Rec.709
  tonemapped = max(tonemapped, 0.0);
#endif

  _OutputTexture[vThreadID.xyz] = float4(tonemapped, 1.0);
}