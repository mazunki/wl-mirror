#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <wlm/context.h>
#include <wlm/util.h>

#define WLM_LOG_COMPONENT event

static wlm_event_loop_handler_t * min_timeout(ctx_t * ctx) {
    wlm_event_loop_handler_t * min = NULL;
    wlm_event_loop_handler_t * cur;
    wl_list_for_each(cur, &ctx->event.handlers, link) {
        if (cur->timeout_ms == -1) continue;
        if (min == NULL || cur->timeout_ms < min->timeout_ms) min = cur;
    }

    return min;
}

void wlm_event_loop_zero(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "zeroing");

    // epoll fd
    ctx->event.pollfd = -1;

    // event handlers
    wl_list_init(&ctx->event.handlers);
}

void wlm_event_loop_init(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "initializing");

    // initialize epoll fd
    ctx->event.pollfd = epoll_create(1);
    if (ctx->event.pollfd == -1) {
        wlm_log(ctx, WLM_ERROR, "%s", strerror(errno));
        wlm_exit_fail(ctx);
        return;
    }
}

void wlm_event_loop_cleanup(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "cleaning up");

    // epoll fd
    if (ctx->event.pollfd != -1) close(ctx->event.pollfd);

    // event handlers are owned by their respective subsystems

    wlm_event_loop_zero(ctx);
}

void wlm_event_loop_handler_zero(struct ctx * ctx, wlm_event_loop_handler_t * handler) {
    wl_list_init(&handler->link);

    handler->fd = -1;
    handler->events = 0;
    handler->timeout_ms = -1;

    handler->on_event = NULL;

    (void)ctx;
}

void wlm_event_loop_run(ctx_t * ctx) {
    struct epoll_event events[EVENT_LOOP_MAX_EVENTS];
    int num_events;

    wlm_event_loop_handler_t * timeout_handler;
    int timeout_ms;

    wlm_event_emit_before_poll(ctx);
    timeout_handler = min_timeout(ctx);
    timeout_ms = timeout_handler == NULL ? -1 : timeout_handler->timeout_ms;

    while (!wlm_wayland_core_is_closing(ctx) && (num_events = epoll_wait(ctx->event.pollfd, events, WLM_ARRAY_LENGTH(events), timeout_ms)) != -1) {
        for (int i = 0; i < num_events; i++) {
            wlm_event_loop_handler_t * handler = (wlm_event_loop_handler_t *)events[i].data.ptr;
            handler->on_event(ctx);
        }

        if (num_events == 0 && timeout_handler != NULL) {
            timeout_handler->on_event(ctx);
        }

        wlm_event_emit_before_poll(ctx);
        timeout_handler = min_timeout(ctx);
        timeout_ms = timeout_handler == NULL ? -1 : timeout_handler->timeout_ms;
    }
}

void wlm_event_loop_add_fd(ctx_t * ctx, wlm_event_loop_handler_t * handler) {
    struct epoll_event event;
    event.events = handler->events;
    event.data.ptr = handler;

    if (epoll_ctl(ctx->event.pollfd, EPOLL_CTL_ADD, handler->fd, &event) == -1) {
        wlm_log(ctx, WLM_ERROR, "%s", strerror(errno));
        wlm_exit_fail(ctx);
    }

    wl_list_insert(&ctx->event.handlers, &handler->link);
}

void wlm_event_loop_change_fd(ctx_t * ctx, wlm_event_loop_handler_t * handler) {
    struct epoll_event event;
    event.events = handler->events;
    event.data.ptr = handler;

    if (epoll_ctl(ctx->event.pollfd, EPOLL_CTL_MOD, handler->fd, &event) == -1) {
        wlm_log(ctx, WLM_ERROR, "%s", strerror(errno));
        wlm_exit_fail(ctx);
    }
}

void wlm_event_loop_remove_fd(ctx_t * ctx, wlm_event_loop_handler_t * handler) {
    if (epoll_ctl(ctx->event.pollfd, EPOLL_CTL_DEL, handler->fd, NULL) == -1) {
        wlm_log(ctx, WLM_ERROR, "%s", strerror(errno));
    }

    wl_list_remove(&handler->link);
}
