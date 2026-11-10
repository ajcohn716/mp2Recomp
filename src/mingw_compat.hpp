#pragma once
#include <objbase.h>
#include <cmath>
// MinGW emulates __uuidof with template specializations. DirectX-Headers and
// DXC declare UUID attributes instead; use Clang's intrinsic for those types.
#ifdef __uuidof
#undef __uuidof
#endif
