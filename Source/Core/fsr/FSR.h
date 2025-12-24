#pragma once

#include "../includes/super_resolution.h"

#if defined(_WIN64) && __has_include("FidelityFX/host/ffx_fsr3.h")
#ifndef ENABLE_FIDELITY_SK
#define ENABLE_FIDELITY_SK 1
#endif // ENABLE_FIDELITY_SK
#elifdef ENABLE_FIDELITY_SK
#undef ENABLE_FIDELITY_SK
#define ENABLE_FIDELITY_SK 0
#endif

#if ENABLE_FIDELITY_SK

namespace FidelityFX
{
	// FSR 3 SR
	class FSR : public SR::SuperResolutionImpl
	{
	public:
		virtual bool HasInit(const SR::InstanceData* data) const override;
		virtual bool IsSupported(const SR::InstanceData* data) const override;

		virtual bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr) override;
		virtual void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device = nullptr) override;

		virtual bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override;

		virtual bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override;

		virtual int GetJitterPhases(const SR::InstanceData* data) const;
		
		virtual bool NeedsStateRestoration() const { return true; }
	};
}

#endif