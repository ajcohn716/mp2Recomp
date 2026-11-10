#include "librecomp/overlays.hpp"
#include "recomp_overlays.inl"
#include "guest_memory.hpp"

extern "C" {
void HuPrcCall(uint8_t*, recomp_context*);
void HuPrcSleep(uint8_t*, recomp_context*);
void HuPrcChildWatch(uint8_t*, recomp_context*);
void HuPrcTerminate(uint8_t*, recomp_context*);
}

void register_mp2_overlays() {
    recomp::overlays::register_overlays({section_table, ARRLEN(section_table), num_sections}, {overlay_sections_by_index, ARRLEN(overlay_sections_by_index)});
}

void mp2_init(uint8_t* rdram, recomp_context* ctx) {
    recomp::overlays::add_loaded_function(0x8007DAA0, HuPrcCall);
    recomp::overlays::add_loaded_function(0x8007D9E0, HuPrcSleep);
    recomp::overlays::add_loaded_function(0x8007D7E8, HuPrcChildWatch);
    recomp::overlays::add_loaded_function(0x8007D93C, HuPrcTerminate);
    fprintf(stderr, "[MP2] ROM loaded; native entrypoint ready\n");
}

void mp2_entry(uint8_t* rdram, recomp_context* ctx) {
    // IPL3 loads the main image. The original entrypoint clears this BSS and
    // jumps indirectly to main after establishing its stack.
    for (uint32_t off = 0; off < 0x2DC10; off += 4) MEM_W(off, 0x800D4BF0) = 0;
    ctx->r29 = (int32_t)0x800FC730;
    func_80000460_1060(rdram, ctx);
}

void mp2_overlay_dma(uint32_t rom, int32_t ram, uint32_t size) {
    if (uint32_t(ram) != 0x80102800) return;
    for (auto& section : section_table) {
        if (section.rom_addr == rom && section.ram_addr == uint32_t(ram)) {
            fprintf(stderr, "[MP2] overlay ROM %08x -> RAM %08x (section %zu)\n", rom, uint32_t(ram), section.index);
            unload_overlays(ram, 0x25800);
            load_overlays(rom, ram, section.size);
            return;
        }
    }
}
