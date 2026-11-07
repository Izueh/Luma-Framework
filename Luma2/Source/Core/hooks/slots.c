#define CINTERFACE
#include <stddef.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "hooks/slots.h"

#define LUMA_DEFINE_SLOT(Interface, Method) const uint32_t LumaSlot_##Interface##_##Method = (uint32_t)(offsetof(Interface##Vtbl, Method) / sizeof(void*));
LUMA_SLOTS(LUMA_DEFINE_SLOT)
