// „Zuletzt blockiert“: Liste der zuletzt blockierten Domains und Detailansicht
// mit Freigeben (Allowlist) bzw. Freigabe zurücknehmen.
#include "pihole.h"

// ================================================================ Liste

static Window *s_list_window;
static MenuLayer *s_menu;

#define ROW_H 48

static void ago_text(time_t t, char *buf, size_t len) {
  int d = (int)(time(NULL) - t);
  if (t == 0) buf[0] = '\0';
  else if (d < 60) strncpy(buf, tr(S_JUST_NOW), len);
  else if (d < 3600) snprintf(buf, len, tr(S_AGO_MIN), d / 60);
  else if (d < 86400) snprintf(buf, len, tr(S_AGO_H), d / 3600);
  else {
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%d.%m. %H:%M", tm);
  }
  buf[len - 1] = '\0';
}

// Kürzt von links („…ads.example.com“), damit die eigentliche Domain sichtbar bleibt
static void fit_left(const char *s, GFont font, int width, char *out, size_t len) {
  size_t n = strlen(s);
  for (size_t i = 0; i < n; i++) {
    if (i == 0) snprintf(out, len, "%s", s);
    else snprintf(out, len, "…%s", s + i);
    GSize sz = graphics_text_layout_get_content_size(out, font, GRect(0, 0, 1000, 30),
                                                     GTextOverflowModeTrailingEllipsis,
                                                     GTextAlignmentLeft);
    if (sz.w <= width) return;
  }
  snprintf(out, len, "%s", s);
}

static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  if (g_blocked_loading || g_blocked_error[0] || g_blocked_count == 0) return 1;
  return g_blocked_count;
}

static int16_t cell_height(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (g_blocked_loading || g_blocked_error[0] || g_blocked_count == 0) return 140;
  return ROW_H;
}

static int16_t header_height(MenuLayer *m, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  menu_cell_basic_header_draw(ctx, cell, tr(S_RECENT_TITLE));
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  if (g_blocked_loading || g_blocked_error[0] || g_blocked_count == 0) {
    const char *text = g_blocked_loading ? tr(S_LOADING_LIST)
                       : g_blocked_error[0] ? g_blocked_error : tr(S_NOTHING_BLOCKED);
    graphics_context_set_text_color(ctx, g_blocked_error[0] && !g_blocked_loading
                                             ? (hl ? GColorWhite : GColorDarkCandyAppleRed)
                                             : (hl ? GColorWhite : GColorDarkGray));
    graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(6, 4, b.size.w - 12, b.size.h - 8), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    return;
  }
  BlockedItem *it = &g_blocked[idx->row];
  GFont f1 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f2 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  char dom[DOMAIN_LEN + 4];
  fit_left(it->domain, f1, b.size.w - 12, dom, sizeof(dom));
  GColor c1 = hl ? GColorWhite : GColorBlack;
  GColor c2 = hl ? GColorWhite : GColorDarkGray;
  if ((it->flags & FLAG_ALLOWED) && !hl) c1 = GColorJaegerGreen;
  graphics_context_set_text_color(ctx, c1);
  graphics_draw_text(ctx, dom, f1, GRect(6, 2, b.size.w - 12, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  char ago[24], sub[96];
  ago_text(it->time, ago, sizeof(ago));
  if (it->flags & FLAG_ALLOWED) {
    snprintf(sub, sizeof(sub), "%s · %s", tr(S_ON_ALLOWLIST), it->client);
  } else if (it->count > 1) {
    char times[12];
    snprintf(times, sizeof(times), tr(S_TIMES), it->count);
    snprintf(sub, sizeof(sub), "%s · %s · %s", it->client, ago, times);
  } else {
    snprintf(sub, sizeof(sub), "%s · %s", it->client, ago);
  }
  graphics_context_set_text_color(ctx, c2);
  graphics_draw_text(ctx, sub, f2, GRect(6, 24, b.size.w - 12, 18), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void detail_push(int index);

static void select_cb(MenuLayer *m, MenuIndex *idx, void *data) {
  app_user_activity();
  if (g_blocked_loading) return;
  if (g_blocked_error[0] || g_blocked_count == 0) {
    // Fehler oder leer: SELECT lädt neu
    g_blocked_loading = true;
    g_blocked_error[0] = '\0';
    menu_layer_reload_data(s_menu);
    send_command(CMD_RECENT, 0, 0);
    return;
  }
  detail_push(idx->row);
}

static void list_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = num_rows,
    .get_cell_height = cell_height,
    .get_header_height = header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_cb,
  });
  menu_layer_set_highlight_colors(s_menu, GColorFolly, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void list_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(s_list_window);
  s_list_window = NULL;
}

void blocked_window_push(void) {
  if (s_list_window) return;
  g_blocked_count = 0;
  g_blocked_error[0] = '\0';
  g_blocked_loading = phone_ready();
  if (!phone_ready()) strncpy(g_blocked_error, tr(S_CONNECTING), sizeof(g_blocked_error) - 1);
  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers){ .load = list_load, .unload = list_unload });
  window_stack_push(s_list_window, true);
  if (phone_ready()) send_command(CMD_RECENT, 0, 0);
}

void blocked_window_update(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

// ================================================================ Detail

static Window *s_detail_window;
static ScrollLayer *s_scroll;
static Layer *s_content;
static ActionBarLayer *s_bar;
static GBitmap *s_icon_check, *s_icon_undo;
static int s_index = -1;
static bool s_busy = false;
static char s_note[160];        // Ergebnis / Fehler
static bool s_note_error = false;

typedef struct {
  const char *font;
  GColor color;
  int gap;  // Abstand davor
} Style;

// Zeichnet (draw = true) oder misst (draw = false) den Inhalt; gibt die Höhe zurück
static int render(GContext *ctx, int width, bool draw) {
  if (s_index < 0 || s_index >= g_blocked_count) return 0;
  BlockedItem *it = &g_blocked[s_index];
  char line[200], ago[24], clock[12];
  int y = 4;
  const int w = width - 12;

#define PUT(text, fontkey, color, gap)                                                            \
  do {                                                                                            \
    GFont _f = fonts_get_system_font(fontkey);                                                    \
    y += (gap);                                                                                   \
    GSize _s = graphics_text_layout_get_content_size((text), _f, GRect(0, 0, w, 2000),            \
                                                     GTextOverflowModeWordWrap, GTextAlignmentLeft); \
    if (draw) {                                                                                   \
      graphics_context_set_text_color(ctx, (color));                                              \
      graphics_draw_text(ctx, (text), _f, GRect(6, y, w, _s.h + 4), GTextOverflowModeWordWrap,    \
                         GTextAlignmentLeft, NULL);                                               \
    }                                                                                             \
    y += _s.h;                                                                                    \
  } while (0)

  PUT(it->domain, FONT_KEY_GOTHIC_24_BOLD, GColorBlack, 0);
  if (it->cname[0]) {
    PUT(tr(S_BLOCKED_AS), FONT_KEY_GOTHIC_14, GColorDarkGray, 4);
    PUT(it->cname, FONT_KEY_GOTHIC_18_BOLD, GColorBlack, 0);
  }
  PUT(tr(S_DEVICE), FONT_KEY_GOTHIC_14, GColorDarkGray, 6);
  PUT(it->client, FONT_KEY_GOTHIC_18_BOLD, GColorBlack, 0);

  ago_text(it->time, ago, sizeof(ago));
  struct tm *tm = localtime(&it->time);
  strftime(clock, sizeof(clock), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", tm);
  if (clock[0] == '0' && !clock_is_24h_style()) memmove(clock, clock + 1, strlen(clock));
  if (it->count > 1) {
    char times[12];
    snprintf(times, sizeof(times), tr(S_TIMES), it->count);
    snprintf(line, sizeof(line), "%s (%s) · %s", clock, ago, times);
  } else {
    snprintf(line, sizeof(line), "%s (%s)", clock, ago);
  }
  PUT(tr(S_TIME), FONT_KEY_GOTHIC_14, GColorDarkGray, 6);
  PUT(line, FONT_KEY_GOTHIC_18_BOLD, GColorBlack, 0);

  static const StrId reasons[] = { S_R_OTHER, S_R_GRAVITY, S_R_REGEX, S_R_DENYLIST,
                                   S_R_CNAME, S_R_UPSTREAM, S_R_SPECIAL };
  int r = (it->reason >= 0 && it->reason <= REASON_SPECIAL) ? it->reason : REASON_OTHER;
  PUT(tr(S_REASON), FONT_KEY_GOTHIC_14, GColorDarkGray, 6);
  PUT(tr(reasons[r]), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, 0);

  // Hinweis / Ergebnis
  const char *note;
  GColor nc = GColorDarkGray;
  if (s_busy) note = tr(S_ALLOWING);
  else if (s_note[0]) {
    note = s_note;
    nc = s_note_error ? GColorDarkCandyAppleRed : GColorJaegerGreen;
  } else if (it->flags & FLAG_NOT_ALLOWABLE) note = tr(S_NOT_ALLOWABLE);
  else if (it->flags & FLAG_ALLOWED) note = tr(S_UNALLOW_HINT);
  else note = tr(S_ALLOW_HINT);
  PUT(note, FONT_KEY_GOTHIC_18_BOLD, nc, 10);
#undef PUT
  return y + 8;
}

static void content_update(Layer *layer, GContext *ctx) {
  render(ctx, layer_get_bounds(layer).size.w, true);
}

static void scroll_to_end(void) {
  GRect fr = layer_get_frame(scroll_layer_get_layer(s_scroll));
  GSize cs = scroll_layer_get_content_size(s_scroll);
  if (cs.h > fr.size.h) scroll_layer_set_content_offset(s_scroll, GPoint(0, fr.size.h - cs.h), true);
}

static void relayout(void) {
  if (!s_detail_window) return;
  GRect fr = layer_get_frame(scroll_layer_get_layer(s_scroll));
  int h = render(NULL, fr.size.w, false);
  if (h < fr.size.h) h = fr.size.h;
  layer_set_frame(s_content, GRect(0, 0, fr.size.w, h));
  scroll_layer_set_content_size(s_scroll, GSize(fr.size.w, h));
  BlockedItem *it = &g_blocked[s_index];
  if (it->flags & FLAG_NOT_ALLOWABLE) action_bar_layer_clear_icon(s_bar, BUTTON_ID_SELECT);
  else action_bar_layer_set_icon(s_bar, BUTTON_ID_SELECT, (it->flags & FLAG_ALLOWED) ? s_icon_undo : s_icon_check);
  layer_mark_dirty(s_content);
}

static void d_up(ClickRecognizerRef r, void *ctx) { app_user_activity(); scroll_layer_scroll_up_click_handler(r, s_scroll); }
static void d_down(ClickRecognizerRef r, void *ctx) { app_user_activity(); scroll_layer_scroll_down_click_handler(r, s_scroll); }

static void d_select(ClickRecognizerRef r, void *ctx) {
  app_user_activity();
  if (s_busy || s_index < 0 || !phone_ready()) return;
  BlockedItem *it = &g_blocked[s_index];
  if (it->flags & FLAG_NOT_ALLOWABLE) return;
  s_busy = true;
  s_note[0] = '\0';
  send_command((it->flags & FLAG_ALLOWED) ? CMD_UNALLOW : CMD_ALLOW, 0, s_index);
  relayout();
}

static void d_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, d_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, d_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, d_select);
}

static void detail_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_icon_check = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHECK);
  s_icon_undo = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_UNDO);

  s_scroll = scroll_layer_create(GRect(0, 0, b.size.w - ACTION_BAR_WIDTH, b.size.h));
  scroll_layer_set_shadow_hidden(s_scroll, true);
  s_content = layer_create(GRect(0, 0, b.size.w - ACTION_BAR_WIDTH, b.size.h));
  layer_set_update_proc(s_content, content_update);
  scroll_layer_add_child(s_scroll, s_content);
  layer_add_child(root, scroll_layer_get_layer(s_scroll));

  s_bar = action_bar_layer_create();
  action_bar_layer_set_background_color(s_bar, GColorBlack);
  action_bar_layer_set_click_config_provider(s_bar, d_click_config);
  action_bar_layer_add_to_window(s_bar, window);
  relayout();
}

static void detail_unload(Window *window) {
  action_bar_layer_destroy(s_bar);
  layer_destroy(s_content);
  scroll_layer_destroy(s_scroll);
  gbitmap_destroy(s_icon_check);
  gbitmap_destroy(s_icon_undo);
  window_destroy(s_detail_window);
  s_detail_window = NULL;
  s_index = -1;
  s_busy = false;
}

static void detail_push(int index) {
  if (s_detail_window) return;
  s_index = index;
  s_busy = false;
  s_note[0] = '\0';
  s_note_error = false;
  s_detail_window = window_create();
  window_set_background_color(s_detail_window, GColorWhite);
  window_set_window_handlers(s_detail_window, (WindowHandlers){ .load = detail_load, .unload = detail_unload });
  window_stack_push(s_detail_window, true);
}

bool blocked_window_detail_busy(void) { return s_detail_window && s_busy; }

void blocked_window_item_changed(int index) {
  if (s_menu) menu_layer_reload_data(s_menu);
  if (s_detail_window && index == s_index) {
    s_busy = false;
    s_note_error = false;
    strncpy(s_note, tr((g_blocked[index].flags & FLAG_ALLOWED) ? S_ALLOWED_OK : S_UNALLOWED_OK),
            sizeof(s_note) - 1);
    relayout();
    scroll_to_end();
  }
}

void blocked_window_show_error(const char *text) {
  if (s_detail_window && s_busy) {
    s_busy = false;
    s_note_error = true;
    strncpy(s_note, text, sizeof(s_note) - 1);
    s_note[sizeof(s_note) - 1] = '\0';
    relayout();
    scroll_to_end();
    return;
  }
  strncpy(g_blocked_error, text, sizeof(g_blocked_error) - 1);
  g_blocked_error[sizeof(g_blocked_error) - 1] = '\0';
  if (s_menu) menu_layer_reload_data(s_menu);
}
