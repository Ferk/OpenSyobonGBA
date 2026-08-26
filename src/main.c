#include <gba.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "font4x6.h"
#include "level.h"
#include "messages.h"
#include "player.h"
#include "text.h"
#include "traps.h"

int main(void)
{
    irqInit();
    irqSet(IRQ_VBLANK, audio_vblank);
    irqEnable(IRQ_VBLANK);

    REG_DISPCNT = MODE_0 | BG0_ON | BG1_ON | OBJ_ON | OBJ_1D_MAP;

    audio_init();
    audio_play_bgm();

    level_init_video();
    level_load_1_1(&level_current);
    player_init_video();
    traps_init_video();
    enemies_init_video();
    messages_init();
    traps_load_1_1(&traps_current, &level_current);
    enemies_load_1_1(&enemy_current, &level_current);

    Player player = { 0 };
    Camera camera;

    player_spawn(&player);
    camera_init(&camera);

    while (1) {
        player_update(&player, &level_current, &traps_current);
        traps_update(&traps_current, &level_current, &player);
        enemies_update(&enemy_current, &level_current, &player);
        messages_update();

        if (traps_stage_clear_requested(&traps_current)) {
            uint16_t death_count = player_death_count(&player);

            traps_ack_stage_clear(&traps_current);
            level_load_1_1(&level_current);
            traps_load_1_1(&traps_current, &level_current);
            enemies_load_1_1(&enemy_current, &level_current);
            memset(&player, 0, sizeof(player));
            player.death_count = death_count;
            player_spawn(&player);
            camera_init(&camera);
            level_force_stream_update();
            audio_play_bgm();
        }

        camera_update(&camera, &player, &level_current);

        VBlankIntrWait();
        audio_update();

        level_stream_bg0(&level_current, camera_x_px(&camera), camera_y_px(&camera));
        player_draw(&player, &camera);
        traps_draw(&traps_current, &camera);
        enemies_draw(&enemy_current, &camera);
        messages_draw(&camera);
    }
}
