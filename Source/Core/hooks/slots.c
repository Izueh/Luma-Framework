#define CINTERFACE
#include <stddef.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include "hooks/slots.h"

#define LUMA_DEFINE_SLOT(Interface, Method) const uint32_t LumaSlot_##Interface##_##Method = (uint32_t)(offsetof(Interface##Vtbl, Method) / sizeof(void*));
LUMA_SLOTS(LUMA_DEFINE_SLOT)

#define LUMA_DEFINE_METHOD_COUNT(Interface) const uint32_t LumaMethodCount_##Interface = (uint32_t)(sizeof(Interface##Vtbl) / sizeof(void*));
LUMA_VTABLE_INTERFACES(LUMA_DEFINE_METHOD_COUNT)
