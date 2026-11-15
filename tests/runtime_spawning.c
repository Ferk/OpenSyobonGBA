#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Include traps to exercise its private activation paths without test hooks. */
#include "../src/traps.c"

OBJATTR test_oam[128];
uint16_t test_sprite_gfx[16384];
uint16_t test_sprite_palette[256];
uint16_t test_keys;
static int sounds, notices;

void audio_play_block_hit(void) { sounds++; }
void audio_play_trap_trigger(void) { sounds++; }
void audio_play_brick_break(void) { sounds++; }
void audio_stop_bgm(void) {}
void audio_play_goal(void) { sounds++; }
void messages_show_text(const char *text, fix16_t x, fix16_t y, uint8_t frames)
{ (void)text; (void)x; (void)y; (void)frames; notices++; }
void messages_show_modal_text(const char *text, fix16_t x, fix16_t y)
{ (void)text; (void)x; (void)y; notices++; }
void messages_show(MessageId id, fix16_t x, fix16_t y, uint8_t frames)
{ (void)id; (void)x; (void)y; (void)frames; notices++; }
void player_kill(Player *player) { player->alive = 0; }
void player_set_checkpoint_flag(Player *p, fix16_t x, fix16_t y, fix16_t sx, fix16_t sy)
{ (void)p; (void)x; (void)y; (void)sx; (void)sy; }
void player_begin_goal(Player *p, fix16_t x, uint16_t frames)
{ (void)p; (void)x; (void)frames; }
void level_set_metatile_cell_palette(Level *l, uint16_t x, uint16_t y, uint8_t tile, uint8_t palette)
{
    if (x < l->width && y < l->height) {
        l->metatiles[y][x] = tile;
        l->palettes[y][x] = palette;
    }
}

static void fill_pool(void)
{
    memset(&enemy_current, 0, sizeof(enemy_current));
    for (int i = 0; i < ENEMY_MAX_ACTIVE; i++) {
        assert(enemies_spawn_direct(&enemy_current, ENEMY_WALKER, 0, 0,
                                     ENEMY_SPRITE_WALKER, -1));
    }
    assert(enemies_available_slots(&enemy_current) == 0);
}

static void test_reuse(void)
{
    fill_pool();
    Enemy saved[ENEMY_MAX_ACTIVE];
    memcpy(saved, enemy_current.enemies, sizeof(saved));
    assert(!enemies_spawn_from_block(&enemy_current, 0, 0, ENEMY_SPRITE_WALKER, 1));
    assert(!enemies_spawn_direct(&enemy_current, ENEMY_WALKER, 0, 0, 0, 1));
    assert(!enemies_spawn_direct_velocity(&enemy_current, ENEMY_LASER, 0, 0, 0, 0, 0, 1));
    assert(memcmp(saved, enemy_current.enemies, sizeof(saved)) == 0);
    enemy_current.enemies[3].state = ENEMY_STATE_DEAD;
    assert(enemies_available_slots(&enemy_current) == 0);
    for (int i = 0; i < 256; i++) {
        Enemy *slot = &enemy_current.enemies[7];
        memset(slot, 0xff, sizeof(*slot));
        slot->state = ENEMY_STATE_EMPTY;
        assert(enemies_available_slots(&enemy_current) == 1);
        assert(enemies_spawn_direct_config(&enemy_current, ENEMY_LASER,
                                           11, 22, 33, 44, ENEMY_SPRITE_LASER, 0, 0, 1));
        assert(enemy_current.count == ENEMY_MAX_ACTIVE);
        assert(slot->x == 11 && slot->y == 22 && slot->vx == 33 && slot->vy == 44);
        assert(slot->w == 64 && slot->h == 8 && slot->state == ENEMY_STATE_ACTIVE);
        assert(slot->captured_entity == 0xff && slot->hold_timer == 0);
        assert(slot->emerge_timer == 0 && slot->timer == 0 && slot->launched == 0);
    }
    assert(enemy_current.enemies[3].state == ENEMY_STATE_DEAD);
    for (int i = 0; i < ENEMY_MAX_ACTIVE; i++) {
        if (i != 3 && i != 7) {
            assert(memcmp(&saved[i], &enemy_current.enemies[i], sizeof(Enemy)) == 0);
        }
    }
    enemy_current.enemies[7].state = ENEMY_STATE_EMPTY;
    assert(enemies_spawn_from_block(&enemy_current, 10, 20, ENEMY_SPRITE_WALKER, -1));
    assert(enemy_current.enemies[7].emerge_timer == 16);
}

static void test_traps(void)
{
    static TrapManager manager;
    static Level level;
    Player player = {0};
    level.width = 150;
    level.height = 17;
    fill_pool();
    Trap *trap = &manager.traps[0];
    manager.trap_count = 2;
    trap->kind = TRAP_STAGE_SPAWNER;
    trap->spawn_enemy_kind = ENEMY_LASER;
    trap->spawn_enemy_sprite = ENEMY_SPRITE_LASER;
    trap->spawn_enemy_count = 3;
    trap->spawn_limit = 1;
    trap->spawn_sound = 1;
    trap->spawn_vx = FIX16_ONE;
    trap->spawn_dir = -1;
    trap->hint_text = "CANNON!!";
    trap->trigger_channel = 1;
    manager.traps[1].kind = TRAP_HINT_BLOCK;
    manager.traps[1].listen_channel = 1;
    manager.traps[1].trigger_delay = 5;
    sounds = notices = 0;
    trigger_stage_spawner(&manager, trap, &level, &player);
    assert(trap->state == TRAP_IDLE && trap->spawn_count == 0 && trap->trigger_pending);
    assert(!sounds && !notices && !manager.traps[1].trigger_pending);
    enemy_current.enemies[0].state = ENEMY_STATE_EMPTY;
    enemy_current.enemies[1].state = ENEMY_STATE_EMPTY;
    update_linked_traps(&manager, &level, &player);
    assert(enemies_available_slots(&enemy_current) == 2);
    assert(trap->state == TRAP_IDLE && trap->spawn_count == 0);
    enemy_current.enemies[2].state = ENEMY_STATE_EMPTY;
    update_linked_traps(&manager, &level, &player);
    assert(enemies_available_slots(&enemy_current) == 0);
    assert(trap->state == TRAP_SPENT && trap->spawn_count == 1 && !trap->trigger_pending);
    assert(sounds == 1 && notices == 1 && manager.traps[1].trigger_pending);
    trigger_stage_spawner(&manager, trap, &level, &player);
    assert(sounds == 1 && notices == 1);

    memset(&manager, 0, sizeof(manager));
    trap = &manager.traps[0];
    trap->kind = TRAP_QUESTION_BLOCK;
    trap->subtype = QUESTION_ENEMY;
    trap->hidden = 1;
    trap->spent_metatile = 255;
    trigger_question_block(&manager, trap, &level, &player);
    assert(trap->state == TRAP_IDLE && trap->hidden);
    enemy_current.enemies[7].state = ENEMY_STATE_EMPTY;
    trigger_question_block(&manager, trap, &level, &player);
    assert(trap->state == TRAP_SPENT && !trap->hidden);
    assert(enemy_current.enemies[7].emerge_timer == 16);

    memset(&manager, 0, sizeof(manager));
    trap = &manager.traps[0];
    trap->spawn_enemy_kind = ENEMY_PIPE_SHOT;
    trap->spawn_enemy_count = 1;
    trap->spawn_interval = 255;
    trap->timer = 254;
    for (int i = 0; i < 300; i++) {
        update_stage_pipe_hazard(trap, &player);
        assert(trap->timer == 255 && trap->spawn_count == 0);
    }
    enemy_current.enemies[7].state = ENEMY_STATE_EMPTY;
    update_stage_pipe_hazard(trap, &player);
    assert(trap->timer == 0 && trap->spawn_count == 1);
}

int main(void)
{
    test_reuse();
    test_traps();
    puts("runtime spawning checks passed");
    return 0;
}
