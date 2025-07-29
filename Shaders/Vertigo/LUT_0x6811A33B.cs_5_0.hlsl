#include "../Includes/Common.hlsl"
#include "../Includes/ACES.hlsl"
#include "../Includes/DICE.hlsl"
//#include "../Includes/Oklab.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "Includes/Settings.hlsl"

Texture2D<float> _CurveLumVsSat : register(t7);
Texture2D<float> _CurveSatVsSat : register(t6);
Texture2D<float> _CurveHueVsSat : register(t5);
Texture2D<float> _CurveHueVsHue : register(t4);
Texture2D<float> _CurveBlue : register(t3);
Texture2D<float> _CurveGreen : register(t2);
Texture2D<float> _CurveRed : register(t1);
Texture2D<float> _CurveMaster : register(t0);

RWTexture3D<float4> _OutputTexture : register(u0);

SamplerState s0_s : register(s0); // Probably bilinear clamp

cbuffer cb0 : register(b0)
{
    float4 _Size : packoffset(c0);               // x: lut_size, y: 1 / (lut_size - 1), zw: unused
    float4 _LogLut3D_Params : packoffset(c1);    // x: 1 / lut_size, y: lut_size - 1, z: contribution, w: unused
    float4 _ColorBalance : packoffset(c2);       // xyz: LMS coeffs, w: unused
    float4 _ColorFilter : packoffset(c3);        // xyz: color, w: unused
    float4 _ChannelMixerRed : packoffset(c4);    // xyz: rgb coeffs, w: unused
    float4 _ChannelMixerGreen : packoffset(c5);  // xyz: rgb coeffs, w: unused
    float4 _ChannelMixerBlue : packoffset(c6);   // xyz: rgb coeffs, w: unused
    float4 _HueSatCon : packoffset(c7);          // x: hue shift, y: saturation, z: contrast, w: unused
    float4 _Lift : packoffset(c8);               // xyz: color, w: unused
    float4 _Gamma : packoffset(c9);              // xyz: color, w: unused
    float4 _Gain : packoffset(c10);               // xyz: color, w: unused
    float4 _Shadows : packoffset(c11);            // xyz: color, w: unused
    float4 _Midtones : packoffset(c12);           // xyz: color, w: unused
    float4 _Highlights : packoffset(c13);         // xyz: color, w: unused
    float4 _ShaHiLimits : packoffset(c14);        // xy: shadows min/max, zw: highlight min/max
    float4 _SplitShadows : packoffset(c15);       // xyz: color, w: balance
    float4 _SplitHighlights : packoffset(c16);    // xyz: color, w: unused
    float4 _Params : packoffset(c17);             // x: enable grading, yzw: unused
}

#define cmp -

float EvaluateCurve(Texture2D<float> curve, float t)
{
  float width;
  float height;
  curve.GetDimensions(width, height);
  const float scale = (width - 1.0) / width;
  const float bias = 0.5 / width;
#if 0 // LUMA FT: corrected LUT sampling to account for half texel offset, and made sure y is sampled on 0.5 (these curves should be 1px in height). Unfortunately this massively shifts colors so we can't really do it
  t = saturate(t) * scale + bias;
#endif
  float x = curve.SampleLevel(s0_s, float2(t, 0.5), 0.0).x;
#if 0
  x = saturate(x);
#endif
  return x;
}

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

float3 ACEScg_to_ACES(float3 x)
{
  return mul(AP1_2_AP0_MAT, x);
}

float rgb_2_saturation(float3 rgb)
{
  const float TINY = 1e-4;
  float mi = min(rgb.r, min(rgb.g, rgb.b));
  float ma = max(rgb.r, max(rgb.g, rgb.b));
  return (max(ma, TINY) - max(mi, TINY)) / max(ma, 1e-2);
}

float3 Unity_ACES(float3 input)
{
#if 0 // WIP original code (functions are missing)
    float3 aces = ACEScg_to_ACES(input);
    // --- Glow module --- //
    half saturation = rgb_2_saturation(half3(aces));
    half ycIn = rgb_2_yc(half3(aces));
    half s = sigmoid_shaper((saturation - 0.4) / 0.2);
    float addedGlow = 1.0 + glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    aces *= addedGlow;

    // --- Red modifier --- //
    half hue = rgb_2_hue(half3(aces));
    half centeredHue = center_hue(hue, RRT_RED_HUE);
    float hueWeight;
    {
        //hueWeight = cubic_basis_shaper(centeredHue, RRT_RED_WIDTH);
        hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / RRT_RED_WIDTH));
        hueWeight *= hueWeight;
    }

    aces.r += hueWeight * saturation * (RRT_RED_PIVOT - aces.r) * (1.0 - RRT_RED_SCALE);

    // --- ACES to RGB rendering space --- //
    float3 acescg = max(0.0, ACES_to_ACEScg(aces));

    // --- Global desaturation --- //
    //acescg = mul(RRT_SAT_MAT, acescg);
    acescg = lerp(dot(acescg, AP1_RGB2Y).xxx, acescg, RRT_SAT_FACTOR);

    // Apply RRT and ODT
    // https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
    const float a = 0.0245786f;
    const float b = 0.000090537f;
    const float c = 0.983729f;
    const float d = 0.4329510f;
    const float e = 0.238081f;

#if defined(SHADER_API_SWITCH)
    // To reduce the likelyhood of extremely large values, we avoid using the x^2 term and therefore
    // divide numerator and denominator by it. This will lead to the constant factors of the
    // quadratic in the numerator and denominator to be divided by x; we add a tiny epsilon to avoid divide by 0.
    float3 rcpAcesCG = rcp(acescg + FLT_MIN);
    float3 rgbPost = (acescg + a - b * rcpAcesCG) / (acescg * c + d + e * rcpAcesCG);
#else
    float3 rgbPost = (acescg * (acescg + a) - b) / (acescg * (c * acescg + d) + e);
#endif

    // Scale luminance to linear code value
    // float3 linearCV = Y_2_linCV(rgbPost, CINEMA_WHITE, CINEMA_BLACK);

    // Apply gamma adjustment to compensate for dim surround
    // Unity does this after their approximate ODT curve fit, but it should be done before anything else.
    float3 linearCV = darkSurround_to_dimSurround(rgbPost);

    // Apply desaturation to compensate for luminance difference
    //linearCV = mul(ODT_SAT_MAT, color);
    linearCV = lerp(dot(linearCV, AP1_RGB2Y).xxx, linearCV, ODT_SAT_FACTOR);

    // Convert to display primary encoding
    // Rendering space RGB to XYZ
    float3 XYZ = mul(AP1_2_XYZ_MAT, linearCV);

    // Apply CAT from ACES white point to assumed observer adapted white point
    XYZ = mul(D60_2_D65_CAT, XYZ);

    // CIE XYZ to display primaries
    linearCV = mul(XYZ_2_REC709_MAT, XYZ);

    return linearCV;
#else
  float4 r0,r1,r2,r3;
  
  r0.yzw = ACEScg_to_ACES(input);
  r0.x = rgb_2_saturation(r0.yzw);

  r1.xyz = r0.wzy + -r0.zyw;
  r1.xy = r1.xy * r0.wz;
  r1.x = r1.x + r1.y;
  r1.x = r0.y * r1.z + r1.x;
#if 1 // LUMA
  r1.x = sqrt(abs(r1.x)) * sign(r1.x);
#else
  r1.x = max(0, r1.x);
  r1.x = sqrt(r1.x);
#endif
  r1.y = r0.w + r0.z;
  r1.y = r1.y + r0.y;
  r1.x = r1.x * 1.75 + r1.y;
  r1.z = -0.4 + r0.x;
  r1.yw = float2(1.0 / 3.0, 2.5) * r1.xz;
  r1.w = 1 + -abs(r1.w);
#if 1
  r1.w = max(0, r1.w);
#endif
  r1.z = cmp(r1.z >= 0);
  r1.z = r1.z ? 1 : -1;
  r1.w = -r1.w * r1.w + 1;
  r1.z = r1.z * r1.w + 1;
  r1.z = 0.025 * r1.z;
  r1.w = cmp(0.16 >= r1.x);
  r1.x = cmp(r1.x >= 0.48);
  r1.y = 0.08 / r1.y;
  r1.y = -0.5 + r1.y;
  r1.y = r1.z * r1.y;
  r1.x = r1.x ? 0 : r1.y;
  r1.x = r1.w ? r1.z : r1.x;
  r1.x = 1 + r1.x;
  r2.yzw = r1.xxx * r0.yzw;
  r1.yz = cmp(r2.zw == r2.yz);
  r1.y = r1.z ? r1.y : 0;
  r0.z = r0.z * r1.x + -r2.w;
  r0.z = 1.73205078 * r0.z;
  r1.z = r2.y * 2 + -r2.z;
  r0.w = -r0.w * r1.x + r1.z;
  r1.z = min(abs(r0.z), abs(r0.w));
  r1.w = max(abs(r0.z), abs(r0.w));
  r1.w = 1 / r1.w;
  r1.z = r1.z * r1.w;
  r1.w = r1.z * r1.z;
  r3.x = r1.w * 0.0208350997 + -0.0851330012;
  r3.x = r1.w * r3.x + 0.180141002;
  r3.x = r1.w * r3.x + -0.330299497;
  r1.w = r1.w * r3.x + 0.999866009;
  r3.x = r1.z * r1.w;
  r3.y = cmp(abs(r0.w) < abs(r0.z));
  r3.x = r3.x * -2 + 1.57079637;
  r3.x = r3.y ? r3.x : 0;
  r1.z = r1.z * r1.w + r3.x;
  r1.w = cmp(r0.w < -r0.w);
  r1.w = r1.w ? -3.141593 : 0;
  r1.z = r1.z + r1.w;
  r1.w = min(r0.z, r0.w);
  r0.z = max(r0.z, r0.w);
  r0.w = cmp(r1.w < -r1.w);
  r0.z = cmp(r0.z >= -r0.z);
  r0.z = r0.z ? r0.w : 0;
  r0.z = r0.z ? -r1.z : r1.z;
  r0.z = 57.2957802 * r0.z;
  r0.z = r1.y ? 0 : r0.z;
  r0.w = cmp(r0.z < 0);
  r1.y = 360 + r0.z;
  r0.z = r0.w ? r1.y : r0.z;
  r0.w = cmp(r0.z < -180);
  r1.y = cmp(180 < r0.z);
  r1.zw = float2(360,-360) + r0.zz;
  r0.z = r1.y ? r1.w : r0.z;
  r0.z = r0.w ? r1.z : r0.z;
  r0.z = 0.0148148146 * r0.z;
  r0.z = 1 + -abs(r0.z);
#if 1 // This seems necessary to look right
  r0.z = max(0, r0.z);
#endif
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r0.z = r0.z * r0.z;
  r0.x = r0.z * r0.x;
  r0.y = -r0.y * r1.x + 0.03;
  r0.x = r0.x * r0.y;
  r2.x = r0.x * 0.18 + r2.y;
  r0.x = dot(float3(1.45143926,-0.236510754,-0.214928567), r2.xzw);
  r0.y = dot(float3(-0.0765537769,1.17622972,-0.0996759236), r2.xzw);
  r0.z = dot(float3(0.00831614807,-0.00603244966,0.997716308), r2.xzw);
#if 0
  r0.xyz = max(float3(0,0,0), r0.xyz);
#endif
  r0.xyz = lerp(dot(r0.xyz, float3(0.212672904,0.715152204,0.0721750036)), r0.xyz, 0.96);
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
#if 0
  r1.y = clamp(r1.y, 0.0, HALF_MAX);
  r1.y = pow(r1.y, DIM_SURROUND_GAMMA);
#else // Not really needed as this is luminance?
  r1.y = pow(abs(r1.y), DIM_SURROUND_GAMMA) * sign(r1.y);
#endif
#if 0
  r0.w = max(9.99999975e-005, r0.y);
#else
  r0.w = r0.y;
#endif
  r0.w = r1.y / r0.w;
  r1.w = 1 - r0.x;
  r0.z = r1.w - r0.y;
  r1.xz = r0.xz * r0.w;
  r0.x = dot(float3(1.6410234,-0.324803293,-0.236424699), r1.xyz);
  r0.y = dot(float3(-0.663662851,1.61533165,0.0167563483), r1.xyz);
  r0.z = dot(float3(0.0117218941,-0.00828444213,0.988394856), r1.xyz);
  r0.xyz = lerp(dot(r0.xyz, float3(0.212672904,0.715152204,0.0721750036)), r0.xyz, 0.93);
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
#endif
}

// Input: Arri c800 or c1000
// Output: linear sRGB/BT.709
// The target texture is R16G16B16A16 so the output can go beyond 1
// LUT size is 32x32x32 (3D)
// https://github.com/Unity-Technologies/Graphics/blob/3ecf962fef917838b178492fc95d0e0bf8d7fbec/Packages/com.unity.render-pipelines.high-definition/Runtime/PostProcessing/Shaders/LutBuilder3D.compute#L4
[numthreads(4, 4, 4)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  if (_Params.x > 0.0) // ColorGrade() (runs in HDR space)
  {
    r1.xyz = vThreadID.xyz * _Size.yyy + float3(-0.386036009,-0.386036009,-0.386036009);
    r1.xyz = float3(13.6054821,13.6054821,13.6054821) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = float3(-0.0479959995,-0.0479959995,-0.0479959995) + r1.xyz;
    r1.xyz = float3(0.179999992,0.179999992,0.179999992) * r1.xyz;
    r2.x = dot(float3(0.390404999,0.549941003,0.00892631989), r1.xyz);
    r2.y = dot(float3(0.070841603,0.963172019,0.00135775004), r1.xyz);
    r2.z = dot(float3(0.0231081992,0.128021002,0.936245024), r1.xyz);
    r1.xyz = _ColorBalance.xyz * r2.xyz;
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
    r1.xyz = r1.xyz * _HueSatCon.zzz + float3(0.413588405,0.413588405,0.413588405);
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
    r1.xyz = _ColorFilter.xyz * r1.xyz;
    r1.xyz = max(float3(0,0,0), r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(0.454545468,0.454545468,0.454545468) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r2.xyz = min(float3(1,1,1), r1.xyz);
    r0.w = dot(r2.xyz, float3(0.212672904,0.715152204,0.0721750036));
    r0.w = saturate(_SplitShadows.w + r0.w);
    r1.w = 1 + -r0.w;
    r2.xyz = float3(-0.5,-0.5,-0.5) + _SplitShadows.xyz;
    r2.xyz = r1.www * r2.xyz + float3(0.5,0.5,0.5);
    r3.xyz = float3(-0.5,-0.5,-0.5) + _SplitHighlights.xyz;
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
    r2.x = dot(r1.xyz, _ChannelMixerRed.xyz);
    r2.y = dot(r1.xyz, _ChannelMixerGreen.xyz);
    r2.z = dot(r1.xyz, _ChannelMixerBlue.xyz);
    r0.w = dot(r2.xyz, float3(0.212672904,0.715152204,0.0721750036));
    r1.xy = _ShaHiLimits.yw + -_ShaHiLimits.xz;
    r1.zw = -_ShaHiLimits.xz + r0.ww;
    r1.xy = float2(1,1) / r1.xy;
    r1.xy = saturate(r1.zw * r1.xy);
    r1.zw = r1.xy * float2(-2,-2) + float2(3,3);
    r1.xy = r1.xy * r1.xy;
    r0.w = r1.w * r1.y;
    r1.x = -r1.z * r1.x + 1;
    r1.z = 1 + -r1.x;
    r1.y = -r1.w * r1.y + r1.z;
    r3.xyz = _Shadows.xyz * r2.xyz;
    r4.xyz = _Midtones.xyz * r2.xyz;
    r1.yzw = r4.xyz * r1.yyy;
    r1.xyz = r3.xyz * r1.xxx + r1.yzw;
    r2.xyz = _Highlights.xyz * r2.xyz;
    r1.xyz = r2.xyz * r0.www + r1.xyz;
    r1.xyz = r1.xyz * _Gain.xyz + _Lift.xyz;
    r2.xyz = cmp(float3(0,0,0) < r1.xyz);
    r3.xyz = cmp(r1.xyz < float3(0,0,0));
    r2.xyz = (int3)-r2.xyz + (int3)r3.xyz;
    r2.xyz = (int3)r2.xyz;
    r1.xyz = pow(abs(r1.xyz), _Gamma.xyz) * sign(r1.xyz); // User custom gamma? Probably neutral at 1, or maybe 1/2.2
    r3.xyz = r2.xyz * r1.xyz;
    r0.w = cmp(r3.y >= r3.z);
    r0.w = r0.w ? 1.000000 : 0;
    r4.xy = r3.zy;
    r4.zw = float2(-1,0.666666687);
    r1.xy = r2.yz * r1.yz + -r4.xy;
    r1.zw = float2(1,-1);
    r1.xyzw = r0.wwww * r1.xyzw + r4.xyzw;
    r0.w = cmp(r3.x >= r1.x);
    r0.w = r0.w ? 1.000000 : 0;
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
    r0.w = EvaluateCurve(_CurveHueVsSat, r2.x);
    r0.w = r0.w + r0.w;
    r1.y = EvaluateCurve(_CurveSatVsSat, r2.z);
    r1.y = r1.y + r1.y;
    r0.w = r1.y * r0.w;
    r3.x = GetLuminance(r3.xyz); // Rec.709
    r1.y = EvaluateCurve(_CurveLumVsSat, r3.x);
    r1.y = r1.y + r1.y;
    r0.w = r1.y * r0.w;
    r3.z = _HueSatCon.x + r2.x;
    r1.y = EvaluateCurve(_CurveHueVsHue, r3.z);
    r1.y = -0.5 + r1.y;
    r1.y = r3.z + r1.y;
    r1.z = cmp(r1.y < 0);
    r1.w = cmp(1 < r1.y);
    r2.xy = float2(1,-1) + r1.yy;
    r1.y = r1.w ? r2.y : r1.y;
    r1.y = r1.z ? r2.x : r1.y;
    r1.yzw = float3(1,0.666666687,0.333333343) + r1.yyy;
    r1.yzw = frac(r1.yzw);
    r1.yzw = r1.yzw * float3(6,6,6) + float3(-3,-3,-3);
    r1.yzw = saturate(float3(-1,-1,-1) + abs(r1.yzw));
    r1.yzw = float3(-1,-1,-1) + r1.yzw;
    r1.yzw = r2.zzz * r1.yzw + float3(1,1,1);
    r2.xyz = r1.xxx * r1.yzw;
    r2.x = dot(r2.xyz, float3(0.212672904,0.715152204,0.0721750036));
    r0.w = _HueSatCon.y * r0.w;
    r1.xyz = r1.xxx * r1.yzw + -r2.xxx;
    r1.xyz = r0.www * r1.xyz + r2.xxx;
    r0.w = max(r1.x, r1.y);
    r0.w = max(r0.w, r1.z);
    r0.w = 1 + r0.w;
    r0.w = rcp(r0.w);
    r1.xyz = r1.xyz * r0.www + float3(0.00390625, 0.00390625, 0.00390625);
    r2.x = EvaluateCurve(_CurveMaster, r1.x);
    r2.y = EvaluateCurve(_CurveMaster, r1.y);
    r2.z = EvaluateCurve(_CurveMaster, r1.z);
    r1.xyz = float3(0.00390625, 0.00390625, 0.00390625) + r2.xyz;
    r2.x = EvaluateCurve(_CurveRed, r1.x);
    r2.y = EvaluateCurve(_CurveGreen, r1.y);
    r2.z = EvaluateCurve(_CurveBlue, r1.z);
    r0.w = max(r2.x, r2.y);
    r0.w = max(r0.w, r2.z);
    r0.w = 1 + -r0.w;
    r0.w = rcp(r0.w);
    r1.xyz = r2.xyz * r0.www;
#if 0
    r1.xyz = max(float3(0,0,0), r1.xyz);
#endif
  }
  else // NeutralColorGrade() (skip grading)
  {
    r0.xyz = vThreadID.xyz * _Size.yyy + float3(-0.386036009,-0.386036009,-0.386036009);
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
  acesSettings.dark_to_dim = true;
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
	float3 oklch = linear_srgb_to_oklch(untonemapped);
	float3 vanillaTMOklab = linear_srgb_to_oklch(vanillaTM);
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
	untonemapped = oklch_to_linear_srgb(oklch);
#else
  bool restoreBrightness = true; // We get raised blacks without this
	untonemapped = RestoreHue(untonemapped, saturate(vanillaTM), 0.75, restoreBrightness);
#endif
   // TODO: do it with new oklab method and test skies being blue... then fix invalid colors (Note: this probably doesn't apply anymore)

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
	tonemapped = RestoreChrominance(tonemapped, tonemappedAlt);
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