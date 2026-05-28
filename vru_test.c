#include <libdragon.h>
#include <stdio.h>
#include <string.h>

#include "vru.h"

#define FONT_UI 10

static rdpq_font_t *g_font;

static void draw_ui(int port, int word_count, int rc_boot, int rc_last, int rc_result, uint16_t state,
                    const vru_identify_t *ident, const vru_result_t *result)
{
    surface_t *disp = display_get();
    graphics_fill_screen(disp, graphics_make_color(18, 22, 34, 255));

    rdpq_attach(disp, NULL);
    rdpq_text_printf(NULL, FONT_UI, 8, 10, "VRU Diagnostics");
    rdpq_text_printf(NULL, FONT_UI, 8, 26, "Port: %d  (C-Up/C-Down)", port + 1);
    rdpq_text_printf(NULL, FONT_UI, 8, 40, "Ready:%d Present:%d Ident:%04X Status:%02X", ident->ready ? 1 : 0,
                     ident->present ? 1 : 0, ident->identifier, ident->status);
    rdpq_text_printf(NULL, FONT_UI, 8, 54, "State:%04X  WordCount:%d (D-Up/D-Down)", state, word_count);

    rdpq_text_printf(NULL, FONT_UI, 8, 74, "A bootstrap   Z clear-dict");
    rdpq_text_printf(NULL, FONT_UI, 8, 88, "B start       L stop");
    rdpq_text_printf(NULL, FONT_UI, 8, 102, "R get result  Start poll 2s");

    rdpq_text_printf(NULL, FONT_UI, 8, 122, "rc_boot=%d rc_last=%d rc_result=%d", rc_boot, rc_last, rc_result);
    rdpq_text_printf(NULL, FONT_UI, 8, 142, "ans=%u warn=%04X mode=%04X", (unsigned)result->answer_num,
                     (unsigned)result->warning, (unsigned)result->mode_status);
    rdpq_text_printf(NULL, FONT_UI, 8, 156, "top answer=%u dist=%u", (unsigned)result->answer[0],
                     (unsigned)result->distance[0]);

    rdpq_text_printf(NULL, FONT_UI, 8, 212, "Tip: keep polling coarse to reduce bus churn.");
    rdpq_detach_show();
}

int main(void)
{
    debug_init_emulog();
    debug_init_usblog();
    joypad_init();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    rdpq_init();
    g_font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_UI, g_font);

    int port = VRU_DEFAULT_PORT;
    int word_count = 16;
    int rc_boot = 0;
    int rc_last = 0;
    int rc_result = 0;
    uint16_t state = 0;
    vru_identify_t ident = {0};
    vru_result_t result;
    memset(&result, 0, sizeof(result));

    int probe_frames = 0;
    while (1) {
        joypad_poll();
        joypad_buttons_t pr = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        if (pr.c_up)
            port = (port + 1) & 3;
        if (pr.c_down)
            port = (port + 3) & 3;
        if (pr.d_up && word_count < 255)
            word_count++;
        if (pr.d_down && word_count > 1)
            word_count--;

        vru_device_t dev;
        vru_device_init(&dev, port);

        if (pr.a) {
            rc_boot = vru_wait_ready(&dev, 1000, 20);
            if (rc_boot == VRU_OK)
                rc_boot = vru_bootstrap(&dev, true);
        }
        if (pr.z)
            rc_last = vru_clear_dictionary(&dev, (uint8_t)word_count);
        if (pr.b)
            rc_last = vru_start_listen(&dev);
        if (pr.l)
            rc_last = vru_stop_listen(&dev);
        if (pr.r)
            rc_result = vru_wait_for_result(&dev, &result, 2000, 20);
        if (pr.start) {
            rc_last = vru_start_listen(&dev);
            if (rc_last == VRU_OK)
                rc_result = vru_wait_for_result(&dev, &result, 2000, 20);
        }

        if (probe_frames <= 0) {
            if (!vru_identify(&dev, &ident)) {
                ident.present = false;
                ident.ready = false;
                ident.identifier = 0;
                ident.status = 0;
            }
            if (vru_read_state(&dev, &state) != 0)
                state = 0xFFFFu;
            probe_frames = 8;
        }
        probe_frames--;

        draw_ui(port, word_count, rc_boot, rc_last, rc_result, state, &ident, &result);
        wait_ms(16);
    }
}
