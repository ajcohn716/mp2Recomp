#include <cstdio>
#include <cstdlib>
#include "recomp.h"
#include "ultramodern/ultra64.h"

void mp2_overlay_dma(uint32_t, int32_t, uint32_t);
extern "C" void mp2_original_osEPiStartDma_recomp(uint8_t*, recomp_context*);
extern "C" void osEPiStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    auto* mb = TO_PTR(OSIoMesg, ctx->r5);
    if (ctx->r6 == 0) mp2_overlay_dma(mb->devAddr, mb->dramAddr, mb->size);
    mp2_original_osEPiStartDma_recomp(rdram, ctx);
}

// Unused 64DD SDK paths share translation units with the normal game. Keep
// explicit traps for unsupported hardware rather than silently accepting it.
#define UNSUPPORTED(name) extern "C" void name(uint8_t*, recomp_context*) { \
    fprintf(stderr, "[MP2] unsupported SDK path: " #name "\n"); std::abort(); }
UNSUPPORTED(osEPiWriteIo_recomp)
UNSUPPORTED(__osSetSR_recomp)
UNSUPPORTED(__osSetCause_recomp)
UNSUPPORTED(__osSetCount_recomp)
UNSUPPORTED(__osSetCompare_recomp)
UNSUPPORTED(osLeoDiskInit_recomp)
