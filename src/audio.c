#include "audio.h"

#ifdef USE_MAXMOD
#include <stdint.h>

#include <maxmod.h>

#include "soundbank.h"

#define AUDIO_BGM_VOLUME 768

extern const unsigned char soundbank_bin[];

static uint8_t bgm_should_loop;

static void play_effect(mm_word effect_id)
{
    mm_sound_effect effect = {
        .id = effect_id,
        .rate = 1024,
        .handle = 0,
        .volume = 255,
        .panning = 128,
    };

    mmEffectEx(&effect);
}
#endif

void audio_init(void)
{
#ifdef USE_MAXMOD
    mmInitDefault((mm_addr)soundbank_bin, 8);
    mmSetModuleVolume(AUDIO_BGM_VOLUME);
#endif
}

void audio_vblank(void)
{
#ifdef USE_MAXMOD
    mmVBlank();
#endif
}

void audio_update(void)
{
#ifdef USE_MAXMOD
    mmFrame();
    if (bgm_should_loop && !mmActive()) {
#if defined(MOD_BGM1_1)
        mmStart(MOD_BGM1_1, MM_PLAY_LOOP);
#endif
        mmSetModuleVolume(AUDIO_BGM_VOLUME);
    }
#endif
}

void audio_play_bgm(void)
{
#if defined(USE_MAXMOD) && defined(MOD_BGM1_1)
    bgm_should_loop = 1;
    mmStart(MOD_BGM1_1, MM_PLAY_LOOP);
    mmSetModuleVolume(AUDIO_BGM_VOLUME);
#endif
}

void audio_stop_bgm(void)
{
#ifdef USE_MAXMOD
    bgm_should_loop = 0;
    mmStop();
#endif
}

void audio_play_jump(void)
{
#if defined(USE_MAXMOD) && defined(SFX_JUMP)
    play_effect(SFX_JUMP);
#endif
}

void audio_play_block_hit(void)
{
#if defined(USE_MAXMOD) && defined(SFX_BLOCK_HIT)
    play_effect(SFX_BLOCK_HIT);
#endif
}

void audio_play_brick_break(void)
{
#if defined(USE_MAXMOD) && defined(SFX_BRICK_BREAK)
    play_effect(SFX_BRICK_BREAK);
#else
    audio_play_block_hit();
#endif
}

void audio_play_trap_trigger(void)
{
#if defined(USE_MAXMOD) && defined(SFX_TRAP_TRIGGER)
    play_effect(SFX_TRAP_TRIGGER);
#endif
}

void audio_play_death(void)
{
#if defined(USE_MAXMOD) && defined(SFX_DEATH)
    play_effect(SFX_DEATH);
#endif
}

void audio_play_goal(void)
{
#if defined(USE_MAXMOD) && defined(SFX_GOAL)
    play_effect(SFX_GOAL);
#else
    audio_play_trap_trigger();
#endif
}
