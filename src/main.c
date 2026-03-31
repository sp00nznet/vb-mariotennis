/*
 * Mario's Tennis - Static recompilation
 * SPDX-License-Identifier: MIT
 */

#include <vbrecomp/vbrecomp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../generated/recomp_funcs.h"

static uint32_t framebuffer[384 * 224];
static int frame_count = 0;

/* Called once per frame from within the game loop */
static bool on_frame(void) {
    frame_count++;

    /* Poll SDL events and update gamepad */
    if (!vb_platform_poll()) return false;
    vb_gamepad_set_buttons(vb_platform_get_buttons());

    /* Render VIP to framebuffer and present */
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);

    /* Log state changes and progress */
    static uint16_t last_state = 0xFFFF;
    uint16_t cur_state = vb_mem_read16(0x0500000C) & 0xF;
    if (cur_state != last_state) {
        printf("*** STATE CHANGE: %d -> %d (frame %d)\n", last_state, cur_state, frame_count);
        last_state = cur_state;
    }
    if (frame_count <= 20 || frame_count % 300 == 1) {
        /* Dump first few world attributes */
        printf("  World attrs: ");
        for (int w = 0; w < 4; w++) {
            uint32_t wa = 0x0003D800 + w * 32;
            uint16_t head = vb_mem_read16(wa);
            uint16_t gx = vb_mem_read16(wa + 2);
            uint16_t gp = vb_mem_read16(wa + 4);
            uint16_t gy = vb_mem_read16(wa + 6);
            printf("[%d: h=%04X gx=%d gy=%d gp=%04X] ", w, head, (int16_t)gx, (int16_t)gy, gp);
        }
        printf("\n");
    }
    if (frame_count % 120 == 1) {
        uint8_t counter_2285 = vb_mem_read8(0x05002285);
        printf("  counter@2285=%d\n", counter_2285);
    }
    if (frame_count % 60 == 1) {
        uint16_t state = vb_mem_read16(0x0500000C);
        /* Check if anything was written to VRAM */
        int vram_nonzero = 0;
        int chr_nonzero = 0, bgmap_nonzero = 0, world_nonzero = 0, oam_nonzero = 0, fb_nonzero = 0;
        for (int i = 0; i < 0x40000; i += 4) {
            uint32_t v = vb_mem_read32(0x00000000 + i);
            if (v == 0) continue;
            vram_nonzero++;
            if (i >= 0x06000 && i < 0x08000) chr_nonzero++;
            else if (i >= 0x0E000 && i < 0x10000) chr_nonzero++;
            else if (i >= 0x20000 && i < 0x3D800) bgmap_nonzero++;
            else if (i >= 0x3D800 && i < 0x3E000) world_nonzero++;
            else if (i >= 0x3E000 && i < 0x40000) oam_nonzero++;
            else if (i < 0x06000 || (i >= 0x08000 && i < 0x0E000) ||
                     (i >= 0x10000 && i < 0x16000) || (i >= 0x18000 && i < 0x1E000))
                fb_nonzero++;
        }
        /* Check VIP registers */
        uint16_t dpctrl = vb_mem_read16(0x0005F812);
        uint16_t dpstts = vb_mem_read16(0x0005F810);
        uint16_t intpnd = vb_mem_read16(0x0005F800);
        uint16_t intenb = vb_mem_read16(0x0005F802);
        printf("Frame %d | State: %d | VRAM:%d (CHR:%d BG:%d WLD:%d OAM:%d FB:%d) | DPCTRL:%04X XPCTRL:%04X\n",
               frame_count, cur_state, vram_nonzero, chr_nonzero, bgmap_nonzero,
               world_nonzero, oam_nonzero, fb_nonzero, dpctrl,
               vb_mem_read16(0x0005F822));
    }

    return true;
}

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

    /* Set up initial CPU state */
    vb_cpu.r[3] = 0x05003FFC;  /* SP: top of WRAM */
    vb_cpu.r[4] = 0x05008000;  /* GP */
    vb_cpu.r[11] = 0x05000000;

    /* Clear PSW: enable interrupts, clear NP/EP flags.
     * The real VB boot code does this but our recompiled entry
     * point skips that part. */
    vb_cpu.sr[VB_SREG_PSW] = 0;

    /* Register interrupt handlers */
    vb_interrupt_set_handler(VB_INT_KEY,   vb_func_07F80000);
    vb_interrupt_set_handler(VB_INT_TIMER, vb_func_07F80024);
    vb_interrupt_set_handler(VB_INT_VIP,   vb_func_07F800AA);

    /* Set fast frame ticks so VIP status updates happen quickly during init */
    vb_interrupt_set_frame_ticks(1000);

    /* Pre-set VIP status so state handlers don't poll forever */
    vb_vip_frame_advance();

    /* Set frame sync flag that the VIP interrupt handler would normally set.
     * WRAM[0x05000018] and [0x0500001A] are frame counters/flags. */
    vb_mem_write16(0x05000018, 1);
    vb_mem_write16(0x0500001A, 1);
    vb_mem_write16(0x0500001C, 1);

    /* Set up frame timing and callback */
    vb_interrupt_set_frame_ticks(20000);
    vb_interrupt_set_frame_callback(on_frame);

    /* Initialize SDL2 window */
    if (!vb_platform_init("Mario's Tennis - vbrecomp", 2)) {
        fprintf(stderr, "Failed to init SDL2 platform\n");
        free(rom);
        return 1;
    }

    printf("Starting game...\n");
    fflush(stdout);

    /* Run entry point: sets up WRAM, clears VRAM, calls init */
    vb_func_07F80120();

    printf("Init complete (state=%d). Running game loop...\n",
           vb_mem_read16(0x0500000C) & 0xF);
    fflush(stdout);

    /* Run state handlers sequentially to load game data */
    printf("Running state 0 (init)...\n"); fflush(stdout);
    vb_func_07F81940();

    printf("Running state 1 (logo setup)...\n"); fflush(stdout);
    vb_func_07F8A912();

    /* Force state to 2 and run tile data loader */
    printf("Running state 2 (tile data load)...\n"); fflush(stdout);
    vb_mem_write16(0x0500000C, 2);
    vb_func_07F8EF3A();

    printf("Running state 3...\n"); fflush(stdout);
    vb_mem_write16(0x0500000C, 3);
    vb_func_07F909DC();

    printf("Running state 4...\n"); fflush(stdout);
    vb_mem_write16(0x0500000C, 4);
    vb_func_07F81B98();

    /* Check VRAM state after running handlers */
    {
        int chr = 0, bg = 0, wld = 0, oam = 0;
        for (int i = 0; i < 0x40000; i += 4) {
            uint32_t v = vb_mem_read32(0x00000000 + i);
            if (v == 0) continue;
            if (i >= 0x06000 && i < 0x10000) chr++;
            else if (i >= 0x20000 && i < 0x3D800) bg++;
            else if (i >= 0x3D800 && i < 0x3E000) wld++;
            else if (i >= 0x3E000) oam++;
        }
        printf("Final VRAM: CHR=%d BG=%d WLD=%d OAM=%d\n", chr, bg, wld, oam);
    }

    /* Render and display */
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);
    printf("Window open. Close to exit.\n");
    fflush(stdout);
    while (vb_platform_poll()) {
        vb_vip_render(framebuffer, 0);
        vb_platform_present(framebuffer);
    }

    printf("Game exited after %d frames\n", frame_count);

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
