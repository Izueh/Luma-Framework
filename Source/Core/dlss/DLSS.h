#pragma once

#include "../includes/super_resolution.h"

#if defined(_WIN64) && __has_include("nvsdk_ngx.h")
#ifndef ENABLE_NGX
#define ENABLE_NGX 1
#endif // ENABLE_NGX
#else
#undef ENABLE_NGX
#define ENABLE_NGX 0
#endif

#if ENABLE_NGX

namespace NGX
{
	// DLSS SR
	class DLSS : public SR::SuperResolutionImpl
	{
	public:
		virtual bool HasInit(const SR::InstanceData* data) const override;
		virtual bool IsSupported(const SR::InstanceData* data) const override;

		virtual bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr) override;
		virtual void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device = nullptr) override;

		virtual bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override;

		virtual bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override;
		
		virtual int GetJitterPhases(const SR::InstanceData* data) const;
	};
}

#endif