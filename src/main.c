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
static bool on_frame(void); /* forward declaration */

/* Trace wrapper for tile decompression function */
extern void vb_func_07F8135C_real(void);
void vb_func_07F8135C(void) {
    static int call_count = 0;
    call_count++;
    if (call_count <= 10) {
        fprintf(stderr, "TILE COPY called #%d: dst=0x%08X src=0x%08X size=0x%X\n",
                call_count, vb_cpu.r[7], vb_cpu.r[6], vb_cpu.r[8]);
    }
    vb_func_07F8135C_real();
}

/*
 * HLE replacement for vb_func_07F8016C — the frame sync / VBlank wait function.
 *
 * The original code:
 *   1. Clears WRAM[0x0500000E] to 0
 *   2. Polls until it becomes nonzero (set by VIP interrupt handler)
 *   3. Returns
 *
 * Our replacement: advance one frame, run the VIP interrupt handler
 * directly, then return. This avoids the polling loop entirely.
 */
void vb_func_07F8016C(void) {
    /* Clear the frame flag (like the original) */
    vb_mem_write16(0x0500000E, 0);

    /* Advance VIP state and fire the interrupt handler directly */
    vb_vip_frame_advance();

    /* Re-enable VIP interrupts (the handler clears INTENB) */
    vb_mem_write16(0x0005F802, 0x4018);

    /* Call the VIP interrupt handler directly to set frame flags */
    vb_func_07F800AA();

    /* Restore PSW after interrupt handler (it does RETI internally) */

    /* Call frame callback for rendering/input */
    frame_count++;
    on_frame();
}

/*
 * HLE replacement for vb_func_07F80186 — XPSTTS wait function.
 *
 * The original polls XPSTTS bits 2-5 until nonzero.
 * We just return immediately since drawing is instantaneous.
 */
void vb_func_07F80186(void) {
    /* Nothing to wait for — drawing is instant in recomp */
}

/* Called once per frame from within the game loop */
static bool on_frame(void) {
    frame_count++;

    /* Poll SDL events and update gamepad */
    if (!vb_platform_poll()) return false;
    vb_gamepad_set_buttons(vb_platform_get_buttons());

    /* Render VIP to framebuffer and present */
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);

    /* Dump VRAM once at frame 100 */
    if (frame_count == 100) {
        FILE *vf = fopen("vram_dump.txt", "w");
        if (vf) {
            fprintf(vf, "VRAM dump at frame %d (state=%d):\n", frame_count,
                    vb_mem_read16(0x0500000C) & 0xF);
            /* Coarse: 4KB blocks */
            for (int region = 0; region < 64; region++) {
                int base = region * 0x1000;
                int nonzero = 0;
                for (int i = 0; i < 0x1000; i += 2) {
                    if (vb_mem_read16(base + i) != 0) nonzero++;
                }
                if (nonzero > 0) {
                    fprintf(vf, "  0x%05X: %4d nonzero\n", base, nonzero);
                }
            }
            /* World attributes detail */
            fprintf(vf, "\nWorld attributes (0x3D800):\n");
            for (int w = 0; w < 32; w++) {
                uint32_t wa = 0x3D800 + w * 32;
                uint16_t head = vb_mem_read16(wa);
                if (head != 0) {
                    int16_t gx = (int16_t)vb_mem_read16(wa + 2);
                    int16_t gy = (int16_t)vb_mem_read16(wa + 6);
                    uint16_t ww = vb_mem_read16(wa + 14);
                    uint16_t wh = vb_mem_read16(wa + 16);
                    uint16_t param = vb_mem_read16(wa + 28);
                    fprintf(vf, "  World %d: HEAD=%04X GX=%d GY=%d W=%d H=%d PARAM=%04X\n",
                            w, head, gx, gy, ww, wh, param);
                }
            }
            /* Palettes (read via bus) */
            fprintf(vf, "\nPalettes: GPLT0=%04X GPLT1=%04X GPLT2=%04X GPLT3=%04X\n",
                    vb_mem_read16(0x0005F830), vb_mem_read16(0x0005F832),
                    vb_mem_read16(0x0005F834), vb_mem_read16(0x0005F836));
            fprintf(vf, "BKCOL=%04X DPCTRL=%04X BRTA=%04X BRTB=%04X BRTC=%04X\n",
                    vb_mem_read16(0x0005F840), vb_mem_read16(0x0005F812),
                    vb_mem_read16(0x0005F814), vb_mem_read16(0x0005F816),
                    vb_mem_read16(0x0005F818));
            fclose(vf);
        }
        /* Write raw VRAM binary */
        FILE *bf = fopen("vram_raw.bin", "wb");
        if (bf) {
            for (int i = 0; i < 0x40000; i++) {
                uint8_t b = vb_mem_read8(i);
                fwrite(&b, 1, 1, bf);
            }
            fclose(bf);
        }
    }

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
            if ((i >= 0x06000 && i < 0x08000) ||
                (i >= 0x0E000 && i < 0x10000) ||
                (i >= 0x16000 && i < 0x18000) ||
                (i >= 0x1E000 && i < 0x20000)) chr_nonzero++;
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

    /* Disable frame ticks during init to avoid interrupts corrupting setup */
    vb_interrupt_set_frame_ticks(0);

    /* Don't pre-fire VIP frame advance — let it happen naturally
     * via frame ticks after init completes */

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

    /* Run entry point: sets up WRAM, clears VRAM, calls init.
     * The entry point never returns (main game loop), so when it does,
     * either it hit a stub or completed init and the state machine ran. */
    vb_func_07F80120();
    printf("Entry point returned! SP=0x%08X r31=0x%08X state=%d\n",
           vb_cpu.r[3], vb_cpu.r[31], vb_mem_read16(0x0500000C) & 0xF);

    printf("Init complete (state=%d). Enabling interrupts...\n",
           vb_mem_read16(0x0500000C) & 0xF);
    fflush(stdout);

    /* NOW enable frame ticks — init is done, interrupts are safe.
     * Use a large value to avoid overwhelming the game with interrupts.
     * VB runs ~20MHz / 50fps = ~400K cycles per frame. Each interrupt check
     * is roughly 1-5 instructions = 200K-80K checks per frame. */
    vb_interrupt_set_frame_ticks(100000);

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

    /* Dump VRAM to file for analysis */
    {
        FILE *vf = fopen("vram_dump.txt", "w");
        if (vf) {
            fprintf(vf, "VRAM dump by 4KB region:\n");
            for (int region = 0; region < 64; region++) {
                int base = region * 0x1000;
                int nonzero = 0;
                for (int i = 0; i < 0x1000; i += 2) {
                    if (vb_mem_read16(base + i) != 0) nonzero++;
                }
                if (nonzero > 0) {
                    fprintf(vf, "  0x%05X-0x%05X: %d nonzero halfwords\n", base, base + 0xFFF, nonzero);
                }
            }
            /* Also dump raw VRAM as binary for inspection */
            fclose(vf);

            FILE *bf = fopen("vram_raw.bin", "wb");
            if (bf) {
                for (int i = 0; i < 0x40000; i++) {
                    uint8_t b = vb_mem_read8(i);
                    fwrite(&b, 1, 1, bf);
                }
                fclose(bf);
            }
            fprintf(stderr, "VRAM dumped to vram_dump.txt and vram_raw.bin\n");
        }
    }

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
