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
// Phase 1: log-space scan seeds a guaranteed sign-change bracket.
// Phase 2: Newton step with bisection fallback refines inside the bracket.
// Returns -1 if no sign change is found in [xmin, xmax].
float Find_Inflection(float xmin, float xmax, int scanSteps, int bisectIters,
                    float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46,
                    int rootOrder)
{
    // Phase 1: find sign-change bracket via uniform log-space scan
    float logMin = log(xmin + 1e-6);
    float logMax = log(xmax);

    float xPrev = exp(logMin);
    float fPrev = (rootOrder == 3)
                  ? FFXV_d3(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)
                  : FFXV_d2(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    float xl = xPrev, fl = fPrev;
    float xr = xPrev, fr = fPrev;
    bool found = false;

    [loop]
    for (int i = 1; i <= scanSteps; i++) {
        float x = exp(lerp(logMin, logMax, (float)i / (float)scanSteps));
        float f = (rootOrder == 3)
                  ? FFXV_d3(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)
                  : FFXV_d2(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        if ((fPrev <= 0 && f >= 0) || (fPrev >= 0 && f <= 0)) {
            xl = xPrev; fl = fPrev;
            xr = x;    fr = f;
            found = true;
            break;
        }
        xPrev = x; fPrev = f;
    }

    if (!found) return -1.0;

    // Phase 2: Newton + bisection hybrid — bracket [xl,xr] always straddles the root
    float x = sqrt(xl * xr); // geometric midpoint start

    [loop]
    for (int it = 0; it < bisectIters; it++) {
        float f, fprime;
        if (rootOrder == 3) {
            f = FFXV_d3(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
            // y'''' via central differences of y'''
            float eps = max(x * 1e-4, 1e-8);
            fprime = (FFXV_d3(x + eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)
                    - FFXV_d3(x - eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)) / (2.0 * eps);
        } else {
            f = FFXV_d2(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
            fprime = FFXV_d3(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
        }

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
    float3 tonemapped_color = renodx::tonemap::neutwo::PerChannel(color, PEAK_NITS/GAME_NITS);
 #if UI_DRAW_TYPE == 2
   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_color, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
   tonemapped_color *= GAME_NITS / UI_NITS;
   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_color, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif
    return tonemapped_color;
}
