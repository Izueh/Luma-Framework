cbuffer PerInstanceCB : register(b2)
{
  float4 cb_positiontoviewtexture : packoffset(c0);
}

SamplerState smp_bilinearclamp_s : register(s0);
Texture2D<float4> ro_viewcolormap : register(t0);

#define cmp -

// Runs after tonemapping
void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;

  r0.xyz = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0).xyz;
  r1.xyz = ro_viewcolormap.Gather(smp_bilinearclamp_s, v0.xy).xyz;
  r2.xyz = ro_viewcolormap.Gather(smp_bilinearclamp_s, v0.xy, int2(-1, -1)).xzw;
  r0.w = max(r1.x, r0.y);
  r1.w = min(r1.x, r0.y);
  r0.w = max(r1.z, r0.w);
  r1.w = min(r1.z, r1.w);
  r2.w = max(r2.y, r2.x);
  r3.x = min(r2.y, r2.x);
  r0.w = max(r2.w, r0.w);
  r1.w = min(r3.x, r1.w);
  r2.w = 0.063000001 * r0.w;
  r0.w = -r1.w + r0.w;
  r1.w = max(0, r2.w);
  r1.w = cmp(r0.w >= r1.w);
  if (r1.w != 0) {
    r1.w = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0, int2(1, -1)).y;
    r2.w = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, v0.xy, 0, int2(-1, 1)).y;
    r3.xy = r2.yx + r1.xz;
    r0.w = 1 / r0.w;
    r3.z = r3.x + r3.y;
    r3.xy = r0.yy * float2(-2,-2) + r3.xy;
    r3.w = r1.w + r1.y;
    r1.w = r2.z + r1.w;
    r4.x = r1.z * -2 + r3.w;
    r1.w = r2.y * -2 + r1.w;
    r2.z = r2.z + r2.w;
    r1.y = r2.w + r1.y;
    r2.w = abs(r3.x) * 2 + abs(r4.x);
    r1.w = abs(r3.y) * 2 + abs(r1.w);
    r3.x = r2.x * -2 + r2.z;
    r1.y = r1.x * -2 + r1.y;
    r2.w = abs(r3.x) + r2.w;
    r1.y = abs(r1.y) + r1.w;
    r1.w = r2.z + r3.w;
    r1.y = cmp(r2.w >= r1.y);
    r1.w = r3.z * 2 + r1.w;
    r2.x = r1.y ? r2.y : r2.x;
    r1.x = r1.y ? r1.x : r1.z;
    r1.z = r1.y ? cb_positiontoviewtexture.w : cb_positiontoviewtexture.z;
    r1.w = r1.w * 0.0833333358 + -r0.y;
    r2.y = r2.x + -r0.y;
    r2.z = r1.x + -r0.y;
    r2.x = r2.x + r0.y;
    r1.x = r1.x + r0.y;
    r2.w = cmp(abs(r2.y) >= abs(r2.z));
    r2.y = max(abs(r2.y), abs(r2.z));
    r1.z = r2.w ? -r1.z : r1.z;
    r0.w = saturate(abs(r1.w) * r0.w);
    r1.w = r1.y ? cb_positiontoviewtexture.z : 0;
    r2.z = r1.y ? 0 : cb_positiontoviewtexture.w;
    r3.xy = r1.zz * float2(0.5,0.5) + v0.xy;
    r3.x = r1.y ? v0.x : r3.x;
    r3.y = r1.y ? r3.y : v0.y;
    r4.x = r3.x + -r1.w;
    r4.y = r3.y + -r2.z;
    r5.x = r3.x + r1.w;
    r5.y = r3.y + r2.z;
    r3.x = r0.w * -2 + 3;
    r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xy, 0).y;
    r0.w = r0.w * r0.w;
    r3.z = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r5.xy, 0).y;
    r1.x = r2.w ? r2.x : r1.x;
    r2.x = 0.25 * r2.y;
    r2.y = -r1.x * 0.5 + r0.y;
    r0.w = r3.x * r0.w;
    r2.y = cmp(r2.y < 0);
    r3.x = -r1.x * 0.5 + r3.y;
    r3.y = -r1.x * 0.5 + r3.z;
    r3.zw = cmp(abs(r3.xy) >= r2.xx);
    r2.w = r4.x + -r1.w;
    r4.x = r3.z ? r4.x : r2.w;
    r2.w = r4.y + -r2.z;
    r4.z = r3.z ? r4.y : r2.w;
    r4.yw = ~(int2)r3.zw;
    r2.w = (int)r4.w | (int)r4.y;
    r4.y = r5.x + r1.w;
    r4.y = r3.w ? r5.x : r4.y;
    r5.x = r5.y + r2.z;
    r4.w = r3.w ? r5.y : r5.x;
    if (r2.w != 0) {
      if (r3.z == 0) {
        r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
      }
      if (r3.w == 0) {
        r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
      }
      r2.w = -r1.x * 0.5 + r3.x;
      r3.x = r3.z ? r3.x : r2.w;
      r2.w = -r1.x * 0.5 + r3.y;
      r3.y = r3.w ? r3.y : r2.w;
      r3.zw = cmp(abs(r3.xy) >= r2.xx);
      r2.w = r4.x + -r1.w;
      r4.x = r3.z ? r4.x : r2.w;
      r2.w = r4.z + -r2.z;
      r4.z = r3.z ? r4.z : r2.w;
      r5.xy = ~(int2)r3.zw;
      r2.w = (int)r5.y | (int)r5.x;
      r5.x = r4.y + r1.w;
      r4.y = r3.w ? r4.y : r5.x;
      r5.x = r4.w + r2.z;
      r4.w = r3.w ? r4.w : r5.x;
      if (r2.w != 0) {
        if (r3.z == 0) {
          r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
        }
        if (r3.w == 0) {
          r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
        }
        r2.w = -r1.x * 0.5 + r3.x;
        r3.x = r3.z ? r3.x : r2.w;
        r2.w = -r1.x * 0.5 + r3.y;
        r3.y = r3.w ? r3.y : r2.w;
        r3.zw = cmp(abs(r3.xy) >= r2.xx);
        r2.w = r4.x + -r1.w;
        r4.x = r3.z ? r4.x : r2.w;
        r2.w = r4.z + -r2.z;
        r4.z = r3.z ? r4.z : r2.w;
        r5.xy = ~(int2)r3.zw;
        r2.w = (int)r5.y | (int)r5.x;
        r5.x = r4.y + r1.w;
        r4.y = r3.w ? r4.y : r5.x;
        r5.x = r4.w + r2.z;
        r4.w = r3.w ? r4.w : r5.x;
        if (r2.w != 0) {
          if (r3.z == 0) {
            r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
          }
          if (r3.w == 0) {
            r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
          }
          r2.w = -r1.x * 0.5 + r3.x;
          r3.x = r3.z ? r3.x : r2.w;
          r2.w = -r1.x * 0.5 + r3.y;
          r3.y = r3.w ? r3.y : r2.w;
          r3.zw = cmp(abs(r3.xy) >= r2.xx);
          r2.w = r4.x + -r1.w;
          r4.x = r3.z ? r4.x : r2.w;
          r2.w = r4.z + -r2.z;
          r4.z = r3.z ? r4.z : r2.w;
          r5.xy = ~(int2)r3.zw;
          r2.w = (int)r5.y | (int)r5.x;
          r5.x = r4.y + r1.w;
          r4.y = r3.w ? r4.y : r5.x;
          r5.x = r4.w + r2.z;
          r4.w = r3.w ? r4.w : r5.x;
          if (r2.w != 0) {
            if (r3.z == 0) {
              r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
            }
            if (r3.w == 0) {
              r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
            }
            r2.w = -r1.x * 0.5 + r3.x;
            r3.x = r3.z ? r3.x : r2.w;
            r2.w = -r1.x * 0.5 + r3.y;
            r3.y = r3.w ? r3.y : r2.w;
            r3.zw = cmp(abs(r3.xy) >= r2.xx);
            r2.w = -r1.w * 1.5 + r4.x;
            r4.x = r3.z ? r4.x : r2.w;
            r2.w = -r2.z * 1.5 + r4.z;
            r4.z = r3.z ? r4.z : r2.w;
            r5.xy = ~(int2)r3.zw;
            r2.w = (int)r5.y | (int)r5.x;
            r5.x = r1.w * 1.5 + r4.y;
            r4.y = r3.w ? r4.y : r5.x;
            r5.x = r2.z * 1.5 + r4.w;
            r4.w = r3.w ? r4.w : r5.x;
            if (r2.w != 0) {
              if (r3.z == 0) {
                r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
              }
              if (r3.w == 0) {
                r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
              }
              r2.w = -r1.x * 0.5 + r3.x;
              r3.x = r3.z ? r3.x : r2.w;
              r2.w = -r1.x * 0.5 + r3.y;
              r3.y = r3.w ? r3.y : r2.w;
              r3.zw = cmp(abs(r3.xy) >= r2.xx);
              r2.w = -r1.w * 2 + r4.x;
              r4.x = r3.z ? r4.x : r2.w;
              r2.w = -r2.z * 2 + r4.z;
              r4.z = r3.z ? r4.z : r2.w;
              r5.xy = ~(int2)r3.zw;
              r2.w = (int)r5.y | (int)r5.x;
              r5.x = r1.w * 2 + r4.y;
              r4.y = r3.w ? r4.y : r5.x;
              r5.x = r2.z * 2 + r4.w;
              r4.w = r3.w ? r4.w : r5.x;
              if (r2.w != 0) {
                if (r3.z == 0) {
                  r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
                }
                if (r3.w == 0) {
                  r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
                }
                r2.w = -r1.x * 0.5 + r3.x;
                r3.x = r3.z ? r3.x : r2.w;
                r2.w = -r1.x * 0.5 + r3.y;
                r3.y = r3.w ? r3.y : r2.w;
                r3.zw = cmp(abs(r3.xy) >= r2.xx);
                r2.w = -r1.w * 2 + r4.x;
                r4.x = r3.z ? r4.x : r2.w;
                r2.w = -r2.z * 2 + r4.z;
                r4.z = r3.z ? r4.z : r2.w;
                r5.xy = ~(int2)r3.zw;
                r2.w = (int)r5.y | (int)r5.x;
                r5.x = r1.w * 2 + r4.y;
                r4.y = r3.w ? r4.y : r5.x;
                r5.x = r2.z * 2 + r4.w;
                r4.w = r3.w ? r4.w : r5.x;
                if (r2.w != 0) {
                  if (r3.z == 0) {
                    r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
                  }
                  if (r3.w == 0) {
                    r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
                  }
                  r2.w = -r1.x * 0.5 + r3.x;
                  r3.x = r3.z ? r3.x : r2.w;
                  r2.w = -r1.x * 0.5 + r3.y;
                  r3.y = r3.w ? r3.y : r2.w;
                  r3.zw = cmp(abs(r3.xy) >= r2.xx);
                  r2.w = -r1.w * 2 + r4.x;
                  r4.x = r3.z ? r4.x : r2.w;
                  r2.w = -r2.z * 2 + r4.z;
                  r4.z = r3.z ? r4.z : r2.w;
                  r5.xy = ~(int2)r3.zw;
                  r2.w = (int)r5.y | (int)r5.x;
                  r5.x = r1.w * 2 + r4.y;
                  r4.y = r3.w ? r4.y : r5.x;
                  r5.x = r2.z * 2 + r4.w;
                  r4.w = r3.w ? r4.w : r5.x;
                  if (r2.w != 0) {
                    if (r3.z == 0) {
                      r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
                    }
                    if (r3.w == 0) {
                      r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
                    }
                    r2.w = -r1.x * 0.5 + r3.x;
                    r3.x = r3.z ? r3.x : r2.w;
                    r2.w = -r1.x * 0.5 + r3.y;
                    r3.y = r3.w ? r3.y : r2.w;
                    r3.zw = cmp(abs(r3.xy) >= r2.xx);
                    r2.w = -r1.w * 2 + r4.x;
                    r4.x = r3.z ? r4.x : r2.w;
                    r2.w = -r2.z * 2 + r4.z;
                    r4.z = r3.z ? r4.z : r2.w;
                    r5.xy = ~(int2)r3.zw;
                    r2.w = (int)r5.y | (int)r5.x;
                    r5.x = r1.w * 2 + r4.y;
                    r4.y = r3.w ? r4.y : r5.x;
                    r5.x = r2.z * 2 + r4.w;
                    r4.w = r3.w ? r4.w : r5.x;
                    if (r2.w != 0) {
                      if (r3.z == 0) {
                        r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
                      }
                      if (r3.w == 0) {
                        r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
                      }
                      r2.w = -r1.x * 0.5 + r3.x;
                      r3.x = r3.z ? r3.x : r2.w;
                      r2.w = -r1.x * 0.5 + r3.y;
                      r3.y = r3.w ? r3.y : r2.w;
                      r3.zw = cmp(abs(r3.xy) >= r2.xx);
                      r2.w = -r1.w * 4 + r4.x;
                      r4.x = r3.z ? r4.x : r2.w;
                      r2.w = -r2.z * 4 + r4.z;
                      r4.z = r3.z ? r4.z : r2.w;
                      r5.xy = ~(int2)r3.zw;
                      r2.w = (int)r5.y | (int)r5.x;
                      r5.x = r1.w * 4 + r4.y;
                      r4.y = r3.w ? r4.y : r5.x;
                      r5.x = r2.z * 4 + r4.w;
                      r4.w = r3.w ? r4.w : r5.x;
                      if (r2.w != 0) {
                        if (r3.z == 0) {
                          r3.x = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.xz, 0).y;
                        }
                        if (r3.w == 0) {
                          r3.y = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r4.yw, 0).y;
                        }
                        r2.w = -r1.x * 0.5 + r3.x;
                        r3.x = r3.z ? r3.x : r2.w;
                        r1.x = -r1.x * 0.5 + r3.y;
                        r3.y = r3.w ? r3.y : r1.x;
                        r2.xw = cmp(abs(r3.xy) >= r2.xx);
                        r1.x = -r1.w * 8 + r4.x;
                        r4.x = r2.x ? r4.x : r1.x;
                        r1.x = -r2.z * 8 + r4.z;
                        r4.z = r2.x ? r4.z : r1.x;
                        r1.x = r1.w * 8 + r4.y;
                        r4.y = r2.w ? r4.y : r1.x;
                        r1.x = r2.z * 8 + r4.w;
                        r4.w = r2.w ? r4.w : r1.x;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    r1.x = v0.x + -r4.x;
    r1.w = -v0.x + r4.y;
    r2.x = v0.y + -r4.z;
    r1.x = r1.y ? r1.x : r2.x;
    r2.x = -v0.y + r4.w;
    r1.w = r1.y ? r1.w : r2.x;
    r2.xz = cmp(r3.xy < float2(0,0));
    r2.w = r1.w + r1.x;
    r2.xy = cmp((int2)r2.yy != (int2)r2.xz);
    r2.z = 1 / r2.w;
    r2.w = cmp(r1.x < r1.w);
    r1.x = min(r1.x, r1.w);
    r1.w = r2.w ? r2.x : r2.y;
    r0.w = r0.w * r0.w;
    r1.x = r1.x * -r2.z + 0.5;
    r0.w = 0.75 * r0.w;
    r1.x = (int)r1.x & (int)r1.w;
    r0.w = max(r1.x, r0.w);
    r1.xz = r0.ww * r1.zz + v0.xy;
    r2.x = r1.y ? v0.x : r1.x;
    r2.y = r1.y ? r1.z : v0.y;
    r0.xyz = ro_viewcolormap.SampleLevel(smp_bilinearclamp_s, r2.xy, 0).xyz;
  }
  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}