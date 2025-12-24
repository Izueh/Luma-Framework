cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m[50] : packoffset(c0);
};

SamplerState s0 : register(s1);
SamplerState s1 : register(s2);
SamplerState s2 : register(s3);
Texture2D<float4> t0 : register(t4);
Texture2D<float4> t1 : register(t5);
Texture2D<float4> t2 : register(t6);

static float2 TEXCOORD;
static float2 TEXCOORD1;
static float2 TEXCOORD3;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
    float2 TEXCOORD : TEXCOORD0;
    float2 TEXCOORD1 : TEXCOORD1;
    float2 TEXCOORD3 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 SV_TARGET : SV_Target0;
};

float dp3_f32(precise float3 a, precise float3 b)
{
    precise float _89 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _89));
}

float dp4_f32(precise float4 a, precise float4 b)
{
    precise float _71 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _71)));
}

void frag_main()
{
    precise float4 _109 = t2.Sample(s2, float2(TEXCOORD3.x, TEXCOORD3.y));
    precise float _110 = _109.x;
    precise float _111 = _109.y;
    precise float _112 = _109.z;
    precise float _117 = asfloat(cb0_m[46u].x);
    precise float _120 = asfloat(cb0_m[46u].y);
    precise float _121 = _110 * _117;
    precise float _122 = _111 * _117;
    precise float _123 = _112 * _117;
    precise float _124 = _120 + _121;
    precise float _125 = _120 + _122;
    precise float _126 = _120 + _123;
    precise float _129 = asfloat(cb0_m[48u].x);
    precise float _132 = asfloat(cb0_m[48u].y);
    precise float _133 = _110 * _129;
    precise float _134 = _111 * _129;
    precise float _135 = _112 * _129;
    precise float _136 = _133 + _132;
    precise float _137 = _134 + _132;
    precise float _138 = _135 + _132;
    float _140 = dp3_f32(float3(_124, _125, _126), 0.3333333432674407958984375f.xxx);
    precise float _141 = _124 - _140;
    precise float _142 = _125 - _140;
    precise float _143 = _126 - _140;
    precise float _146 = asfloat(cb0_m[47u].x);
    precise float _147 = _141 * _146;
    precise float _148 = _146 * _142;
    precise float _149 = _146 * _143;
    precise float _150 = _140 + _147;
    precise float _151 = _140 + _148;
    precise float _152 = _140 + _149;
    precise float _158 = asfloat(cb0_m[46u].z);
    precise float _161 = asfloat(cb0_m[46u].w);
    precise float _162 = clamp(_150, 0.0f, 1.0f) * _158;
    precise float _163 = _158 * clamp(_151, 0.0f, 1.0f);
    precise float _164 = _158 * clamp(_152, 0.0f, 1.0f);
    precise float _165 = _162 + _161;
    precise float _166 = _161 + _163;
    precise float _167 = _161 + _164;
    float _169 = dp3_f32(float3(_136, _137, _138), 0.3333333432674407958984375f.xxx);
    precise float _170 = _136 - _169;
    precise float _171 = _137 - _169;
    precise float _172 = _138 - _169;
    precise float _175 = asfloat(cb0_m[49u].x);
    precise float _176 = _170 * _175;
    precise float _177 = _175 * _171;
    precise float _178 = _175 * _172;
    precise float _179 = _169 + _176;
    precise float _180 = _169 + _177;
    precise float _181 = _169 + _178;
    precise float _184 = asfloat(cb0_m[48u].z);
    precise float _187 = asfloat(cb0_m[48u].w);
    precise float _188 = _179 * _184;
    precise float _189 = _184 * _180;
    precise float _190 = _184 * _181;
    precise float _191 = _188 + _187;
    precise float _192 = _187 + _189;
    precise float _193 = _187 + _190;
    precise float4 _202 = t1.Sample(s1, float2(TEXCOORD1.x, TEXCOORD1.y));
    precise float _206 = _191 * _202.x;
    precise float _207 = _202.y * _192;
    precise float _208 = _202.z * _193;
    precise float _209 = _165 + _206;
    precise float _210 = _207 + _166;
    precise float _211 = _208 + _167;
    precise float _214 = asfloat(cb0_m[14u].y);
    precise float _215 = _209 * _214;
    precise float _216 = _214 * _210;
    precise float _217 = _214 * _211;
    precise float4 _229 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y));
    precise float _230 = _229.x;
    precise float _231 = _229.y;
    precise float _232 = _229.z;
    precise float _235 = asfloat(cb0_m[4u].x);
    precise float _236 = _230 + _235;
    precise float _237 = _231 + _235;
    precise float _238 = _232 + _235;
    precise float _239 = _236 * _230;
    precise float _240 = _231 * _237;
    precise float _241 = _232 * _238;
    precise float _242 = _239 + 1.0f;
    precise float _243 = _240 + 1.0f;
    precise float _244 = _241 + 1.0f;
    precise float _248 = _230 + sqrt(_242);
    precise float _249 = _231 + sqrt(_243);
    precise float _250 = _232 + sqrt(_244);
    precise float _267 = dp4_f32(_229, float4(asfloat(cb0_m[15u].x), asfloat(cb0_m[15u].y), asfloat(cb0_m[15u].z), asfloat(cb0_m[15u].w))) + asfloat(cb0_m[14u].w);
    SV_TARGET.w = clamp(_267, 0.0f, 1.0f);
    precise float _271 = _248 - 1.0f;
    precise float _272 = _249 - 1.0f;
    precise float _273 = _250 - 1.0f;
    precise float _276 = asfloat(cb0_m[4u].w);
    precise float _277 = _276 * _271;
    precise float _278 = _276 * _272;
    precise float _279 = _276 * _273;
    precise float _282 = asfloat(cb0_m[14u].x);
    precise float _283 = _277 * _282;
    precise float _284 = _278 * _282;
    precise float _285 = _279 * _282;
    precise float _286 = _191 * _283;
    precise float _287 = _284 * _192;
    precise float _288 = _285 * _193;
    precise float _291 = asfloat(cb0_m[12u].y);
    precise float _292 = _286 * _291;
    precise float _293 = _287 * _291;
    precise float _294 = _288 * _291;
    precise float _295 = _292 + 1.0f;
    precise float _296 = _293 + 1.0f;
    precise float _297 = _294 + 1.0f;
    precise float _300 = asfloat(cb0_m[12u].z);
    precise float _301 = _292 * _300;
    precise float _302 = _293 * _300;
    precise float _303 = _294 * _300;
    precise float _304 = _301 + 1.0f;
    precise float _305 = _302 + 1.0f;
    precise float _306 = _303 + 1.0f;
    precise float _307 = _292 * _304;
    precise float _308 = _305 * _293;
    precise float _309 = _306 * _294;
    precise float _310 = _307 / _295;
    precise float _311 = _308 / _296;
    precise float _312 = _309 / _297;
    precise float _313 = clamp(_310, 0.0f, 1.0f);
    precise float _314 = clamp(_311, 0.0f, 1.0f);
    precise float _315 = clamp(_312, 0.0f, 1.0f);
    precise float _316 = 1.0f - _313;
    precise float _317 = 1.0f - _314;
    precise float _318 = 1.0f - _315;
    precise float _319 = clamp(_215, 0.0f, 1.0f) * _316;
    precise float _320 = _317 * clamp(_216, 0.0f, 1.0f);
    precise float _321 = _318 * clamp(_217, 0.0f, 1.0f);
    precise float _322 = _313 + _319;
    precise float _323 = _320 + _314;
    precise float _324 = _321 + _315;
    SV_TARGET.x = _322;
    SV_TARGET.y = _323;
    SV_TARGET.z = _324;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD1 = stage_input.TEXCOORD1;
    TEXCOORD3 = stage_input.TEXCOORD3;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
