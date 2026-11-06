#ifndef __MESH_TIMER_H__
#define __MESH_TIMER_H__

typedef void (*mesh_timeout_handler)(void *ctx, void *arg);

void mesh_timer_init(void);
int mesh_timer_set(mesh_timeout_handler handler, void *arg, unsigned int time_ms);
int mesh_timer_clear(mesh_timeout_handler handler);
long long mesh_timer_schedule(void *ctx);
void mesh_timer_signal(void);
void mesh_timer_for_signal(int wait_time_ms);

void mesh_timer_deinit(void);

#endif
