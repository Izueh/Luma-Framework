#pragma once

#include <cstdint>

struct GBFRResolvedAddresses;

bool ResolveGBFRAddresses();
uintptr_t ResolveGBFRDataOrFallback(uintptr_t resolved_absolute, uintptr_t fallback_rva);
void* ResolveGBFRCodeOrFallback(void* resolved_absolute, uintptr_t fallback_rva);
