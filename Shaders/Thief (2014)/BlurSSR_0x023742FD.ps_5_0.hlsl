Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[2];
}

#define cmp

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21;
  r0.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r1.x = cmp(0.00999999978 < r0.x);
  r2.xyzw = cb0[1].xyxy * float4(-10,-10,-9,-9) + v0.xyxy;
  r1.yzw = t0.Sample(s0_s, r2.xy).xyw;
  r2.xyz = t0.Sample(s0_s, r2.zw).xyw;
  r3.xyzw = cb0[1].xyxy * float4(-8,-8,-7,-7) + v0.xyxy;
  r4.xyz = t0.Sample(s0_s, r3.xy).xyw;
  r3.xyz = t0.Sample(s0_s, r3.zw).xyw;
  r5.xyzw = cb0[1].xyxy * float4(-6,-6,-5,-5) + v0.xyxy;
  r6.xyz = t0.Sample(s0_s, r5.xy).xyw;
  r5.xyz = t0.Sample(s0_s, r5.zw).xyw;
  r7.xyzw = cb0[1].xyxy * float4(-4,-4,-3,-3) + v0.xyxy;
  r8.xyz = t0.Sample(s0_s, r7.xy).xyw;
  r7.xyz = t0.Sample(s0_s, r7.zw).xyw;
  r9.xyzw = cb0[1].xyxy * float4(-2,-2,3,3) + v0.xyxy;
  r10.xyz = t0.Sample(s0_s, r9.xy).xyw;
  r9.xy = -cb0[1].xy + v0.xy;
  r11.xyz = t0.Sample(s0_s, r9.xy).xyw;
  r9.xy = cb0[1].xy + v0.xy;
  r12.xyz = t0.Sample(s0_s, r9.xy).xyw;
  r9.xy = cb0[1].xy * float2(2,2) + v0.xy;
  r13.xyz = t0.Sample(s0_s, r9.xy).xyw;
  r9.xyz = t0.Sample(s0_s, r9.zw).xyw;
  r14.xyzw = cb0[1].xyxy * float4(4,4,5,5) + v0.xyxy;
  r15.xyz = t0.Sample(s0_s, r14.xy).xyw;
  r14.xyz = t0.Sample(s0_s, r14.zw).xyw;
  r16.xyzw = cb0[1].xyxy * float4(6,6,7,7) + v0.xyxy;
  r17.xyz = t0.Sample(s0_s, r16.xy).xyw;
  r16.xyz = t0.Sample(s0_s, r16.zw).xyw;
  r18.xyzw = cb0[1].xyxy * float4(8,8,9,9) + v0.xyxy;
  r19.xyz = t0.Sample(s0_s, r18.xy).xyw;
  r18.xyz = t0.Sample(s0_s, r18.zw).xyw;
  r20.xy = cb0[1].xy * float2(10,10) + v0.xy;
  r20.xyz = t0.Sample(s0_s, r20.xy).xyw;
  if (r1.x != 0) {
    o0.xyzw = r0.xyzw;
    return;
  }
  r2.w = cmp(0.00999999978 < r1.y);
  r1.yzw = r2.www ? r1.yzw : 0;
  r2.w = cmp(0.00999999978 < r2.x);
  r3.w = cmp(0.00999999978 < r1.y);
  r21.xyz = min(r1.yzw, r2.xyz);
  r2.xyz = r3.www ? r21.xyz : r2.xyz;
  r1.yzw = r2.www ? r2.xyz : r1.yzw;
  r2.x = cmp(0.00999999978 < r4.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r21.xyz = min(r1.yzw, r4.xyz);
  r2.yzw = r2.yyy ? r21.xyz : r4.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r3.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r4.xyz = min(r1.yzw, r3.xyz);
  r2.yzw = r2.yyy ? r4.xyz : r3.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r6.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r6.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r6.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r5.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r5.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r5.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r8.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r8.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r8.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r7.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r7.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r7.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r10.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r10.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r10.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r11.x);
  r2.y = cmp(0.00999999978 < r1.y);
  r3.xyz = min(r1.yzw, r11.xyz);
  r2.yzw = r2.yyy ? r3.xyz : r11.xyz;
  r1.yzw = r2.xxx ? r2.yzw : r1.yzw;
  r2.x = cmp(0.00999999978 < r1.y);
  r2.yzw = min(r1.yzw, r0.xyw);
  r0.xyw = r2.xxx ? r2.yzw : r0.xyw;
  r0.xyw = r1.xxx ? r0.xyw : r1.yzw;
  r1.x = cmp(0.00999999978 < r12.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r12.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r12.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r13.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r13.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r13.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r9.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r9.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r9.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r15.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r15.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r15.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r14.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r14.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r14.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r17.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r17.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r17.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r16.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r16.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r16.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r19.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r19.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r19.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r18.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r18.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r18.xyz;
  r0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  r1.x = cmp(0.00999999978 < r20.x);
  r1.y = cmp(0.00999999978 < r0.x);
  r2.xyz = min(r0.xyw, r20.xyz);
  r1.yzw = r1.yyy ? r2.xyz : r20.xyz;
  o0.xyw = r1.xxx ? r1.yzw : r0.xyw;
  o0.z = r0.z;
}