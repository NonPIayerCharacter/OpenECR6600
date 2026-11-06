#include "mesh_timer.h"
#include "mesh_common.h"

typedef struct mesh_timer_tag {
    struct dl_list list;
    mesh_timeout_handler cb;
    void *arg;
    long long time;
}mesh_timer_tag_t;

typedef struct {
    struct dl_list timeout;
    struct dl_list execute;
    os_mutex_handle_t timer_mutex;
    os_sem_handle_t timer_signal;
}mesh_timer_ctx_t;

static mesh_timer_ctx_t mesh_timer_ctx;

__INLINE mesh_timer_ctx_t *mesh_timer_ctx_get(void)
{
    return &mesh_timer_ctx;
}

void mesh_timer_init(void)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();

    dl_list_init(&ctx->timeout);
    dl_list_init(&ctx->execute);
    ctx->timer_mutex = os_mutex_create();
    ctx->timer_signal = os_sem_create(1,0);
}

int mesh_timer_clear(mesh_timeout_handler handler)
{
    mesh_timer_tag_t *timeout, *prev;
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();
    int clear = 0;

    os_mutex_lock(ctx->timer_mutex, WAIT_FOREVER);
    dl_list_for_each_safe(timeout, prev, &ctx->timeout,
        mesh_timer_tag_t, list) {
        if (timeout->cb== handler) {
            dl_list_del(&timeout->list);
            os_free(timeout);
            clear = 1;
        }
    }

    if(!clear && dl_list_len(&ctx->execute)){
        dl_list_for_each_safe(timeout, prev, &ctx->execute,
            mesh_timer_tag_t, list) {
            if (timeout->cb== handler) {
                dl_list_del(&timeout->list);
                os_free(timeout);
                clear = 1;
            }
        }
    }
    os_mutex_unlock(ctx->timer_mutex);

    return clear ? 0 :-1;
}

int mesh_timer_is_set(mesh_timeout_handler handler, void *arg, unsigned int time_ms)
{
    mesh_timer_tag_t *timeout, *prev;
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();

    dl_list_for_each_safe(timeout, prev, &ctx->timeout,
        mesh_timer_tag_t, list) {
        if (timeout->cb == handler && (timeout->arg == arg)) {
            return 0;
        }
    }

    return -1;
}

int mesh_timer_set(mesh_timeout_handler handler, void *arg, unsigned int time_ms)
{
    mesh_timer_tag_t *mesh_timer = NULL, *tmp = NULL, *prev, *last_timer = NULL;
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();
    int cnt;

    os_mutex_lock(ctx->timer_mutex, WAIT_FOREVER);

    dl_list_for_each_safe(tmp, prev, &ctx->timeout,
        mesh_timer_tag_t, list) {
        if (tmp->cb== handler) {
            dl_list_del(&tmp->list);
            last_timer = tmp;
            break;
        }
    }

    if (last_timer) {
        mesh_timer = last_timer;
    } else {
        mesh_timer = (mesh_timer_tag_t *) os_zalloc(sizeof(mesh_timer_tag_t));
        if (!mesh_timer) {
            os_mutex_unlock(ctx->timer_mutex);
            mesh_e("%s %d", __func__, __LINE__);
            return -1;
        }
    }

    mesh_timer->time = os_time_get();
    mesh_timer->arg = arg;
    mesh_timer->cb = handler;
    mesh_timer->time += time_ms;

    if (time_ms == 0) {
        dl_list_add_tail(&ctx->execute, &mesh_timer->list);
        mesh_timer_signal();
        os_mutex_unlock(ctx->timer_mutex);
        return 0;
    }

    if (dl_list_empty(&ctx->timeout)) {
        dl_list_add_tail(&ctx->timeout, &mesh_timer->list);
    } else {
        cnt = 0;
        dl_list_for_each(tmp, &ctx->timeout, mesh_timer_tag_t, list) {
            if (mesh_timer->time < tmp->time) {
                    dl_list_add(tmp->list.prev, &mesh_timer->list);
                    if (cnt == 0) {
                        mesh_timer_signal();
                    }
                    os_mutex_unlock(ctx->timer_mutex);
                    return 0;
            }
            cnt++;
        }

        dl_list_add_tail(&ctx->timeout, &mesh_timer->list);
    }
    os_mutex_unlock(ctx->timer_mutex);

    return 0;
}

long long mesh_timer_rearrange_timeout(void *env)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();
    mesh_timer_tag_t *timeout, *prev, *first;
    long long time;
    long long sleep_time = 0;

    time = os_time_get();
    os_mutex_lock(ctx->timer_mutex, WAIT_FOREVER);
    if (dl_list_empty(&ctx->timeout)) {
        os_mutex_unlock(ctx->timer_mutex);
        return 0;
    }

    dl_list_for_each_safe(timeout, prev, &ctx->timeout,
        mesh_timer_tag_t, list) {
            if(time >= timeout->time) {
                dl_list_del(&timeout->list);
                dl_list_add_tail(&ctx->execute, &timeout->list);
            }
    }

    first = dl_list_first(&ctx->timeout, mesh_timer_tag_t, list);
    if (first) {
        sleep_time = first->time - time;
    }
    os_mutex_unlock(ctx->timer_mutex);

    return sleep_time;
}

long long mesh_timer_schedule(void *env)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();
    mesh_timer_tag_t *timeout;
    long long wait_time;
    struct dl_list *head;

#if 0
    dl_list_for_each_safe(timeout, prev, &ctx->execute,
        mesh_timer_tag_t, list) {
        os_mutex_lock(ctx->timer_mutex, WAIT_FOREVER);
        dl_list_del(&timeout->list);
        os_mutex_unlock(ctx->timer_mutex);
        timeout->cb(env, timeout->arg);
        os_free(timeout);
    }
#endif

    do {
        os_mutex_lock(ctx->timer_mutex, WAIT_FOREVER);
        head = &ctx->execute;
        if (dl_list_empty(head)) {
            os_mutex_unlock(ctx->timer_mutex);
            break;
        }
        timeout = dl_list_entry(head->next, mesh_timer_tag_t, list);
        dl_list_del(&timeout->list);
        os_mutex_unlock(ctx->timer_mutex);
        timeout->cb(env, timeout->arg);
        os_free(timeout);
    }while(1);

    wait_time = mesh_timer_rearrange_timeout(env);
    if (wait_time == 0) {
        wait_time = WAIT_FOREVER;
    }

    if (!dl_list_empty(&ctx->execute)){
        wait_time = 0;
    }

    return wait_time;
}

void  mesh_timer_signal(void)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();

    os_sem_post(ctx->timer_signal);
}

void mesh_timer_for_signal(int wait_time_ms)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();

    os_sem_wait(ctx->timer_signal, wait_time_ms);
}

void mesh_timer_deinit(void)
{
    mesh_timer_ctx_t *ctx = mesh_timer_ctx_get();
    mesh_timer_tag_t *timeout, *prev;

    os_mutex_unlock(ctx->timer_mutex);
    dl_list_for_each_safe(timeout, prev, &ctx->timeout,
        mesh_timer_tag_t, list) {
            dl_list_del(&timeout->list);
            os_free(timeout);
    }

    dl_list_for_each_safe(timeout, prev, &ctx->execute,
        mesh_timer_tag_t, list) {
            dl_list_del(&timeout->list);
            os_free(timeout);
    }

    os_mutex_unlock(ctx->timer_mutex);
    os_mutex_destroy(ctx->timer_mutex);
    os_sem_destroy(ctx->timer_signal);
}


