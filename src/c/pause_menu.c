// Menü zum Pausieren bzw. Wiederaktivieren des Blockierens.
#include "pihole.h"

static Window *s_window;
static MenuLayer *s_menu;
static bool s_paused;  // Zustand beim Öffnen

static const int s_durations[] = { 30, 300, 900, 3600, -1 };
static const StrId s_labels[] = { S_30S, S_5M, S_15M, S_1H, S_FOREVER };
#define NUM_DURATIONS 5

static uint16_t num_sections(MenuLayer *m, void *ctx) { return s_paused ? 2 : 1; }

static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  return (s_paused && section == 0) ? 1 : NUM_DURATIONS;
}

static int16_t header_height(MenuLayer *m, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  StrId id = s_paused ? (section == 0 ? S_MENU_RESUME_TITLE : S_PAUSE_FOR) : S_MENU_PAUSE_TITLE;
  menu_cell_basic_header_draw(ctx, cell, tr(id));
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  if (s_paused && idx->section == 0) {
    menu_cell_basic_draw(ctx, cell, tr(S_RESUME_NOW), NULL, NULL);
    return;
  }
  const char *sub = NULL;
  if (s_durations[idx->row] == g_config.default_pause) sub = g_config.lang == 1 ? "Default (hold SELECT)" : "Standard (SELECT lang)";
  menu_cell_basic_draw(ctx, cell, tr(s_labels[idx->row]), sub, NULL);
}

static void select_cb(MenuLayer *m, MenuIndex *idx, void *data) {
  app_user_activity();
  int seconds;
  if (s_paused && idx->section == 0) seconds = 0;
  else seconds = s_durations[idx->row];
  status_window_set_busy(true);
  send_command(CMD_SET_BLOCKING, seconds, 0);
  window_stack_remove(s_window, true);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = num_sections,
    .get_num_rows = num_rows,
    .get_header_height = header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_cb,
  });
  menu_layer_set_highlight_colors(s_menu, s_paused ? GColorJaegerGreen : GColorChromeYellow,
                                  s_paused ? GColorWhite : GColorBlack);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  // Standarddauer vorauswählen
  if (!s_paused) {
    for (int i = 0; i < NUM_DURATIONS; i++) {
      if (s_durations[i] == g_config.default_pause) {
        menu_layer_set_selected_index(s_menu, MenuIndex(0, i), MenuRowAlignCenter, false);
      }
    }
  }
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  window_destroy(s_window);
  s_window = NULL;
}

void pause_menu_push(void) {
  if (s_window) return;
  time_t now = time(NULL);
  s_paused = g_status.blocking == BLOCKING_DISABLED &&
             (g_status.pause_end == 0 || g_status.pause_end > now);
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}

void pause_menu_close(void) {
  if (s_window) window_stack_remove(s_window, true);
}
