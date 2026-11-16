#include <assert.h>
#include <string.h>

#include "../src/messages.c"

uint32_t test_bg_chars[8192];
uint16_t test_bg_map[1024];
uint16_t test_bg_palette[256];
uint16_t test_bg_control, test_bg_x, test_bg_y;

int camera_world_to_screen_x(const Camera *camera, fix16_t x)
{ return FIX16_TO_INT(x - camera->x); }
int camera_world_to_screen_y(const Camera *camera, fix16_t y)
{ return FIX16_TO_INT(y - camera->y); }

int main(void)
{
    const char *banner = "WELCOME TO OWATA ZONE\n\n\n\n\n\n1                 1                 1";
    Camera camera = {0};
    messages_init();
    messages_show_banner(banner);
    assert(!messages_is_modal() && active_message.screen_fixed);
    assert(active_message.line_count == 7 && active_message.line_len[6] == 37);
    for (int i = 0; i < 1000; i++) messages_update();
    assert(active_message.timer == 1);
    messages_draw(&camera);
    uint16_t saved[1024];
    memcpy(saved, test_bg_map, sizeof(saved));
    camera.x = FIX16_FROM_INT(2000);
    camera.y = FIX16_FROM_INT(100);
    messages_draw(&camera);
    assert(memcmp(saved, test_bg_map, sizeof(saved)) == 0);
    for (int i = 0; i < 3; i++) {
        int tile = MESSAGE_GLYPH_TILE_BASE + active_message.line_tile_offset[6] + i * 9;
        assert(test_bg_chars[tile * 8] != 0);
    }
    assert(!active_message.old_box_w);

    messages_show_text("TEMPORARY", 0, 0, 2);
    assert(!active_message.screen_fixed);
    messages_update();
    assert(!active_message.screen_fixed && active_message.timer == 1);
    messages_update();
    assert(active_message.screen_fixed && active_message.text == banner);

    messages_show_modal_text("HINT", 0, 0);
    assert(messages_is_modal());
    messages_show_banner(banner);
    assert(messages_is_modal());
    messages_dismiss();
    messages_update();
    assert(!messages_is_modal() && active_message.screen_fixed);
    messages_init();
    messages_update();
    assert(!banner_text && active_message.timer == 0);
    return 0;
}
