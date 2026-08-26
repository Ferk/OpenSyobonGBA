#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_vblank(void);
void audio_update(void);
void audio_play_bgm(void);
void audio_stop_bgm(void);
void audio_play_jump(void);
void audio_play_block_hit(void);
void audio_play_brick_break(void);
void audio_play_trap_trigger(void);
void audio_play_death(void);
void audio_play_goal(void);

#endif
