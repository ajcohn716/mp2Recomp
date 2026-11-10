// Authored no-ROM link fixtures. Never used by the actual game build.
#include "recomp.h"
#include "librecomp/rsp.hpp"
extern "C" {
void HuPrcEnd(uint8_t*,recomp_context*){std::abort();}
void HuUnlinkPrc(uint8_t*,recomp_context*){std::abort();}
void HuMemMemoryFreePerm(uint8_t*,recomp_context*){std::abort();}
void func_80000460_1060(uint8_t*,recomp_context*){std::abort();}
}
RspExitReason aspMain(uint8_t*,uint32_t){std::abort();}
