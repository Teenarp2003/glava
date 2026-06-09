
/* Native Wayland window creation and EGL context backend.

   Renders GLava as a wlr-layer-shell surface (desktop background by default),
   suitable for wlroots-based compositors such as Hyprland, Sway, river and
   Wayfire. The OpenGL renderer and shaders are shared with the other backends;
   only window/context creation differs.

   Unlike the X11/GLX backend this file intentionally does NOT define
   GLAVA_RDX11, so it carries no Xlib dependency and the gl_wcb X11 hooks are
   left as NULL placeholders. */

#ifdef GLAVA_WAYLAND

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>

#include "glad.h"
#include "render.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

extern struct gl_wcb wcb_wayland;

/* Shared display/registry/EGL state (single connection per process) */
static struct wl_display*           display      = NULL;
static struct wl_registry*          registry     = NULL;
static struct wl_compositor*        compositor   = NULL;
static struct zwlr_layer_shell_v1*  layer_shell  = NULL;
static struct wl_output*            output       = NULL;

static EGLDisplay egl_display = EGL_NO_DISPLAY;

static int  swap_interval = 1;
static bool transparent   = false;
/* These flags have no meaningful equivalent for layer-shell surfaces; they are
   tracked only so the corresponding gl_wcb setters remain valid no-ops. */
static bool floating, decorated, focused, maximized;

struct wlwin {
    struct wl_surface*             surface;
    struct zwlr_layer_surface_v1*  layer_surface;
    struct wl_egl_window*          egl_window;
    EGLSurface                     egl_surface;
    EGLContext                     context;
    EGLConfig                      config;
    int    width, height;   /* current framebuffer size (from configure) */
    int    req_x, req_y;    /* requested margin offset for positioned surfaces */
    bool   configured;
    bool   should_close;
    bool   clickthrough;
    char   override_state;
};

static bool offscreen(void) { return false; }

/* --- registry --------------------------------------------------------- */

static void registry_global(void* data, struct wl_registry* reg, uint32_t name,
                            const char* interface, uint32_t version) {
    (void) data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
                                      version < 4 ? version : 4);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
                                       version < 4 ? version : 4);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        /* Bind the first advertised output; the compositor decides placement
           when NULL is passed, but binding one keeps multi-monitor sane. */
        if (!output)
            output = wl_registry_bind(reg, name, &wl_output_interface,
                                      version < 3 ? version : 3);
    }
}

static void registry_global_remove(void* data, struct wl_registry* reg, uint32_t name) {
    (void) data; (void) reg; (void) name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove
};

/* --- layer surface ---------------------------------------------------- */

static void layer_surface_configure(void* data, struct zwlr_layer_surface_v1* s,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct wlwin* win = data;
    zwlr_layer_surface_v1_ack_configure(s, serial);
    if (w > 0 && h > 0) {
        win->width  = (int) w;
        win->height = (int) h;
        if (win->egl_window)
            wl_egl_window_resize(win->egl_window, (int) w, (int) h, 0, 0);
    }
    win->configured = true;
}

static void layer_surface_closed(void* data, struct zwlr_layer_surface_v1* s) {
    (void) s;
    struct wlwin* win = data;
    win->should_close = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed    = layer_surface_closed
};

/* --- init ------------------------------------------------------------- */

static void init(void) {
    floating = decorated = focused = maximized = false;
    transparent = false;

    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "wl_display_connect(): could not connect to a Wayland compositor\n"
                "(is WAYLAND_DISPLAY set?)\n");
        glava_abort();
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    /* Two roundtrips: first to receive globals, second to settle output info. */
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    if (!compositor) {
        fprintf(stderr, "Wayland compositor does not expose wl_compositor\n");
        glava_abort();
    }
    if (!layer_shell) {
        fprintf(stderr,
                "Wayland compositor does not support the wlr-layer-shell protocol\n"
                "(zwlr_layer_shell_v1). This backend requires a wlroots-based\n"
                "compositor such as Hyprland, Sway, river or Wayfire.\n");
        glava_abort();
    }

    egl_display = eglGetDisplay((EGLNativeDisplayType) display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay(): failed to obtain an EGL display\n");
        glava_abort();
    }

    EGLint egl_major, egl_minor;
    if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
        fprintf(stderr, "eglInitialize(): failed (0x%x)\n", eglGetError());
        glava_abort();
    }

    /* Request a desktop OpenGL context so the existing `#version 330 core`
       shaders run unmodified (as opposed to GLES). */
    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "eglBindAPI(EGL_OPENGL_API): failed; this driver does not\n"
                "expose desktop OpenGL through EGL.\n");
        glava_abort();
    }
}

/* --- window creation -------------------------------------------------- */

static enum zwlr_layer_shell_v1_layer layer_from_type(const char* type) {
    /* Map the X11-style window type/keyword to a layer-shell layer. The default
       (including the stock "normal" type) is the desktop background, which is
       what this visualizer is intended for. */
    if (!type) return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    /* skip a leading override-redirect marker, if present */
    const char* t = (type[0] == '!') ? type + 1 : type;
    if      (strstr(t, "overlay")) return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    else if (strstr(t, "top") || strstr(t, "dock")) return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    else if (strstr(t, "bottom")) return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
}

static void set_input_region_empty(struct wlwin* w) {
    struct wl_region* region = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(w->surface, region);
    wl_region_destroy(region);
}

static void* create_and_bind(const char* name, const char* class,
                             const char* type, const char** states,
                             size_t states_sz,
                             int d, int h,
                             int x, int y,
                             int version_major, int version_minor,
                             bool clickthrough, bool off) {
    (void) name; (void) states; (void) states_sz; (void) off;

    struct wlwin* w = malloc(sizeof(struct wlwin));
    *w = (struct wlwin) {
        .width          = d > 0 ? d : 0,
        .height         = h > 0 ? h : 0,
        .req_x          = x,
        .req_y          = y,
        .configured     = false,
        .should_close   = false,
        .override_state = (type && type[0] == '!') ? type[1] : '\0'
    };

    enum zwlr_layer_shell_v1_layer layer = layer_from_type(type);
    /* A background layer should never steal pointer input; honor the explicit
       clickthrough flag for the higher (top/overlay) layers. */
    w->clickthrough = clickthrough || layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;

    /* Choose an EGL framebuffer config (alpha only when transparency requested). */
    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      transparent ? 8 : 0,
        EGL_NONE
    };
    EGLint num_config = 0;
    if (!eglChooseConfig(egl_display, cfg_attrs, &w->config, 1, &num_config) || num_config < 1) {
        fprintf(stderr, "eglChooseConfig(): no suitable EGL config (0x%x)\n", eglGetError());
        glava_abort();
    }

    /* Try a compatibility profile first (matches GLX behavior and keeps any
       legacy GL usage working), then fall back to core. */
    const EGLint ctx_attrs_compat[] = {
        EGL_CONTEXT_MAJOR_VERSION, version_major,
        EGL_CONTEXT_MINOR_VERSION, version_minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
        EGL_NONE
    };
    const EGLint ctx_attrs_core[] = {
        EGL_CONTEXT_MAJOR_VERSION, version_major,
        EGL_CONTEXT_MINOR_VERSION, version_minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    w->context = eglCreateContext(egl_display, w->config, EGL_NO_CONTEXT, ctx_attrs_compat);
    if (w->context == EGL_NO_CONTEXT)
        w->context = eglCreateContext(egl_display, w->config, EGL_NO_CONTEXT, ctx_attrs_core);
    if (w->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext(): failed to create an OpenGL %d.%d context (0x%x)\n",
                version_major, version_minor, eglGetError());
        glava_abort();
    }

    w->surface = wl_compositor_create_surface(compositor);
    if (!w->surface) {
        fprintf(stderr, "wl_compositor_create_surface(): failed\n");
        glava_abort();
    }

    w->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, w->surface, output, layer, class ? class : "glava");
    if (!w->layer_surface) {
        fprintf(stderr, "zwlr_layer_shell_v1_get_layer_surface(): failed\n");
        glava_abort();
    }
    zwlr_layer_surface_v1_add_listener(w->layer_surface, &layer_surface_listener, w);

    /* A desktop-background visualizer should cover the whole monitor, so the
       background layer always fills its output and ignores `setgeometry` (the
       default config requests 800x600, which would otherwise leave it as a
       small region in the corner). Other layers honor an explicit size. */
    bool fill = layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
                || w->width <= 0 || w->height <= 0;
    if (fill) {
        zwlr_layer_surface_v1_set_anchor(w->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_size(w->layer_surface, 0, 0);
        /* Let the compositor's configure event provide the real output size. */
        w->width  = 0;
        w->height = 0;
    } else {
        /* Positioned, fixed-size surface anchored to the top-left corner. */
        zwlr_layer_surface_v1_set_size(w->layer_surface, w->width, w->height);
        zwlr_layer_surface_v1_set_anchor(w->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_margin(w->layer_surface, y, 0, 0, x);
    }
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        w->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    /* Stretch beneath panels/bars (background/wallpaper behavior). */
    zwlr_layer_surface_v1_set_exclusive_zone(w->layer_surface, -1);

    if (w->clickthrough)
        set_input_region_empty(w);

    /* Initial commit without a buffer; wait for the first configure. */
    wl_surface_commit(w->surface);
    wl_display_roundtrip(display);

    int win_w = w->width  > 0 ? w->width  : 1;
    int win_h = w->height > 0 ? w->height : 1;
    w->egl_window = wl_egl_window_create(w->surface, win_w, win_h);
    if (!w->egl_window) {
        fprintf(stderr, "wl_egl_window_create(): failed\n");
        glava_abort();
    }

    w->egl_surface = eglCreateWindowSurface(egl_display, w->config,
                                            (EGLNativeWindowType) w->egl_window, NULL);
    if (w->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface(): failed (0x%x)\n", eglGetError());
        glava_abort();
    }

    if (!eglMakeCurrent(egl_display, w->egl_surface, w->egl_surface, w->context)) {
        fprintf(stderr, "eglMakeCurrent(): failed (0x%x)\n", eglGetError());
        glava_abort();
    }

    if (!glad_instantiated) {
        if (!gladLoadGLLoader((GLADloadproc) eglGetProcAddress)) {
            fprintf(stderr, "gladLoadGLLoader(eglGetProcAddress): failed to load OpenGL\n");
            glava_abort();
        }
        glad_instantiated = true;
    }

    eglSwapInterval(egl_display, swap_interval);

    return w;
}

/* --- runtime hooks ---------------------------------------------------- */

static void swap_buffers(struct wlwin* w) {
    eglSwapBuffers(egl_display, w->egl_surface);
    /* Pump any pending events (configure/closed) without blocking. */
    wl_display_dispatch_pending(display);
    wl_display_flush(display);
    if (wl_display_get_error(display) != 0)
        w->should_close = true;
}

static bool should_close (struct wlwin* w) { return w->should_close; }
static bool should_render(struct wlwin* w) { return !w->should_close; }
static bool bg_changed   (struct wlwin* w) { (void) w; return false; }

static void get_fbsize(struct wlwin* w, int* d, int* h) {
    *d = w->width  > 0 ? w->width  : 1;
    *h = w->height > 0 ? w->height : 1;
}

static void get_pos(struct wlwin* w, int* x, int* y) {
    *x = w->req_x;
    *y = w->req_y;
}

static void set_geometry(struct wlwin* w, int x, int y, int d, int h) {
    w->req_x = x;
    w->req_y = y;
    if (d > 0 && h > 0) {
        zwlr_layer_surface_v1_set_size(w->layer_surface, d, h);
        zwlr_layer_surface_v1_set_anchor(w->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_margin(w->layer_surface, y, 0, 0, x);
        if (w->egl_window)
            wl_egl_window_resize(w->egl_window, d, h, 0, 0);
        w->width  = d;
        w->height = h;
    }
    wl_surface_commit(w->surface);
}

static void set_visible(struct wlwin* w, bool visible) {
    if (!visible) {
        wl_surface_attach(w->surface, NULL, 0, 0);
        wl_surface_commit(w->surface);
    }
    /* Becoming visible happens implicitly on the next buffer swap. */
}

static void raise(struct wlwin* w) { (void) w; /* layer ordering is fixed by the compositor */ }

static double get_timert(void) {
    struct timespec tv;
    if (clock_gettime(CLOCK_MONOTONIC, &tv))
        fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC, ...): %s\n", strerror(errno));
    return (double) tv.tv_sec + ((double) tv.tv_nsec / 1000000000.0);
}

/* time is stored as an offset baked into req fields is overkill; keep a per-window base */
static double base_time = 0.0;
static double get_time(struct wlwin* w) { (void) w; return get_timert() - base_time; }
static void   set_time(struct wlwin* w, double time) { (void) w; base_time = get_timert() - time; }

static void set_swap(int interval) {
    swap_interval = interval;
    if (egl_display != EGL_NO_DISPLAY)
        eglSwapInterval(egl_display, interval);
}

static void set_transparent(bool t) { transparent = t; }
static void set_floating   (bool v) { floating  = v; }
static void set_decorated  (bool v) { decorated = v; }
static void set_focused    (bool v) { focused   = v; }
static void set_maximized  (bool v) { maximized = v; }

static void destroy(struct wlwin* w) {
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (w->egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, w->egl_surface);
    if (w->context     != EGL_NO_CONTEXT) eglDestroyContext(egl_display, w->context);
    if (w->egl_window)    wl_egl_window_destroy(w->egl_window);
    if (w->layer_surface) zwlr_layer_surface_v1_destroy(w->layer_surface);
    if (w->surface)       wl_surface_destroy(w->surface);
    free(w);
}

static void terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }
    if (display) {
        wl_display_disconnect(display);
        display = NULL;
    }
}

static const char* get_environment(void) {
    /* Used to resolve env_*.glsl auto-desktop presets; there are no Wayland
       specific presets shipped, so this falls back to env_default.glsl. */
    return getenv("XDG_CURRENT_DESKTOP");
}

/* Manual struct initialization: this backend does not define GLAVA_RDX11, so
   the X11 hooks are left as NULL placeholders (see render.h). */
struct gl_wcb wcb_wayland = {
    .name = "wayland",
    WCB_FUNC(offscreen),
    WCB_FUNC(init),
    WCB_FUNC(create_and_bind),
    WCB_FUNC(should_close),
    WCB_FUNC(should_render),
    WCB_FUNC(bg_changed),
    WCB_FUNC(swap_buffers),
    WCB_FUNC(raise),
    WCB_FUNC(destroy),
    WCB_FUNC(terminate),
    WCB_FUNC(set_swap),
    WCB_FUNC(get_pos),
    WCB_FUNC(get_fbsize),
    WCB_FUNC(set_geometry),
    WCB_FUNC(set_floating),
    WCB_FUNC(set_decorated),
    WCB_FUNC(set_focused),
    WCB_FUNC(set_maximized),
    WCB_FUNC(set_transparent),
    WCB_FUNC(set_time),
    WCB_FUNC(get_time),
    WCB_FUNC(set_visible),
    WCB_FUNC(get_environment),
    ._X11_DISPLAY_PLACEHOLDER = NULL,
    ._X11_WINDOW_PLACEHOLDER  = NULL
};

#endif /* GLAVA_WAYLAND */
