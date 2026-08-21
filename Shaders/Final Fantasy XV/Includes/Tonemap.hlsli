#include "neutwo.hlsl"
#include "Common.hlsl"
#include "../../Includes/ColorGradingLUT.hlsl"

// Internal helper for log safety
float SafeLog(float x) { return log(max(x, 1e-6)); }

// --- 1. Forward Tonemapping Function ---
float FFXV(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    const float a = 39.8107185;
    float u = InvLog * log(a * x + ZeroSlope);
    float up = pow(u, Param_n37);
    return Param_n46 * log(Contrast * up + 1.0) - Param_n49;
}

float3 FFXV(float3 color, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    return float3(
        FFXV(color.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV(color.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV(color.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49)
    );
}

// --- 1b. Inverse Tonemapping Function ---
float FFXV_Inverse(float y, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    float up = (exp((y + Param_n49) / Param_n46) - 1.0) / Contrast;
    float u  = pow(max(up, 0.0), 1.0 / Param_n37);
    return (exp(u / InvLog) - ZeroSlope) / 39.8107185;
}

float3 FFXV_Inverse(float3 y, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    return float3(
        FFXV_Inverse(y.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV_Inverse(y.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV_Inverse(y.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49)
    );
}

// --- 1c. Recover the game's SDR curve tuning from the HDR-mode uploads ---
// The game computes the HDR tuning from the SDR tuning by a fixed rule (verified
// bit-identical across multiple scene captures, SDR vs HDR display mode):
//   (K_hdr + 1) = 3.988391 * (K_sdr + 1)
//   10^D_hdr    = 2 * 10^D_sdr          (D = Disposition, inside InvLog)
//   p_hdr       = p_sdr + 0.34526
//   n46_hdr     = 0.507594 * n46_sdr
//   n49_hdr     = 0.606713 * n49_sdr
//   ZeroSlope, HighRange: identical in both modes
// Inverting gives the SDR curve from whatever HDR params the game uploads, so the
// SDR look is preserved even if the game ever changes its constants.
void RecoverSDRParams(
    float Contrast_hdr, float InvLog_hdr, float ZeroSlope_hdr, float Param_n37_hdr,
    float Param_n46_hdr, float Param_n49_hdr,
    out float Contrast, out float InvLog, out float ZeroSlope,
    out float Param_n37, out float Param_n46, out float Param_n49)
{
    Contrast  = (Contrast_hdr + 1.0) / 3.988391 - 1.0;
    InvLog    = 1.0 / log((exp(1.0 / InvLog_hdr) - 1.0) / 2.0 + 1.0);
    ZeroSlope = ZeroSlope_hdr;
    Param_n37 = Param_n37_hdr - 0.34526;
    Param_n46 = Param_n46_hdr / 0.507594;
    Param_n49 = Param_n49_hdr / 0.606713;
}

// --- 2. First Derivative (Slope) ---
float FFXV_d1(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    const float a = 39.8107185;
    float axb = a * x + ZeroSlope;
    float u = InvLog * log(axb);
    float up = pow(u, Param_n37);
    // (u^p)' = p * u^(p-1) * (InvLog * a / (ax + b))
    float up_p = Param_n37 * pow(u, Param_n37 - 1.0) * (InvLog * a / axb);
    float denom = 1.0 + Contrast * up;
    return Param_n46 * (Contrast * up_p) / denom;
}

// --- 3. Second Derivative (Curvature) ---
float FFXV_d2(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    const float a = 39.8107185;
    float axb = max(a * x + ZeroSlope, 1e-6);
    float g = log(axb);
    float gSafe = max(g, 1e-6); // prevent u < 0 at low x where log(axb) < 0
    float u = InvLog * gSafe;
    float up = pow(u, Param_n37);
    float up_p = Param_n37 * pow(u, Param_n37 - 1.0) * (InvLog * a / axb);
    // up_pp = p * inv^2 * a^2 / (ax+b)^2 * u^(p-2) * ((p-1) - gSafe)
    float up_pp = Param_n37 * InvLog * InvLog * (a * a) / (axb * axb) *
                  pow(u, Param_n37 - 2.0) * ((Param_n37 - 1.0) - gSafe);
    float denom = 1.0 + Contrast * up;
    return Param_n46 * ((Contrast * up_pp) / denom - (Contrast * Contrast * up_p * up_p) / (denom * denom));
}

// --- 4. Third Derivative (Numerical Approximation for Root Finding) ---
float FFXV_d3(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    float eps = max(x * 1e-4, 1e-7);
    return (FFXV_d2(x + eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46) - 
            FFXV_d2(x - eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)) / (2.0 * eps);
}

// --- 5. Newton-Bisection Root Finder ---
// rootOrder: 2 = y''=0 (true inflection), 3 = y'''=0 (max curvature, better linear pivot)
// For rootOrder 3 the y''=0 inflection is computed first, then the y'''=0 root is
// searched on [inflection, xmax] with a fallback to the inflection if none is found
// (same semantics as renodx FindInflection_FFXV precision 2).
// Phase 1: log-space scan seeds a guaranteed sign-change bracket.
// Phase 2: Newton step with bisection fallback refines inside the bracket.
// Returns -1 if no sign change is found in [xmin, xmax].
float Find_Inflection(float xmin, float xmax, int scanSteps, int bisectIters,
                    float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46,
                    int rootOrder)
{
    // Phase 1: find y''=0 sign-change bracket via uniform log-space scan
    float logMin = log(xmin + 1e-6);
    float logMax = log(xmax);

    float xPrev = exp(logMin);
    float fPrev = FFXV_d2(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    float xl = xPrev, fl = fPrev;
    float xr = xPrev, fr = fPrev;
    bool found = false;

    [loop]
    for (int i = 1; i <= scanSteps; i++) {
        float x = exp(lerp(logMin, logMax, (float)i / (float)scanSteps));
        float f = FFXV_d2(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        if ((fPrev <= 0 && f >= 0) || (fPrev >= 0 && f <= 0)) {
            xl = xPrev; fl = fPrev;
            xr = x;    fr = f;
            found = true;
            break;
        }
        xPrev = x; fPrev = f;
    }

    if (!found) return -1.0;

    // Phase 2: Newton + bisection hybrid — bracket [xl,xr] always straddles the y''=0 root
    float x = sqrt(xl * xr); // geometric midpoint start

    [loop]
    for (int it = 0; it < bisectIters; it++) {
        float f = FFXV_d2(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
        float fprime = FFXV_d3(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        // Narrow the bracket before attempting Newton, maintaining sign-change invariant
        if ((fl <= 0 && f >= 0) || (fl >= 0 && f <= 0)) {
            xr = x; fr = f;
        } else {
            xl = x; fl = f;
        }

        // Newton step if it lands inside the current bracket; else geometric midpoint
        float x_newton = x - f / (fprime + 1e-9);
        x = (x_newton > xl && x_newton < xr) ? x_newton : sqrt(xl * xr);

        if (abs(xr - xl) < 1e-5 * xl) break;
    }

    float inflection = x;
    if (rootOrder != 3) return inflection;

    // Phase 3: search for the y'''=0 (max curvature) root on [inflection, xmax]
    logMin = log(inflection + 1e-6);
    xPrev = inflection;
    fPrev = FFXV_d3(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    xl = xPrev; fl = fPrev;
    xr = xPrev; fr = fPrev;
    found = false;

    [loop]
    for (int i = 1; i <= scanSteps; i++) {
        float xScan = exp(lerp(logMin, logMax, (float)i / (float)scanSteps));
        float f = FFXV_d3(xScan, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        if ((fPrev <= 0 && f >= 0) || (fPrev >= 0 && f <= 0)) {
            xl = xPrev; fl = fPrev;
            xr = xScan; fr = f;
            found = true;
            break;
        }
        xPrev = xScan; fPrev = f;
    }

    if (!found) return inflection;

    // Phase 4: Newton + bisection hybrid on y''' (4th derivative via central differences)
    x = sqrt(xl * xr);
    [loop]
    for (int it = 0; it < bisectIters; it++) {
        float f = FFXV_d3(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
        // y'''' via central differences of y'''
        float eps = max(x * 1e-4, 1e-8);
        float fprime = (FFXV_d3(x + eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)
                       - FFXV_d3(x - eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)) / (2.0 * eps);

        if ((fl <= 0 && f >= 0) || (fl >= 0 && f <= 0)) {
            xr = x; fr = f;
        } else {
            xl = x; fl = f;
        }

        float x_newton = x - f / (fprime + 1e-9);
        x = (x_newton > xl && x_newton < xr) ? x_newton : sqrt(xl * xr);

        if (abs(xr - xl) < 1e-5 * xl) break;
    }

    return x;
}

// --- 6. The Final Extended Tonemapper ---
float FFXV_Extended(float x, float base, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49, float inflection)
{
    float pivot_x = inflection;
    float pivot_y = FFXV(pivot_x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49);
    float slope   = FFXV_d1(pivot_x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    float extended = slope * (x - pivot_x) + pivot_y;

    return (x > pivot_x) ? extended : base;
}

// Float3 variant
float3 FFXV_Extended(float3 color, float3 base, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49, float inflection)
{
    return float3(
        FFXV_Extended(color.r, base.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection),
        FFXV_Extended(color.g, base.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection),
        FFXV_Extended(color.b, base.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection)
    );
}

float3 ApplyTonemapAndGrading(float3 color)
{
    // color = BT2020_To_BT709(color);
    float3 tonemapped_color = renodx::tonemap::neutwo::PerChannel(color, PEAK_NITS/GAME_NITS);
 #if UI_DRAW_TYPE == 2
   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_color, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
   tonemapped_color *= GAME_NITS / UI_NITS;
   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_color, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif
    return (tonemapped_color);
}
