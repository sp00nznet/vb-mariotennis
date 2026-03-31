/*
 * Mario's Tennis - Static recompilation
 * SPDX-License-Identifier: MIT
 */

#include <vbrecomp/vbrecomp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../generated/recomp_funcs.h"

/* Read file into malloc'd buffer */
static uint8_t *load_rom(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Error: failed to read ROM\n");
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)size;
    return buf;
}

int main(int argc, char **argv) {
    const char *rom_path = argc > 1 ? argv[1] : "Mario's Tennis (Japan, USA).vb";

    /* Load ROM for data access */
    uint32_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) return 1;

    printf("Mario's Tennis - vbrecomp\n");
    printf("ROM: %u bytes\n", rom_size);

    /* Initialize all VB subsystems */
    vb_init(rom, rom_size);

    /* Set up initial register state (matches what the reset vector does) */
    vb_cpu.r[3] = 0x05003FFC;  /* SP: top of WRAM */
    vb_cpu.r[4] = 0x05008000;  /* GP */
    vb_cpu.r[11] = 0x05000000;

    /* Register interrupt handlers */
    vb_interrupt_set_handler(VB_INT_KEY,   vb_func_07F80000);
    vb_interrupt_set_handler(VB_INT_TIMER, vb_func_07F80024);
    vb_interrupt_set_handler(VB_INT_VIP,   vb_func_07F800AA);

    /* Initialize platform (SDL2 window) */
    if (!vb_platform_init("Mario's Tennis - vbrecomp", 2)) {
        fprintf(stderr, "Warning: could not init platform, running headless\n");
    }

    printf("Starting execution at entry point...\n");

    /* Call the entry point (the recompiled reset handler).
     * This sets up WRAM and then calls the game's init/main loop. */
    vb_func_07F80120();

    printf("Entry point returned.\n");
    printf("Registers after return:\n");
    printf("  r3(sp)=%08X r4(gp)=%08X r31(lp)=%08X\n",
           vb_cpu.r[3], vb_cpu.r[4], vb_cpu.r[31]);
    printf("  PSW=%08X PC=%08X\n",
           vb_cpu.sr[5], vb_cpu.pc);

    /* Even though the recompiled code returned early (missing functions),
     * let's render whatever is in VRAM and display it */
    uint32_t framebuffer[384 * 224];
    vb_vip_render(framebuffer, 0); /* Left eye */

    /* Show the frame for a few seconds so we can see if anything is there */
    if (vb_platform_poll()) {
        vb_platform_present(framebuffer);

        /* Wait for user to close window */
        printf("Window open. Close it or press Ctrl+C to exit.\n");
        bool running = true;
        while (running) {
            running = vb_platform_poll();

            /* Re-render each frame in case VIP state changed */
            vb_vip_render(framebuffer, 0);
            vb_platform_present(framebuffer);
        }
    }

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
