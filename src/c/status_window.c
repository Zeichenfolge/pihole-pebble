// Startbildschirm: farbiger Statusbalken (aktiv / pausiert mit Countdown),
// darunter die Zahlen der letzten 24 Stunden. Rechts die Aktionsleiste:
// UP = neu laden, SELECT = pausieren/aktivieren, DOWN = zuletzt blockiert.
#include "pihole.h"

static Window *s_window;
static Layer *s_canvas;
static ActionBarLayer *s_action_bar;
static GBitmap *s_icon_refresh, *s_icon_pause, *s_icon_play, *s_icon_list;
static bool s_busy = false;
static char s_error[160];
static bool s_refresh_after_end_sent = false;

#define BAND_H 66

// Tatsächlicher Zustand: ist eine Pause abgelaufen, blockiert Pi-hole von selbst wieder
static int effective_blocking(void) {
  if (!g_status.loaded) return BLOCKING_UNKNOWN;
  if (g_status.blocking == BLOCKING_DISABLED && g_status.pause_end != 0 &&
      time(NULL) >= g_status.pause_end) {
    return BLOCKING_ENABLED;
  }
  return g_status.blocking;
}

static void format_duration(int secs, char *buf, size_t len) {
  if (secs < 0) secs = 0;
  int h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
  if (h > 0) snprintf(buf, len, "%d:%02d:%02d", h, m, s);
  else snprintf(buf, len, "%d:%02d", m, s);
}

static void format_clock(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, clock_is_24h_style() ? "%H:%M" : "%I:%M %p", tm);
  if (buf[0] == '0' && !clock_is_24h_style()) memmove(buf, buf + 1, strlen(buf));  // „2:47 PM“
}

static void draw_text(GContext *ctx, const char *text, const char *font, GRect r,
                      GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, fonts_get_system_font(font), r, GTextOverflowModeTrailingEllipsis,
                     align, NULL);
}

static void draw_row(GContext *ctx, int y, int w, const char *label, const char *value) {
  draw_text(ctx, label, FONT_KEY_GOTHIC_18, GRect(6, y, w - 12, 22), GTextAlignmentLeft, GColorDarkGray);
  draw_text(ctx, value, FONT_KEY_GOTHIC_18_BOLD, GRect(6, y, w - 12, 22), GTextAlignmentRight, GColorBlack);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w;
  time_t now = time(NULL);
  int state = effective_blocking();

  // --- Statusbalken
  GColor bg = GColorDarkGray, fg = GColorWhite;
  const char *title = g_status.loaded ? tr(S_UNKNOWN) : "Pi-hole";
  char line2[48] = "";
  if (state == BLOCKING_ENABLED) {
    bg = GColorJaegerGreen;
    title = tr(S_ACTIVE);
    strncpy(line2, g_status.host, sizeof(line2) - 1);
  } else if (state == BLOCKING_DISABLED) {
    title = tr(S_PAUSED);
    if (g_status.pause_end != 0) {
      bg = GColorChromeYellow;
      fg = GColorBlack;
      char d[16];
      format_duration((int)(g_status.pause_end - now), d, sizeof(d));
      snprintf(line2, sizeof(line2), tr(S_LEFT), d);
    } else {
      bg = GColorRed;
      strncpy(line2, tr(S_UNLIMITED), sizeof(line2) - 1);
    }
  } else if (state == BLOCKING_FAILED) {
    bg = GColorDarkCandyAppleRed;
    title = tr(S_FAILED);
  }
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(0, 0, w, BAND_H), 0, GCornerNone);
  draw_text(ctx, title, FONT_KEY_GOTHIC_28_BOLD, GRect(6, 0, w - 12, 34), GTextAlignmentLeft, fg);
  draw_text(ctx, line2, FONT_KEY_GOTHIC_18, GRect(6, 36, w - 12, 24), GTextAlignmentLeft, fg);

  // --- Inhalt
  int y = BAND_H + 4;
  if (s_error[0]) {
    draw_text(ctx, s_error, FONT_KEY_GOTHIC_18_BOLD, GRect(6, y, w - 12, b.size.h - y - 22),
              GTextAlignmentLeft, GColorDarkCandyAppleRed);
  } else if (!g_status.loaded) {
    draw_text(ctx, tr(phone_ready() ? S_LOADING : S_CONNECTING), FONT_KEY_GOTHIC_18,
              GRect(6, y, w - 12, 60), GTextAlignmentLeft, GColorDarkGray);
  } else {
    draw_row(ctx, y, w, tr(S_QUERIES), g_status.total); y += 23;
    draw_row(ctx, y, w, tr(S_BLOCKED), g_status.blocked); y += 25;
    // Anteil blockiert als Balken
    int bar_w = w - 12 - 62;
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(6, y + 2, bar_w, 10), 3, GCornersAll);
    int pct = g_status.percent_x10;
    if (pct < 0) pct = 0;
    if (pct > 1000) pct = 1000;
    int fill = bar_w * pct / 1000;
    if (pct > 0 && fill < 3) fill = 3;
    graphics_context_set_fill_color(ctx, GColorFolly);
    if (fill > 0) graphics_fill_rect(ctx, GRect(6, y + 2, fill, 10), 3, GCornersAll);
    draw_text(ctx, g_status.percent, FONT_KEY_GOTHIC_14_BOLD, GRect(w - 6 - 58, y - 3, 58, 18),
              GTextAlignmentRight, GColorBlack);
    y += 17;
    draw_row(ctx, y, w, tr(S_CLIENTS), g_status.clients); y += 23;
    draw_row(ctx, y, w, tr(S_BLOCKLIST), g_status.gravity); y += 23;
    draw_row(ctx, y, w, tr(S_LISTS_UPDATED), g_status.updated); y += 23;
  }

  // --- Fußzeile
  char foot[48] = "";
  if (s_busy) {
    strncpy(foot, tr(S_WORKING), sizeof(foot) - 1);
  } else if (g_status.loaded && g_status.fetched_at) {
    char clock[12];
    format_clock(g_status.fetched_at, clock, sizeof(clock));
    snprintf(foot, sizeof(foot), tr(S_AS_OF), clock);
  }
  draw_text(ctx, foot, FONT_KEY_GOTHIC_14, GRect(6, b.size.h - 20, w - 12, 18), GTextAlignmentLeft,
            GColorDarkGray);
}

static void update_action_bar(void) {
  if (!s_action_bar) return;
  int state = effective_blocking();
  action_bar_layer_set_icon_animated(s_action_bar, BUTTON_ID_SELECT,
                                     state == BLOCKING_DISABLED ? s_icon_play : s_icon_pause, true);
}

void status_window_update(void) {
  if (!s_window) return;
  if (g_status.loaded) s_error[0] = '\0';
  s_refresh_after_end_sent = false;
  update_action_bar();
  layer_mark_dirty(s_canvas);
}

void status_window_show_error(const char *text) {
  strncpy(s_error, text, sizeof(s_error) - 1);
  s_error[sizeof(s_error) - 1] = '\0';
  if (s_canvas) layer_mark_dirty(s_canvas);
}

void status_window_set_busy(bool busy) {
  s_busy = busy;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void request_refresh(void) {
  if (!phone_ready()) return;
  s_error[0] = '\0';
  status_window_set_busy(true);
  send_command(CMD_REFRESH, 0, 0);
}

// ---------------------------------------------------------------- Tasten

static void up_click(ClickRecognizerRef r, void *ctx) {
  app_user_activity();
  request_refresh();
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  app_user_activity();
  if (!phone_ready() || !g_status.loaded) return;
  pause_menu_push();
}

static void select_long_click(ClickRecognizerRef r, void *ctx) {
  // Schnell umschalten: aktiv → Standardpause, pausiert → wieder aktivieren
  app_user_activity();
  if (!phone_ready()) return;
  vibes_short_pulse();
  s_error[0] = '\0';
  status_window_set_busy(true);
  send_command(CMD_QUICK, g_config.default_pause, 0);
}

static void down_click(ClickRecognizerRef r, void *ctx) {
  app_user_activity();
  blocked_window_push();
}

static void back_click(ClickRecognizerRef r, void *ctx) {
  window_stack_pop(true);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---------------------------------------------------------------- Touch (Pebble Time 2)

#if defined(PBL_TOUCH)
static GPoint s_touch_start;
static bool s_touch_down = false;

static void touch_handler(const TouchEvent *event, void *context) {
  if (event->non_navigational) return;
  if (event->type == TouchEvent_Touchdown) {
    s_touch_start = GPoint(event->x, event->y);
    s_touch_down = true;
    return;
  }
  if (event->type != TouchEvent_Liftoff || !s_touch_down) return;
  s_touch_down = false;
  if (abs(event->x - s_touch_start.x) > 12 || abs(event->y - s_touch_start.y) > 12) return;  // Wischen
  if (window_stack_get_top_window() != s_window) return;
  GRect frame = layer_get_frame(window_get_root_layer(s_window));
  int bar_x = frame.size.w - ACTION_BAR_WIDTH;
  if (event->x >= bar_x) {
    int third = frame.size.h / 3;
    if (event->y < third) up_click(NULL, NULL);
    else if (event->y < 2 * third) select_click(NULL, NULL);
    else down_click(NULL, NULL);
  } else if (event->y < BAND_H) {
    select_click(NULL, NULL);
  } else {
    down_click(NULL, NULL);
  }
}
#endif

// ---------------------------------------------------------------- Zeit

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  if (!g_status.loaded || g_status.blocking != BLOCKING_DISABLED || g_status.pause_end == 0) return;
  time_t now = time(NULL);
  layer_mark_dirty(s_canvas);
  if (now >= g_status.pause_end) {
    update_action_bar();
    // Pause vorbei: kurz warten, dann den echten Zustand holen
    if (!s_refresh_after_end_sent && now >= g_status.pause_end + 2 && phone_ready()) {
      s_refresh_after_end_sent = true;
      send_command(CMD_REFRESH, 0, 0);
    }
  }
}

// ---------------------------------------------------------------- Fenster

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_canvas = layer_create(GRect(0, 0, b.size.w - ACTION_BAR_WIDTH, b.size.h));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_icon_refresh = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_REFRESH);
  s_icon_pause = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PAUSE);
  s_icon_play = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PLAY);
  s_icon_list = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LIST);

  s_action_bar = action_bar_layer_create();
  action_bar_layer_set_background_color(s_action_bar, GColorBlack);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_refresh);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_pause);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_icon_list);
  action_bar_layer_set_click_config_provider(s_action_bar, click_config);
  action_bar_layer_add_to_window(s_action_bar, window);
  update_action_bar();

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void window_appear(Window *window) {
#if defined(PBL_TOUCH)
  touch_service_subscribe(touch_handler, NULL);
#endif
  layer_mark_dirty(s_canvas);
}

static void window_disappear(Window *window) {
#if defined(PBL_TOUCH)
  touch_service_unsubscribe();
#endif
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  action_bar_layer_destroy(s_action_bar);
  s_action_bar = NULL;
  layer_destroy(s_canvas);
  s_canvas = NULL;
  gbitmap_destroy(s_icon_refresh);
  gbitmap_destroy(s_icon_pause);
  gbitmap_destroy(s_icon_play);
  gbitmap_destroy(s_icon_list);
  window_destroy(s_window);
  s_window = NULL;
}

void status_window_push(void) {
#if defined(PBL_TOUCH)
  app_touch_navigation_enable(true);
#endif
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
