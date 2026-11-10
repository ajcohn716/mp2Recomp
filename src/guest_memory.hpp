#pragma once
#include "recomp.h"
// Host-side patches use 32-bit guest pointers. Generated code uses sign-extended
// 64-bit register values; normalize host pointers before the common macros.
#undef MEM_W
#undef MEM_H
#undef MEM_HU
#undef MEM_B
#undef MEM_BU
#define MP2_PHYS(offset, address) (static_cast<uint32_t>((address) + (offset)) & 0x1FFFFFFFu)
#define MEM_W(offset, address) (*reinterpret_cast<int32_t*>(rdram + MP2_PHYS(offset, address)))
#define MEM_H(offset, address) (*reinterpret_cast<int16_t*>(rdram + (MP2_PHYS(offset, address) ^ 2)))
#define MEM_HU(offset, address) (*reinterpret_cast<uint16_t*>(rdram + (MP2_PHYS(offset, address) ^ 2)))
#define MEM_B(offset, address) (*reinterpret_cast<int8_t*>(rdram + (MP2_PHYS(offset, address) ^ 3)))
#define MEM_BU(offset, address) (*reinterpret_cast<uint8_t*>(rdram + (MP2_PHYS(offset, address) ^ 3)))
