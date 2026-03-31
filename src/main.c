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
    if (frame_count % 60 == 1) {
        uint16_t state = vb_mem_read16(0x0500000C);
        /* Check if anything was written to VRAM */
        int vram_nonzero = 0;
        for (int i = 0; i < 0x40000; i += 4) {
            if (vb_mem_read32(0x00000000 + i) != 0) vram_nonzero++;
        }
        /* Check VIP registers */
        uint16_t dpctrl = vb_mem_read16(0x0005F812);
        uint16_t dpstts = vb_mem_read16(0x0005F810);
        uint16_t intpnd = vb_mem_read16(0x0005F800);
        uint16_t intenb = vb_mem_read16(0x0005F802);
        printf("Frame %d | State: %d | VRAM: %d nonzero | DPCTRL:%04X DPSTTS:%04X INTPND:%04X INTENB:%04X\n",
               frame_count, state & 0xF, vram_nonzero, dpctrl, dpstts, intpnd, intenb);
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

    /* Run the entry point. It contains the game's main loop but may
     * return early when hitting stub functions. We restart it to keep going. */
    while (1) {
        vb_func_07F80120();

        /* Entry point returned — game hit a stub or unresolved code path.
         * Log it and keep running via the frame callback. */
        uint16_t state = vb_mem_read16(0x0500000C) & 0xF;
        printf("Entry point returned at frame %d (state=%d). Restarting loop...\n",
               frame_count, state);
        fflush(stdout);

        /* Run idle loop: just fire frame callbacks until state changes or user quits */
        uint16_t prev_state = state;
        int idle_frames = 0;
        while (idle_frames < 300) {  /* Max 5 seconds of idle */
            if (!on_frame()) goto done;
            frame_count++;
            vb_vip_frame_advance();

            state = vb_mem_read16(0x0500000C) & 0xF;
            if (state != prev_state) break;
            idle_frames++;
        }

        /* Try calling the state handler directly */
        if (state <= 14) {
            /* Reset stack pointer to avoid stack overflow from repeated entries */
            vb_cpu.r[3] = 0x05003FFC;
        }
    }
done:
    printf("Game exited after %d frames\n", frame_count);

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
