#include "..\..\Core\core.hpp"
#include "hooks.hpp"

void LogXMMatrix(const char* label, const DirectX::XMMATRIX& matrix, 
                 Luma::Utils::Log::Level level = Luma::Utils::Log::Level::Verbose)
{
#if DEVELOPMENT     
    std::stringstream s;
    s << label << ":\n";
    
    for (int row = 0; row < 4; ++row)
    {
        s << "  [";
        for (int col = 0; col < 4; ++col)
        {
            // Format: fixed-point, 4 decimals, width 8 for alignment
            s << std::fixed << std::setprecision(8) << std::setw(12) 
              << matrix.r[row].m128_f32[col];
            if (col < 3) s << ", ";
        }
        s << "]";
        if (row < 3) s << "\n";
    }
    
    Luma::Utils::Log::Write(level, s.str().c_str());
#endif
}

thread_local bool enable_jitter = true;

char __fastcall Hooked_CameraUpdateFieldMap3DHUD(__int64 a1)
{
    enable_jitter = false;
    
    auto original_result = g_camera_fieldmap_view_proj_matrix_hook
    .unsafe_call<char>(a1);
    
    enable_jitter = true;
    
    return original_result;
}

__int64 __fastcall Hooked_UpdateActiveCameraAddress(__int64 a1, __int64 a2)
{
    auto original_result = g_active_camera_hook
    .unsafe_call<__int64>(a1, a2);
    
    CameraData = reinterpret_cast<CCamera*>(a2);
    
    //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, std::format("Camera Active: 0x{:X}", a2).c_str());

    return original_result;
}

__int64 __fastcall Hooked_CameraUpdateFunctionSceneAnd3DHUD(void* a1, __int64 a2, int32_t* a3)
{
    CombineViewProjectionMatrices((__int64)a1 + 8, 1, 1);
    
    auto view = reinterpret_cast<DirectX::XMMATRIX*>((uintptr_t)a1 + 128);
    auto proj = reinterpret_cast<DirectX::XMMATRIX*>((uintptr_t)a1 + 192);
    DirectX::XMMATRIX cleanProj = *proj;
    cleanProj.r[2].m128_f32[0] = 0.0;
    cleanProj.r[2].m128_f32[1] = 0.0;
    
    DirectX::XMMATRIX stableVP = DirectX::XMMatrixMultiply(*view, cleanProj);
    DirectX::XMVECTOR world = DirectX::XMVectorSet(*reinterpret_cast<float*>(&a3[0]), *reinterpret_cast<float*>(&a3[1]), *reinterpret_cast<float*>(&a3[2]), 1.0f);
    DirectX::XMVECTOR clip = DirectX::XMVector4Transform(world, stableVP);
    DirectX::XMFLOAT4 clipFloats;
    DirectX::XMStoreFloat4(&clipFloats, clip);
    
    auto out = reinterpret_cast<ScreenProjection*>(a2);
    if (clipFloats.w <= 0.0001f)
    {
        out->visible = 0;
        return a2;
    }
    
    const float invW = 1.0f / clipFloats.w;
    const float ndcX = clipFloats.x * invW;
    const float ndcY = clipFloats.y * invW;
    const float ndcZ = clipFloats.z * invW;
    
    out->x = (ndcX + 1.0f) * 640.0f;
    out->y = (1.0f - ndcY) * 360.0f;
    out->depth = -ndcZ;
    out->visible = 1;
    
    return a2;
}

__int64 __fastcall Hooked_ComputeProjectionMatrix(__int64 a1, float a2, float a3, float a4, float a5, char a6, float a7)
{
    auto original_result = g_compute_projection_matrix_hook
    .unsafe_call<__int64>(a1, a2, a3, a4, a5, a6, a7);
    
    if (CameraData == nullptr)
        return original_result;
    
    //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, std::format("Near: {:6f}, Far: {:6f}", CameraData->camera_distances.x, CameraData->camera_distances.y).c_str());
    
    //a2 - vert fov
    //a3 - aspect ratio
    //a4 - near plane
    //a5 - far plane
    if ( a3 != 1.0 && a4 == CameraData->camera_distances.x && a5 == CameraData->camera_distances.y && enable_jitter)
    {
        int width  = *(int*)(RenderResolution + 0x00);
        int height = *(int*)(RenderResolution + 0x04);
        
        // the matrix is transposed
        *(float*)(a1 + 0x20) = projection_jitters.x * 2.0 / static_cast<float>(width);
        *(float*)(a1 + 0x24) = projection_jitters.y * -2.0 / static_cast<float>(height);
#if 0
        using namespace DirectX;
	   
        XMMATRIX projection_matrix = XMLoadFloat4x4(
           reinterpret_cast<const XMFLOAT4X4*>(a1));
           
        LogXMMatrix("projection_matrix", projection_matrix);

        std::stringstream s;
        s << "Computing projection matrix: ";
        s << std::format("Width: {}, Height: {}", *(int*)(RenderResolution + 0x00), *(int*)(RenderResolution + 0x04));
        s << std::format(", Near: {}, Far: {}, Aspect: {}, Jitter: {}, {}, FrameIdx: {}.", a4, a5, a3, projection_jitters.x, projection_jitters.y, cb_luma_global_settings.FrameIndex);
        Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, s.str().c_str());
        
        Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, std::format("Camera Address: 0x{:X}", (a1-184)).c_str());
#endif
    }
    
    return original_result;
}

void **__fastcall PostEffectSearchModeRendererPrepare(__int64 a1, float a2)
{
    auto original_result = g_search_mode_hook
    .unsafe_call<void**>(a1, a2);
    
    //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Search Mode: On");
    is_search_mode_on = true;
    
    return original_result;
}

char __fastcall Hooked_BattleMainLoopIterate(__int64 a1)
{
    auto original_result = g_battle_mode_hook
    .unsafe_call<char>(a1);
    
    //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Battle Mode: On");
    
    is_in_battle_mode = true;

    return original_result;
}


__int64 __fastcall PostEffectHatchingRendererPrepare(__int64 a1, float a2)
{
    auto original_result = g_postfx_hatching_hook
    .unsafe_call<__int64>(a1, a2);
        
    //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Hatching: On");

    is_hatching_on = true;
        
    return original_result;
}
