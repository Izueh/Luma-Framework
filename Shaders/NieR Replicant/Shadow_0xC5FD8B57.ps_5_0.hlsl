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

SamplerState texLinearDepth_s_s : register(s2);
SamplerComparisonState texShadow0_s_s : register(s0);
SamplerComparisonState texShadow1_s_s : register(s1);
Texture2D<float4> texShadow0 : register(t0);
Texture2D<float4> texShadow1 : register(t1);
Texture2D<float> texLinearDepth : register(t2);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float2 o0 : SV_TARGET0)
{
	float4 r0,r1,r2,r3,r4,r5,r6,r7;
	float4 fDest;

	r0.xy = dr_downscale.xy * v1.xy;
	r0.y = texLinearDepth.SampleLevel(texLinearDepth_s_s, r0.xy, 0).x;
	r1.z = -gFarClip * r0.y;
	r0.z = -r1.z < shadow_limit.y;
	if (r0.z != 0) {
		r0.zw = (v1.xy - LumaData.GameData.ProjectionJitters) * float2(2,-2) + float2(-1,1);
		r0.zw = gUVToView.xy * r0.zw;
		r1.xy = r0.yy * r0.zw;
		r0.z = dot(r1.xyz, gMtxInvView._m00_m01_m02);
		r2.x = gMtxInvView._m03 + r0.z;
		r0.z = dot(r1.xyz, gMtxInvView._m10_m11_m12);
		r2.y = gMtxInvView._m13 + r0.z;
		r0.z = dot(r1.xyz, gMtxInvView._m20_m21_m22);
		r2.z = gMtxInvView._m23 + r0.z;
		r0.z = -r1.z < split_position.x;
		if (r0.z != 0) {
			r1.xyz = camera_dir.xyz * camera_bias.xxx + r2.xyz;
			r1.w = 1;
			r3.x = dot(r1.xyzw, mtx_shadow[0]._m00_m10_m20_m30);
			r3.y = dot(r1.xyzw, mtx_shadow[0]._m01_m11_m21_m31);
			r3.z = dot(r1.xyzw, mtx_shadow[0]._m02_m12_m22_m32);
			r0.z = dot(r1.xyzw, mtx_shadow[0]._m03_m13_m23_m33);
			r0.w = 1.1920929e-07 < r0.z;
			r1.xyz = r3.xyz / r0.zzz;
			r1.xyz = r0.www ? r1.xyz : 0;
			r0.z = 0 < cascade_blend_band_far_range.x;
			r0.w = r0.y * -gFarClip + split_position.x;
			r0.w = saturate(r0.w / cascade_blend_band_far_range.x);
			r0.z = r0.z ? r0.w : 1;
			r0.w = 1.44269502 * r1.z;
			r0.w = exp2(r0.w);
			r0.w = -23 + r0.w;
			r0.w = exp2(r0.w);
			r0.w = saturate(constant_bias * r0.w + r1.z);
			texShadow0.GetDimensions(0, fDest.x, fDest.y, fDest.z);
			r1.zw = fDest.xy;
			r3.xy = float2(1,1) / r1.zw;
			r1.xy = r1.xy * r1.zw + float2(0.5,0.5);
			r1.zw = floor(r1.xy);
			r1.xy = r1.xy + -r1.zw;
			r1.zw = float2(-0.5,-0.5) + r1.zw;
			r1.zw = r1.zw * r3.xy;
			r3.zw = -r1.yx * float2(2,2) + float2(3,3);
			r3.zw = max(float2(9.99999975e-06,9.99999975e-06), r3.zw);
			r4.xy = r1.xy * float2(2,2) + float2(1,1);
			r4.xy = max(float2(9.99999975e-06,9.99999975e-06), r4.xy);
			r4.zw = float2(2,2) + -r1.xy;
			r4.zw = r4.zw / r3.wz;
			r5.zw = float2(-1,-1) + r4.zw;
			r1.xy = r1.xy / r4.xy;
			r5.xy = float2(1,1) + r1.xy;
			r1.x = r3.w * r3.z;
			r6.xyzw = r5.zwxw * r3.xyxy + r1.zwzw;
			r1.y = texShadow0.SampleCmpLevelZero(texShadow0_s_s, r6.xy, r0.w).x;
			r3.zw = r4.xy * r3.zw;
			r2.w = texShadow0.SampleCmpLevelZero(texShadow0_s_s, r6.zw, r0.w).x;
			r2.w = r3.z * r2.w;
			r1.x = r1.x * r1.y + r2.w;
			r5.xyzw = r5.zyxy * r3.xyxy + r1.zwzw;
			r1.y = texShadow0.SampleCmpLevelZero(texShadow0_s_s, r5.xy, r0.w).x;
			r1.x = r3.w * r1.y + r1.x;
			r1.y = r4.x * r4.y;
			r0.w = texShadow0.SampleCmpLevelZero(texShadow0_s_s, r5.zw, r0.w).x;
			r0.w = r1.y * r0.w + r1.x;
			r1.x = 0.0625 * r0.w;
			r1.y = r0.z < 1;
			if (r1.y != 0) {
				r3.xyz = camera_dir.xyz * camera_bias.yyy + r2.xyz;
				r3.w = 1;
				r4.x = dot(r3.xyzw, mtx_shadow[1]._m00_m10_m20_m30);
				r4.y = dot(r3.xyzw, mtx_shadow[1]._m01_m11_m21_m31);
				r4.z = dot(r3.xyzw, mtx_shadow[1]._m02_m12_m22_m32);
				r1.z = dot(r3.xyzw, mtx_shadow[1]._m03_m13_m23_m33);
				r1.w = 1.1920929e-07 < r1.z;
				r3.xyz = r4.xyz / r1.zzz;
				r3.xyz = r1.www ? r3.xyz : 0;
				r1.z = 1.44269502 * r3.z;
				r1.z = exp2(r1.z);
				r1.z = -23 + r1.z;
				r1.z = exp2(r1.z);
				r1.z = saturate(constant_bias1 * r1.z + r3.z);
				texShadow1.GetDimensions(0, fDest.x, fDest.y, fDest.z);
				r3.zw = fDest.xy;
				r4.xy = float2(1,1) / r3.zw;
				r3.xy = r3.xy * r3.zw + float2(0.5,0.5);
				r3.zw = floor(r3.xy);
				r3.xy = r3.xy + -r3.zw;
				r3.zw = float2(-0.5,-0.5) + r3.zw;
				r3.zw = r3.zw * r4.xy;
				r4.zw = -r3.yx * float2(2,2) + float2(3,3);
				r4.zw = max(float2(9.99999975e-06,9.99999975e-06), r4.zw);
				r5.xy = r3.xy * float2(2,2) + float2(1,1);
				r5.xy = max(float2(9.99999975e-06,9.99999975e-06), r5.xy);
				r5.zw = float2(2,2) + -r3.xy;
				r5.zw = r5.zw / r4.wz;
				r6.zw = float2(-1,-1) + r5.zw;
				r3.xy = r3.xy / r5.xy;
				r6.xy = float2(1,1) + r3.xy;
				r1.w = r4.w * r4.z;
				r7.xyzw = r6.zwxw * r4.xyxy + r3.zwzw;
				r2.w = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r7.xy, r1.z).x;
				r3.xy = r5.xy * r4.zw;
				r4.z = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r7.zw, r1.z).x;
				r3.x = r4.z * r3.x;
				r1.w = r1.w * r2.w + r3.x;
				r4.xyzw = r6.zyxy * r4.xyxy + r3.zwzw;
				r2.w = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r4.xy, r1.z).x;
				r1.w = r3.y * r2.w + r1.w;
				r2.w = r5.x * r5.y;
				r1.z = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r4.zw, r1.z).x;
				r1.z = r2.w * r1.z + r1.w;
				r1.z = 0.0625 * r1.z;
				r0.w = r0.w * 0.0625 + -r1.z;
				r0.z = r0.z * r0.w + r1.z;
			}
			r0.x = r1.y ? r0.z : r1.x;
		} else {
			r1.xyz = camera_dir.xyz * camera_bias.yyy + r2.xyz;
			r1.w = 1;
			r2.x = dot(r1.xyzw, mtx_shadow[1]._m00_m10_m20_m30);
			r2.y = dot(r1.xyzw, mtx_shadow[1]._m01_m11_m21_m31);
			r2.z = dot(r1.xyzw, mtx_shadow[1]._m02_m12_m22_m32);
			r0.z = dot(r1.xyzw, mtx_shadow[1]._m03_m13_m23_m33);
			r0.w = 1.1920929e-07 < r0.z;
			r1.xyz = r2.xyz / r0.zzz;
			r1.xyz = r0.www ? r1.xyz : 0;
			r0.z = 1.44269502 * r1.z;
			r0.z = exp2(r0.z);
			r0.z = -23 + r0.z;
			r0.z = exp2(r0.z);
			r0.z = saturate(constant_bias1 * r0.z + r1.z);
			texShadow1.GetDimensions(0, fDest.x, fDest.y, fDest.z);
			r1.zw = fDest.xy;
			r2.xy = float2(1,1) / r1.zw;
			r1.xy = r1.xy * r1.zw + float2(0.5,0.5);
			r1.zw = floor(r1.xy);
			r1.xy = r1.xy + -r1.zw;
			r1.zw = float2(-0.5,-0.5) + r1.zw;
			r1.zw = r1.zw * r2.xy;
			r2.zw = -r1.yx * float2(2,2) + float2(3,3);
			r2.zw = max(float2(9.99999975e-06,9.99999975e-06), r2.zw);
			r3.xy = r1.xy * float2(2,2) + float2(1,1);
			r3.xy = max(float2(9.99999975e-06,9.99999975e-06), r3.xy);
			r3.zw = float2(2,2) + -r1.xy;
			r3.zw = r3.zw / r2.wz;
			r4.zw = float2(-1,-1) + r3.zw;
			r1.xy = r1.xy / r3.xy;
			r4.xy = float2(1,1) + r1.xy;
			r0.w = r2.w * r2.z;
			r5.xyzw = r4.zwxw * r2.xyxy + r1.zwzw;
			r1.x = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r5.xy, r0.z).x;
			r2.zw = r3.xy * r2.zw;
			r1.y = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r5.zw, r0.z).x;
			r1.y = r2.z * r1.y;
			r0.w = r0.w * r1.x + r1.y;
			r1.xyzw = r4.zyxy * r2.xyxy + r1.zwzw;
			r1.x = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r1.xy, r0.z).x;
			r0.w = r2.w * r1.x + r0.w;
			r1.x = r3.x * r3.y;
			r0.z = texShadow1.SampleCmpLevelZero(texShadow1_s_s, r1.zw, r0.z).x;
			r0.z = r1.x * r0.z + r0.w;
			r0.x = 0.0625 * r0.z;
		}
	} else {
		r0.x = 1;
	}
	o0.xy = r0.xy;
	return;
}