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
static const char *last_notice;

void audio_play_block_hit(void) { sounds++; }
void audio_play_trap_trigger(void) { sounds++; }
void audio_play_brick_break(void) { sounds++; }
void audio_stop_bgm(void) {}
void audio_play_goal(void) { sounds++; }
void messages_show_text(const char *text, fix16_t x, fix16_t y, uint8_t frames)
{ last_notice = text; (void)x; (void)y; (void)frames; notices++; }
void messages_show_banner(const char *text) { last_notice = text; notices++; }
void messages_show_modal_text(const char *text, fix16_t x, fix16_t y)
{ (void)text; (void)x; (void)y; notices++; }
void messages_show(MessageId id, fix16_t x, fix16_t y, uint8_t frames)
{ (void)id; (void)x; (void)y; (void)frames; notices++; }
void player_kill(Player *player) { player->alive = 0; }
void player_set_supported(Player *player) { player->on_ground = 1; }
int level_camera_min_y_px(const Level *level) { (void)level; return -80; }
int level_camera_max_y_px(const Level *level) { (void)level; return 32; }
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

static void test_entrance_and_underground(void)
{
    static TrapManager manager;
    static Level level;
    Player player = {0};
    level.width = 160;
    level.height = 17;
    memset(&manager, 0, sizeof(manager));
    memset(&enemy_current, 0, sizeof(enemy_current));
    manager.trap_count = 2;
    Trap *block = &manager.traps[0];
    Trap *shot = &manager.traps[1];
    block->kind = TRAP_QUESTION_BLOCK;
    block->subtype = QUESTION_HIDDEN_POISON;
    block->trigger_channel = 31;
    block->spent_metatile = 255;
    shot->kind = TRAP_STAGE_SPAWNER;
    shot->disable_channel = 31;
    shot->spawn_enemy_kind = ENEMY_PIPE_SHOT;
    shot->spawn_enemy_count = 1;
    shot->spawn_limit = 1;
    trigger_question_block(&manager, block, &level, &player);
    assert(shot->state == TRAP_SPENT);
    trigger_stage_spawner(&manager, shot, &level, &player);
    assert(enemy_current.count == 0);
    shot->state = TRAP_IDLE;
    trigger_stage_spawner(&manager, shot, &level, &player);
    assert(enemy_current.count == 1);
    emit_trap_channel(&manager, &level, &player, 31);
    assert(shot->state == TRAP_SPENT && enemy_current.count == 1);

    memset(&manager, 0, sizeof(manager));
    manager.trap_count = 1;
    block = &manager.traps[0];
    block->kind = TRAP_QUESTION_BLOCK;
    block->subtype = QUESTION_FRAGILE_COIN;
    block->spent_metatile = 255;
    block->hidden = 1;
    block->visual_palette = 1;
    trigger_question_block(&manager, block, &level, &player);
    assert(block->kind == TRAP_BREAKING_PLATFORM && block->state == TRAP_IDLE);
    assert(!block->hidden && block->visual_metatile == METATILE_SOLID);
    assert(manager.dynamic_collider_count == 1);
    assert(manager.entities[0].kind == TRAP_ENTITY_COIN_POPUP);
    assert(level.metatiles[0][0] == METATILE_SOLID);
    player.alive = 1;
    player.y = FIX16_FROM_INT(-PLAYER_HEIGHT_PX);
    player.x = 0;
    player.vy = 0;
    block->w = block->h = FIX16_FROM_INT(16);
    player.on_ground = 1;
    update_breaking_platform(&manager, block, &level, &player);
    assert(block->state == TRAP_SPENT && level.metatiles[0][0] == METATILE_EMPTY);

    memset(&manager, 0, sizeof(manager));
    shot = &manager.traps[0];
    manager.trap_count = 1;
    shot->kind = TRAP_STAGE_SPAWNER;
    shot->spawn_enemy_kind = ENEMY_KUMA;
    shot->spawn_enemy_count = 1;
    shot->spawn_limit = 1;
    shot->activation_limit = 2;
    shot->repeat_rearm_offset_x = -FIX16_ONE;
    trigger_stage_spawner(&manager, shot, &level, &player);
    assert(shot->spawn_count == 1 && shot->state == TRAP_ACTIVE);
    shot->state = TRAP_IDLE;
    trigger_stage_spawner(&manager, shot, &level, &player);
    assert(shot->spawn_count == 2 && shot->state == TRAP_SPENT);
    shot->state = TRAP_IDLE;
    trigger_stage_spawner(&manager, shot, &level, &player);
    assert(shot->spawn_count == 2 && shot->state == TRAP_SPENT);

    memset(&manager, 0, sizeof(manager));
    manager.trap_count = 2;
    for (int i = 0; i < 2; i++) {
        Trap *lift = &manager.traps[i];
        lift->kind = TRAP_MOVING_PLATFORM;
        lift->listen_channel = lift->trigger_channel = 32;
        lift->vy = FIX16_ONE;
        lift->channel_vy = 3 * FIX16_ONE;
        lift->w = FIX16_FROM_INT(48);
        lift->h = FIX16_FROM_INT(16);
    }
    player.y = FIX16_FROM_INT(-PLAYER_HEIGHT_PX);
    player.on_ground = 1;
    update_moving_platform(&manager, &manager.traps[0], &level, &player);
    assert(manager.traps[0].vy == 3 * FIX16_ONE);
    assert(manager.traps[1].vy == 3 * FIX16_ONE);

    for (int subtype = PIPE_FIREWORKS; subtype <= PIPE_OWATA; subtype += 3) {
        Trap pipe = {0};
        pipe.state = TRAP_ACTIVE;
        pipe.subtype = subtype;
        pipe.timer = 18;
        pipe.w = FIX16_FROM_INT(32);
        player.alive = 1;
        update_enter_pipe(&manager, &pipe, &player);
        assert(player.alive && pipe.timer == 19);
        update_enter_pipe(&manager, &pipe, &player);
        assert(!player.alive && pipe.state == TRAP_SPENT && !player.hidden);
        assert(strcmp(last_notice, subtype == PIPE_FIREWORKS
                                       ? TXT_MSG_FIREWORKS : TXT_MSG_DONE_FOR) == 0);
        assert(!manager.stage_transition_requested);
    }
}

int main(void)
{
    test_reuse();
    test_traps();
    test_entrance_and_underground();
    puts("runtime spawning checks passed");
    return 0;
}
