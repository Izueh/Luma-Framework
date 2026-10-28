#include "Includes/Common.hlsl"

cbuffer SystemCbScene : register(b0)
{
	row_major float3x4 gMtxView : packoffset(c0);
	float4x4 gMtxProj : packoffset(c3);
	float4x4 gMtxViewProj : packoffset(c7);
	row_major float3x4 gMtxInvView : packoffset(c11);
	float4x4 gMtxInvProj : packoffset(c14);
	float4x4 gMtxInvViewProj : packoffset(c18);
	row_major float3x4 gMtxPrevView : packoffset(c22);
	float4x4 gMtxPrevProj : packoffset(c25);
	float4x4 gMtxPrevViewProj : packoffset(c29);
	row_major float3x4 gMtxPrevInvView : packoffset(c33);
	float4x4 gMtxPrevInvProj : packoffset(c36);
	float4x4 gMtxPrevInvViewProj : packoffset(c40);
	float2 gUVToView : packoffset(c44);
	float gNearClip : packoffset(c44.z);
	float gFarClip : packoffset(c44.w);
	float3 gCameraPos : packoffset(c45);
	float gCameraFov : packoffset(c45.w);
	float gTime : packoffset(c46);
	float gDeltaTime : packoffset(c46.y);
	float2 gScreenSize : packoffset(c46.z);
	float3 gFrustumCorner : packoffset(c47);
	float gExposureNormalizeFactor : packoffset(c47.w);
	float gLeafDiffuseAlgorithmMix : packoffset(c48);
	float gGiLightEvaluateScale : packoffset(c48.y);
	float2 gEyeLightFadeDistance : packoffset(c48.z);
	float3 gCameraDir : packoffset(c49);
	float gEyeLightScale : packoffset(c49.w);
	float gSkinDualLobeSpecularMix : packoffset(c50);
	float gEyeSpecularityForIBL : packoffset(c50.y);
	float gScleraRoughnessMix : packoffset(c50.z);
	float gScleraRoughness : packoffset(c50.w);
	float gScleraLightScale : packoffset(c51);
	float3 gIrisCausticsLightDir : packoffset(c51.y);
	float gIlluminanceEmissiveInfluence : packoffset(c52);
	float gEyeEmissiveCubeScale : packoffset(c52.y);
	float gIrisCausticsRatio : packoffset(c52.z);
	int gIrisCausticsOverride : packoffset(c52.w);
	float gIrisConcavityScaleParam : packoffset(c53);
	float gIrisConcavityPowerParam : packoffset(c53.y);
	float4 gEyeFixedLightOffsetDirection : packoffset(c54);
	float3 gCameraUp : packoffset(c55);
	float3 gCameraSide : packoffset(c56);
	float gEyeSpecularityScale : packoffset(c56.w);
}

cbuffer CbShadow : register(b1)
{
	int split_count : packoffset(c0);
	float constant_bias : packoffset(c0.y);
	float slope_scale_bias : packoffset(c0.z);
	float normal_offset_scale : packoffset(c0.w);
	float4 split_position : packoffset(c1);
	float3 shadow_color : packoffset(c2);
	float2 shadow_limit : packoffset(c3);
	float2 shadow_map_size : packoffset(c3.z);
	float4x4 mtx_shadow[4] : packoffset(c4);
	float4 cascade_blend_band_far_range : packoffset(c20);
	float4 camera_bias : packoffset(c21);
	float3 camera_dir : packoffset(c22);
	float constant_bias1 : packoffset(c22.w);
	float constant_bias2 : packoffset(c23);
	float constant_bias3 : packoffset(c23.y);
}

cbuffer CbDynamicResolution : register(b2)
{
	float2 dynamic_resolution_size : packoffset(c0);
	float2 static_resolution_size : packoffset(c0.z);
	float2 dr_downscale : packoffset(c1);
	float2 dr_inv_downscale : packoffset(c1.z);
}

SamplerState texDepth_s_s : register(s0);
SamplerState texError_s_s : register(s1);
Texture2D<float> texDepth : register(t0);
Texture2D<float4> texError : register(t1);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	float4 r0,r1,r2,r3,r4;

	r0.xy = dr_downscale.xy * v1.xy;
	r0.z = texDepth.SampleLevel(texDepth_s_s, r0.xy, 0).x;
	r0.xy = (r0.xy  - LumaData.GameData.ProjectionJitters) * float2(2,-2) + float2(-1,1);
	r0.xy = gUVToView.xy * r0.xy;
	r1.xy = r0.zz * r0.xy;
	r1.z = -gFarClip * r0.z;
	r0.x = dot(r1.xyz, gMtxInvView._m00_m01_m02);
	r0.x = gMtxInvView._m03 + r0.x;
	r1.w = dot(r1.xyz, gMtxInvView._m10_m11_m12);
	r0.y = gMtxInvView._m13 + r1.w;
	r1.x = dot(r1.xyz, gMtxInvView._m20_m21_m22);
	r0.z = gMtxInvView._m23 + r1.x;
	r1.x = 0 < split_count;
	r2 = 0.0f;
	if (r1.x != 0) {
		r1.y = -r1.z < split_position.x;
		if (r1.y != 0) {
			r0.w = 1;
			r2.x = dot(r0.xyzw, mtx_shadow[0]._m00_m10_m20_m30);
			r2.y = dot(r0.xyzw, mtx_shadow[0]._m01_m11_m21_m31);
			r1.w = dot(r0.xyzw, mtx_shadow[0]._m03_m13_m23_m33);
			r2.z = 1.1920929e-07 < r1.w;
			r2.xy = r2.xy / r1.ww;
			r2.xy = r2.zz ? r2.xy : 0;
			r2.zw = ddx_fine(r2.xy);
			r2.xy = ddy_fine(r2.xy);
			r2.xy = shadow_map_size.yy * r2.xy;
			r3.xy = shadow_map_size.xx * r2.zw + r2.xy;
			r1.w = dot(r3.xy, r3.xy);
			r1.w = sqrt(r1.w);
			r2.xy = shadow_map_size.xx * r2.zw + -r2.xy;
			r2.x = dot(r2.xy, r2.xy);
			r2.x = sqrt(r2.x);
			r1.w = max(r2.x, r1.w);
			r1.w = 0.100000001 * r1.w;
			r2.xyz = texError.Sample(texError_s_s, r1.ww).xyz;
			r2.w = 1;
		}
		r1.w = ~(int)r1.y;
		r1.w = (int)r1.w & 1;
		r3.x = r1.y;
	} else {
		r3.x = 0;
		r1.yw = float2(0,-1);
	}
	r1.y = ~(int)r1.y;
	r3.y = ~(int)r3.x;
	r1.y = (int)r1.y & (int)r3.y;
	r1.x = r1.x ? r1.y : 0;
	r1.y = (int)r1.w < split_count;
	r1.x = r1.y ? r1.x : 0;
	if (r1.x != 0) {
		r1.y = -r1.z < split_position.y;
		if (r1.y != 0) {
			r0.w = 1;
			r4.x = dot(r0.xyzw, mtx_shadow[1]._m00_m10_m20_m30);
			r4.y = dot(r0.xyzw, mtx_shadow[1]._m01_m11_m21_m31);
			r3.y = dot(r0.xyzw, mtx_shadow[1]._m03_m13_m23_m33);
			r3.z = 1.1920929e-07 < r3.y;
			r3.yw = r4.xy / r3.yy;
			r3.yz = r3.zz ? r3.yw : 0;
			r4.xy = ddx_fine(r3.yz);
			r3.yz = ddy_fine(r3.yz);
			r3.yz = shadow_map_size.yy * r3.yz;
			r4.zw = shadow_map_size.xx * r4.xy + r3.yz;
			r3.w = dot(r4.zw, r4.zw);
			r3.yz = shadow_map_size.xx * r4.xy + -r3.yz;
			r3.y = dot(r3.yz, r3.yz);
			r3.yw = sqrt(r3.yw);
			r3.y = max(r3.w, r3.y);
			r3.y = 0.100000001 * r3.y;
			r2.xyz = texError.Sample(texError_s_s, r3.yy).xyz;
			r2.w = 1;
		}
		r1.w = r1.y ? 1 : 2;
		r3.x = r1.y;
	} else {
		r1.y = -1;
	}
	r1.y = ~(int)r1.y;
	r3.y = ~(int)r3.x;
	r1.y = (int)r1.y & (int)r3.y;
	r1.x = r1.x ? r1.y : 0;
	r1.y = (int)r1.w < split_count;
	r1.x = r1.y ? r1.x : 0;
	if (r1.x != 0) {
		r1.y = -r1.z < split_position.z;
		if (r1.y != 0) {
			r0.w = 1;
			r4.x = dot(r0.xyzw, mtx_shadow[2]._m00_m10_m20_m30);
			r4.y = dot(r0.xyzw, mtx_shadow[2]._m01_m11_m21_m31);
			r3.y = dot(r0.xyzw, mtx_shadow[2]._m03_m13_m23_m33);
			r3.z = 1.1920929e-07 < r3.y;
			r3.yw = r4.xy / r3.yy;
			r3.yz = r3.zz ? r3.yw : 0;
			r4.xy = ddx_fine(r3.yz);
			r3.yz = ddy_fine(r3.yz);
			r3.yz = shadow_map_size.yy * r3.yz;
			r4.zw = shadow_map_size.xx * r4.xy + r3.yz;
			r3.w = dot(r4.zw, r4.zw);
			r3.yz = shadow_map_size.xx * r4.xy + -r3.yz;
			r3.y = dot(r3.yz, r3.yz);
			r3.yw = sqrt(r3.yw);
			r3.y = max(r3.w, r3.y);
			r3.y = 0.100000001 * r3.y;
			r2.xyz = texError.Sample(texError_s_s, r3.yy).xyz;
			r2.w = 1;
		}
		r1.w = r1.y ? 2 : 3;
		r3.x = r1.y;
	} else {
		r1.y = -1;
	}
	r1.y = ~(int)r1.y;
	r3.y = ~(int)r3.x;
	r1.y = (int)r1.y & (int)r3.y;
	r1.x = r1.x ? r1.y : 0;
	r1.y = (int)r1.w < split_count;
	r1.x = r1.y ? r1.x : 0;
	if (r1.x != 0) {
		r3.x = -r1.z < split_position.w;
		if (r3.x != 0) {
			r0.w = 1;
			r1.x = dot(r0.xyzw, mtx_shadow[3]._m00_m10_m20_m30);
			r1.y = dot(r0.xyzw, mtx_shadow[3]._m01_m11_m21_m31);
			r0.x = dot(r0.xyzw, mtx_shadow[3]._m03_m13_m23_m33);
			r0.y = 1.1920929e-07 < r0.x;
			r0.xz = r1.xy / r0.xx;
			r0.xy = r0.yy ? r0.xz : 0;
			r0.zw = ddx_fine(r0.xy);
			r0.xy = ddy_fine(r0.xy);
			r0.xy = shadow_map_size.yy * r0.xy;
			r1.xy = shadow_map_size.xx * r0.zw + r0.xy;
			r1.x = dot(r1.xy, r1.xy);
			r1.x = sqrt(r1.x);
			r0.xy = shadow_map_size.xx * r0.zw + -r0.xy;
			r0.x = dot(r0.xy, r0.xy);
			r0.x = sqrt(r0.x);
			r0.x = max(r1.x, r0.x);
			r0.x = 0.100000001 * r0.x;
			r0.xyz = texError.Sample(texError_s_s, r0.xx).xyz;
			o0.xyz = r0.xyz;
			o0.w = 1;
		} else {
			o0.xyzw = r2.xyzw;
		}
	} else {
		o0.xyzw = r2.xyzw;
	}
	if (r3.x != 0) return;
	o0.xyzw = float4(0,0,0,0);
	return;
}