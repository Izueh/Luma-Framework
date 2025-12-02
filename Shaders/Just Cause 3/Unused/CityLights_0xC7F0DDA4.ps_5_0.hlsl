cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

SamplerState LightTexture_s : register(s0);
Texture2D<float4> LightTexture : register(t0);

// Distant "billboard" lights drawn into the distance like in many open world games
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = Globals[5].y + -Globals[0].w;
  r0.x = r0.x * Globals[26].w + Globals[25].w;
  r0.y = -Globals[0].w + v2.y;
  r0.x = r0.y * r0.x;
  r0.x = max(0, r0.x);
  r0.x = 1 + r0.x;
  r0.yzw = -Globals[5].xyz + v2.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.x = r0.y / r0.x;
  r0.xyz = -Globals[26].xyz * r0.xxx;
  r0.xyz = float3(1.44269502,1.44269502,1.44269502) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.w = LightTexture.Sample(LightTexture_s, v1.xy).x;
  r1.xyz = v3.xyz * r0.www;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  o0.xyz = r1.xyz * r0.xyz;
  o0.w = 1;
}