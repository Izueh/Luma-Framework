cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[74] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[211] : packoffset(c0);
};

SamplerState s13 : register(s13);
SamplerState s14 : register(s14);
Texture2D<float4> t13 : register(t13);
Texture2D<float4> t14 : register(t14);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
    float _85 = asfloat(cb4_m[210u].x);
    float _94 = asfloat(cb4_m[210u].y);
    float _104 = mad((abs(_85) > 0.0f) ? (1.0f / _85) : 9.999999933815812510711506376258e+36f, 0.5f, v5.x);
    float _105 = mad((abs(_94) > 0.0f) ? (1.0f / _94) : 9.999999933815812510711506376258e+36f, 0.5f, v5.y);
    float4 _114 = t14.Sample(s14, float2(clamp(_104, 0.0f, 1.0f), clamp(_105, 0.0f, 1.0f)));
    float4 _149 = t13.Sample(s13, float2(_104, _105));
    float _168 = asfloat((asuint(_149.x) & cb3_m[70u].x) | cb3_m[71u].x) - 0.5f;
    float _169 = asfloat((asuint(_149.y) & cb3_m[70u].y) | cb3_m[71u].y) - 0.5f;
    float4 _184 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0040000001899898052215576171875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0040000001899898052215576171875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _215 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.008000000379979610443115234375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.008000000379979610443115234375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _254 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.01200000010430812835693359375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.01200000010430812835693359375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _281 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.01600000075995922088623046875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.01600000075995922088623046875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _320 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0199999995529651641845703125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0199999995529651641845703125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _347 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0240000002086162567138671875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0240000002086162567138671875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _386 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0280000008642673492431640625f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0280000008642673492431640625f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _413 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0320000015199184417724609375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0320000015199184417724609375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _452 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.03599999845027923583984375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.03599999845027923583984375f, _105), -0.0f), 0.0f, 1.0f)));
    float _473 = ((((((((asfloat((asuint(_114.x) & cb3_m[72u].x) | cb3_m[73u].x) + asfloat((asuint(_184.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_215.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_254.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_281.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_320.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_347.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_386.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_413.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_452.x) & cb3_m[72u].x) | cb3_m[73u].x);
    float _474 = asfloat((asuint(_452.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_413.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_386.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_347.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_320.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_281.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_254.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_215.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_114.y) & cb3_m[72u].y) | cb3_m[73u].y) + asfloat((asuint(_184.y) & cb3_m[72u].y) | cb3_m[73u].y)))))))));
    float _475 = asfloat((cb3_m[72u].z & asuint(_452.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_413.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_386.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_347.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_320.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_281.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_254.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_215.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_114.z)) | cb3_m[73u].z) + asfloat((cb3_m[72u].z & asuint(_184.z)) | cb3_m[73u].z)))))))));
    float _476 = asfloat((cb3_m[72u].w & asuint(_452.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_413.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_386.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_347.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_320.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_281.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_254.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_215.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_114.w)) | cb3_m[73u].w) + asfloat(cb3_m[73u].w | (cb3_m[72u].w & asuint(_184.w)))))))))));
    float4 _479 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.039999999105930328369140625f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.039999999105930328369140625f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _518 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0439999997615814208984375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0439999997615814208984375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _545 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.048000000417232513427734375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.048000000417232513427734375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _584 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.05200000107288360595703125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.05200000107288360595703125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _611 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.056000001728534698486328125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.056000001728534698486328125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _650 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0599999986588954925537109375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0599999986588954925537109375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _677 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.064000003039836883544921875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.064000003039836883544921875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _716 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.06800000369548797607421875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.06800000369548797607421875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _743 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.0719999969005584716796875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.0719999969005584716796875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _782 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.075999997556209564208984375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.075999997556209564208984375f, _105), -0.0f), 0.0f, 1.0f)));
    float _803 = (((((((((_473 + asfloat((asuint(_479.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_518.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_545.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_584.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_611.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_650.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_677.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_716.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_743.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_782.x) & cb3_m[72u].x) | cb3_m[73u].x);
    float _804 = asfloat((asuint(_782.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_743.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_716.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_677.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_650.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_611.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_584.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_545.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_518.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_479.y) & cb3_m[72u].y) | cb3_m[73u].y) + _474)))))))));
    float _805 = asfloat((cb3_m[72u].z & asuint(_782.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_743.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_716.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_677.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_650.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_611.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_584.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_545.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_518.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_479.z)) | cb3_m[73u].z) + _475)))))))));
    float _806 = asfloat((cb3_m[72u].w & asuint(_782.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_743.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_716.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_677.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_650.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_611.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_584.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_545.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_518.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_479.w)) | cb3_m[73u].w) + _476)))))))));
    float4 _809 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.07999999821186065673828125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.07999999821186065673828125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _848 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.083999998867511749267578125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.083999998867511749267578125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _875 = t14.Sample(s14, float2(clamp(max(mad(_168, 0.087999999523162841796875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, 0.087999999523162841796875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _906 = t14.Sample(s14, float2(clamp(mad(_168, 0.092000000178813934326171875f, _104), 0.0f, 1.0f), clamp(mad(_169, 0.092000000178813934326171875f, _105), 0.0f, 1.0f)));
    float4 _945 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.0040000001899898052215576171875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.0040000001899898052215576171875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _972 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.008000000379979610443115234375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.008000000379979610443115234375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _1011 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.01200000010430812835693359375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.01200000010430812835693359375f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _1038 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.01600000075995922088623046875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.01600000075995922088623046875f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _1081 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.0199999995529651641845703125f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.0199999995529651641845703125f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _1108 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.0240000002086162567138671875f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.0240000002086162567138671875f, _105), -0.0f), 0.0f, 1.0f)));
    float _1129 = (((((((((_803 + asfloat((asuint(_809.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_848.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_875.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_906.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_945.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_972.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_1011.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_1038.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_1081.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_1108.x) & cb3_m[72u].x) | cb3_m[73u].x);
    float _1130 = ((((((((asfloat((asuint(_848.y) & cb3_m[72u].y) | cb3_m[73u].y) + (asfloat((asuint(_809.y) & cb3_m[72u].y) | cb3_m[73u].y) + _804)) + asfloat((asuint(_875.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_906.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_945.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_972.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_1011.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_1038.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_1081.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_1108.y) & cb3_m[72u].y) | cb3_m[73u].y);
    float _1131 = ((((((((asfloat((cb3_m[72u].z & asuint(_848.z)) | cb3_m[73u].z) + (asfloat((cb3_m[72u].z & asuint(_809.z)) | cb3_m[73u].z) + _805)) + asfloat((cb3_m[72u].z & asuint(_875.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_906.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_945.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_972.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_1011.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_1038.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_1081.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_1108.z)) | cb3_m[73u].z);
    float _1132 = ((((((((asfloat((cb3_m[72u].w & asuint(_848.w)) | cb3_m[73u].w) + (asfloat((cb3_m[72u].w & asuint(_809.w)) | cb3_m[73u].w) + _806)) + asfloat((cb3_m[72u].w & asuint(_875.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_906.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_945.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_972.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_1011.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_1038.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_1081.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_1108.w)) | cb3_m[73u].w);
    float4 _1143 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.0280000008642673492431640625f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.0280000008642673492431640625f, _105), -0.0f), 0.0f, 1.0f)));
    float4 _1170 = t14.Sample(s14, float2(clamp(max(mad(_168, -0.0320000015199184417724609375f, _104), -0.0f), 0.0f, 1.0f), clamp(max(mad(_169, -0.0320000015199184417724609375f, _105), -0.0f), 0.0f, 1.0f)));
    o0.x = ((_1129 + asfloat((asuint(_1143.x) & cb3_m[72u].x) | cb3_m[73u].x)) + asfloat((asuint(_1170.x) & cb3_m[72u].x) | cb3_m[73u].x)) * 0.03125f;
    o0.y = ((_1130 + asfloat((asuint(_1143.y) & cb3_m[72u].y) | cb3_m[73u].y)) + asfloat((asuint(_1170.y) & cb3_m[72u].y) | cb3_m[73u].y)) * 0.03125f;
    o0.z = ((_1131 + asfloat((cb3_m[72u].z & asuint(_1143.z)) | cb3_m[73u].z)) + asfloat((cb3_m[72u].z & asuint(_1170.z)) | cb3_m[73u].z)) * 0.03125f;
    o0.w = ((_1132 + asfloat((cb3_m[72u].w & asuint(_1143.w)) | cb3_m[73u].w)) + asfloat((cb3_m[72u].w & asuint(_1170.w)) | cb3_m[73u].w)) * 0.03125f;
}