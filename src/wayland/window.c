#include <math.h>
#include <wlm/context.h>

#define WLM_LOG_COMPONENT wayland

// --- private helper functions ---

static bool window_configure_ready(ctx_t * ctx) {
    return (ctx->wl.window.flags & WLM_WAYLAND_WINDOW_READY) == WLM_WAYLAND_WINDOW_READY;
}

static bool window_complete(ctx_t * ctx) {
    return (ctx->wl.window.flags & WLM_WAYLAND_WINDOW_COMPLETE) == WLM_WAYLAND_WINDOW_COMPLETE;
}

static bool use_output_scale(ctx_t * ctx) {
    return ctx->wl.protocols.fractional_scale_manager == NULL && wl_surface_get_version(ctx->wl.window.surface) < 6;
}

static bool use_surface_preferred_scale(ctx_t * ctx) {
    return ctx->wl.protocols.fractional_scale_manager == NULL && wl_surface_get_version(ctx->wl.window.surface) >= 6;
}

static bool use_fractional_preferred_scale(ctx_t * ctx) {
    return ctx->wl.protocols.fractional_scale_manager != NULL;
}

static void apply_surface_changes(ctx_t * ctx) {
    if (!ctx->wl.window.changed) return;

    if (ctx->wl.window.changed & WLM_WAYLAND_WINDOW_SIZE_CHANGED) {
        wlm_log(ctx, WLM_DEBUG, "new viewport destination size = %dx%d",
            ctx->wl.window.width, ctx->wl.window.height
        );
        wp_viewport_set_destination(ctx->wl.window.viewport, ctx->wl.window.width, ctx->wl.window.height);
    }

    if ((ctx->wl.window.changed & WLM_WAYLAND_WINDOW_SIZE_CHANGED) || (ctx->wl.window.changed & WLM_WAYLAND_WINDOW_SCALE_CHANGED)) {
        uint32_t buffer_width = round(ctx->wl.window.width * ctx->wl.window.scale);
        uint32_t buffer_height = round(ctx->wl.window.height * ctx->wl.window.scale);
        if (ctx->wl.window.buffer_width != buffer_width || ctx->wl.window.buffer_height != buffer_height) {
            wlm_log(ctx, WLM_DEBUG, "new buffer size = %dx%d", buffer_width, buffer_height);

            ctx->wl.window.buffer_width = buffer_width;
            ctx->wl.window.buffer_height = buffer_height;
            ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_BUFFER_SIZE_CHANGED;
        }
    }

    // TODO: react to preferred transform

    // trigger buffer resize and render
    if (!window_complete(ctx)) {
        ctx->wl.window.init_done = true;
        wlm_event_emit_window_init_done(ctx);
    } else {
        wlm_event_emit_window_changed(ctx);
    }

    // TODO: who performs wl_surface_commit()?
    wl_surface_commit(ctx->wl.window.surface);

    ctx->wl.window.changed = WLM_WAYLAND_WINDOW_UNCHANGED;
    ctx->wl.window.flags |= WLM_WAYLAND_WINDOW_COMPLETE;
}

static void trigger_initial_configure(ctx_t * ctx) {
    // TODO: set fullscreen here if already requested

    // set libdecor app properties
    libdecor_frame_set_app_id(ctx->wl.window.libdecor_frame, "at.yrlf.wl_mirror");
    libdecor_frame_set_title(ctx->wl.window.libdecor_frame, "Wayland Output Mirror");

    // map libdecor frame
    wlm_log(ctx, WLM_DEBUG, "mapping frame");
    libdecor_frame_map(ctx->wl.window.libdecor_frame);
}

// --- xdg_wm_base event handlers ---

static void on_xdg_wm_base_ping(
    void * data, struct xdg_wm_base * xdg_wm_base,
    uint32_t serial
) {
    if (xdg_wm_base == NULL) return;

    ctx_t * ctx = (ctx_t *)data;
    xdg_wm_base_pong(ctx->wl.protocols.xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = on_xdg_wm_base_ping,
};

// --- surface event handlers ---

static void on_surface_enter(
    void * data, struct wl_surface * surface,
    struct wl_output * output
) {
    if (surface == NULL) return;
    if (!wlm_wayland_registry_is_own_proxy((struct wl_proxy *)output)) return;

    ctx_t * ctx = (ctx_t *)data;
    wlm_wayland_output_entry_t * entry = wlm_wayland_output_find(ctx, output);
    if (ctx->wl.window.current_output == entry) return;

    wlm_log(ctx, WLM_DEBUG, "entering output %s", entry->name);

    ctx->wl.window.current_output = entry;
    ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_OUTPUT_CHANGED;

    if (use_output_scale(ctx) && ctx->wl.window.scale != entry->scale) {
        wlm_log(ctx, WLM_INFO, "using output scale = %d", entry->scale);

        ctx->wl.window.scale = entry->scale;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_SCALE_CHANGED;
    }
}

static void on_surface_leave(
    void * data, struct wl_surface * surface,
    struct wl_output * output
) {
    if (surface == NULL) return;
    if (!wlm_wayland_registry_is_own_proxy((struct wl_proxy *)output)) return;

    ctx_t * ctx = (ctx_t *)data;
    wlm_wayland_output_entry_t * entry = wlm_wayland_output_find(ctx, output);
    if (ctx->wl.window.current_output != entry) return;

    wlm_log(ctx, WLM_DEBUG, "leaving output %s", entry->name);

    // ignore: current output is set with next entered output anyway
}

static void on_surface_preferred_buffer_scale(
    void * data, struct wl_surface * surface,
    int32_t scale
) {
    if (surface == NULL) return;

    ctx_t * ctx = (ctx_t *)data;

    if (use_surface_preferred_scale(ctx) && ctx->wl.window.scale != scale) {
        wlm_log(ctx, WLM_INFO, "using preferred integer scale = %d", scale);

        ctx->wl.window.scale = scale;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_SCALE_CHANGED;
    }
}

static void on_surface_preferred_buffer_transform(
    void * data, struct wl_surface * surface,
    uint32_t transform_int
) {
    if (surface == NULL) return;

    ctx_t * ctx = (ctx_t *)data;
    enum wl_output_transform transform = (enum wl_output_transform)transform_int;


    if (ctx->wl.window.transform != transform) {
        wlm_log(ctx, WLM_INFO, "using preferred transform = %s",
            WLM_PRINT_OUTPUT_TRANSFORM(transform)
        );

        ctx->wl.window.transform = transform;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_TRANSFORM_CHANGED;
    }
}

static const struct wl_surface_listener surface_listener = {
    .enter = on_surface_enter,
    .leave = on_surface_leave,
    .preferred_buffer_scale = on_surface_preferred_buffer_scale,
    .preferred_buffer_transform = on_surface_preferred_buffer_transform,
};

// --- fractional_scale event handlers ---

static void on_fractional_scale_preferred_scale(
    void * data, struct wp_fractional_scale_v1 * fractional_scale,
    uint32_t scale_times_120
) {
    if (fractional_scale == NULL) return;

    ctx_t * ctx = (ctx_t *)data;
    double scale = scale_times_120 / 120.;

    if (use_fractional_preferred_scale(ctx) && ctx->wl.window.scale != scale) {
        wlm_log(ctx, WLM_INFO, "using preferred fractional scale = %.3f", scale);

        ctx->wl.window.scale = scale;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_SCALE_CHANGED;
    }
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = on_fractional_scale_preferred_scale,
};

// --- libdecor_frame event handlers ---

static void on_libdecor_frame_configure(
    struct libdecor_frame * frame,
    struct libdecor_configuration * configuration, void * data
) {
    if (frame == NULL) return;

    ctx_t * ctx = (ctx_t *)data;
    wlm_log(ctx, WLM_DEBUG, "configuring");

    // get minimum window size
    int min_width = 0;
    int min_height = 0;
    libdecor_frame_get_min_content_size(frame, &min_width, &min_height);

    // set minimum window size
    int new_min_width = min_width;
    int new_min_height = min_height;
    if (new_min_width < DEFAULT_WIDTH) new_min_width = DEFAULT_WIDTH;
    if (new_min_height < DEFAULT_HEIGHT) new_min_height = DEFAULT_HEIGHT;
    if (new_min_width != min_width || new_min_height != min_height) {
        wlm_log(ctx, WLM_DEBUG, "setting minimum size = %dx%d", new_min_width, new_min_height);
        libdecor_frame_set_min_content_size(frame, new_min_width, new_min_height);
    }

    // get window size
    int width = 0;
    int height = 0;
    if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
        if (ctx->wl.window.width == 0 || ctx->wl.window.height == 0) {
            wlm_log(ctx, WLM_DEBUG, "falling back to default size");
            width = new_min_width;
            height = new_min_height;
        } else {
            wlm_log(ctx, WLM_DEBUG, "falling back to previous size");
            width = ctx->wl.window.width;
            height = ctx->wl.window.height;
        }
    }

    // update window size
    if (ctx->wl.window.width != (uint32_t)width || ctx->wl.window.height != (uint32_t)height) {
        ctx->wl.window.width = width;
        ctx->wl.window.height = height;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_SIZE_CHANGED;
    }

    // get fullscreen state
    bool is_fullscreen = false;
    enum libdecor_window_state window_state;
    if (libdecor_configuration_get_window_state(configuration, &window_state)) {
        if (window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN) {
            is_fullscreen = true;
        }
    }

    // TODO: update actual fullscreen state

    // commit window configuration
    struct libdecor_state * state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);
    (void)is_fullscreen;

    // check if things changed, emit events
    apply_surface_changes(ctx);
}

static void on_libdecor_frame_commit(
    struct libdecor_frame * frame, void * data
) {
    // ignore: don't need to know when frame committed
    (void)frame;
    (void)data;
}

static void on_libdecor_frame_close(
    struct libdecor_frame * frame, void * data
) {
    if (frame == NULL) return;

    ctx_t * ctx = (ctx_t *)data;
    wlm_log(ctx, WLM_DEBUG, "close requested");

    wlm_wayland_core_request_close(ctx);
}

static void on_libdecor_frame_dismiss_popup(
    struct libdecor_frame * frame,
    const char * seat_name, void * data
) {
    (void)frame;
    (void)seat_name;
    (void)data;
}

static struct libdecor_frame_interface libdecor_frame_listener = {
    .configure = on_libdecor_frame_configure,
    .commit = on_libdecor_frame_commit,
    .close = on_libdecor_frame_close,
    .dismiss_popup = on_libdecor_frame_dismiss_popup
};

// --- internal event handlers ---

void wlm_wayland_window_on_output_init_done(ctx_t * ctx) {
    // remember that output information is complete
    ctx->wl.window.flags |= WLM_WAYLAND_WINDOW_OUTPUTS_DONE;
    if (window_configure_ready(ctx)) {
        trigger_initial_configure(ctx);
    }
}

void wlm_wayland_window_on_output_changed(ctx_t * ctx, wlm_wayland_output_entry_t * entry) {
    if (!wlm_wayland_window_is_init_called(ctx)) return;
    if (ctx->wl.window.current_output != entry) return;

    if (use_output_scale(ctx) && ctx->wl.window.scale != entry->scale) {
        wlm_log(ctx, WLM_INFO, "using output scale = %d", entry->scale);

        ctx->wl.window.scale = entry->scale;
        ctx->wl.window.changed |= WLM_WAYLAND_WINDOW_SCALE_CHANGED;
    }
}

void wlm_wayland_window_on_output_removed(ctx_t * ctx, wlm_wayland_output_entry_t * entry) {
    if (!wlm_wayland_window_is_init_called(ctx)) return;
    if (ctx->wl.window.current_output != entry) return;

    // set current output to null because it would dangle otherwise
    ctx->wl.window.current_output = NULL;
}

void wlm_wayland_window_on_before_poll(ctx_t * ctx) {
    if (!wlm_wayland_window_is_init_done(ctx)) return;

    // check if things changed, emit events
    apply_surface_changes(ctx);
}

// --- initialization and cleanup ---

void wlm_wayland_window_zero(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "zeroing");

    ctx->wl.window.surface = NULL;
    ctx->wl.window.viewport = NULL;
    ctx->wl.window.fractional_scale = NULL;
    ctx->wl.window.libdecor_frame = NULL;

    ctx->wl.window.current_output = NULL;
    ctx->wl.window.transform = WL_OUTPUT_TRANSFORM_NORMAL;
    ctx->wl.window.width = 0;
    ctx->wl.window.height = 0;
    ctx->wl.window.buffer_width = 0;
    ctx->wl.window.buffer_height = 0;
    ctx->wl.window.scale = 1.0;

    ctx->wl.window.changed = WLM_WAYLAND_WINDOW_UNCHANGED;
    ctx->wl.window.flags = WLM_WAYLAND_WINDOW_INCOMPLETE;

    ctx->wl.window.init_called = false;
    ctx->wl.window.init_done = false;
}

void wlm_wayland_window_init(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "initializing");
    wlm_assert(wlm_wayland_registry_is_init_done(ctx), ctx, WLM_FATAL, "initial sync not complete");
    wlm_assert(!wlm_wayland_window_is_init_called(ctx), ctx, WLM_FATAL, "already initialized");
    ctx->wl.window.init_called = true;

    // listen for ping events
    xdg_wm_base_add_listener(ctx->wl.protocols.xdg_wm_base, &xdg_wm_base_listener, (void *)ctx);

    // create surface
    ctx->wl.window.surface = wl_compositor_create_surface(ctx->wl.protocols.compositor);
    wl_surface_add_listener(ctx->wl.window.surface, &surface_listener, (void *)ctx);

    // create viewport
    ctx->wl.window.viewport = wp_viewporter_get_viewport(ctx->wl.protocols.viewporter, ctx->wl.window.surface);

    // create fractional scale if supported
    if (ctx->wl.protocols.fractional_scale_manager != NULL) {
        ctx->wl.window.fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            ctx->wl.protocols.fractional_scale_manager, ctx->wl.window.surface
        );
        wp_fractional_scale_v1_add_listener(ctx->wl.window.fractional_scale, &fractional_scale_listener, (void *)ctx);
    }

    // create xdg surface
    ctx->wl.window.libdecor_frame = libdecor_decorate(ctx->wl.core.libdecor_context, ctx->wl.window.surface, &libdecor_frame_listener, (void *)ctx);

    // don't map frame or commit surface yet, wait for outputs
    ctx->wl.window.flags |= WLM_WAYLAND_WINDOW_TOPLEVEL_DONE;
    if (window_configure_ready(ctx)) {
        trigger_initial_configure(ctx);
    }
}

void wlm_wayland_window_cleanup(ctx_t * ctx) {
    wlm_log(ctx, WLM_TRACE, "cleaning up");

    if (ctx->wl.window.libdecor_frame != NULL) libdecor_frame_unref(ctx->wl.window.libdecor_frame);
    if (ctx->wl.window.fractional_scale != NULL) wp_fractional_scale_v1_destroy(ctx->wl.window.fractional_scale);
    if (ctx->wl.window.viewport != NULL) wp_viewport_destroy(ctx->wl.window.viewport);
    if (ctx->wl.window.surface != NULL) wl_surface_destroy(ctx->wl.window.surface);

    wlm_wayland_window_zero(ctx);
}

// --- public functions ---

bool wlm_wayland_window_is_init_called(ctx_t * ctx) {
    return ctx->wl.window.init_called;
}

bool wlm_wayland_window_is_init_done(ctx_t * ctx) {
    return ctx->wl.window.init_done;
}
