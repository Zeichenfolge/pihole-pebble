// Pi-hole für Pebble – Einstieg, Nachrichten vom/zum Handy, Speicher,
// Wecker fürs Pausenende und AppGlance (Anzeige in der App-Liste).
#include "pihole.h"

Status g_status;
Config g_config = { .lang = 0, .default_pause = 300, .quick_launch = true, .vibrate = true };
BlockedItem g_blocked[MAX_BLOCKED];
int g_blocked_count = 0;
bool g_blocked_loading = false;
char g_blocked_error[160];

// Persistente Schlüssel
#define P_CONFIG 1
#define P_STATUS 2
#define P_WAKEUP 3
#define P_VERSION 4
#define DATA_VERSION 1

#define WAKEUP_COOKIE 42

static bool s_js_ready = false;
static bool s_quick_pending = false;
static AppTimer *s_exit_timer = NULL;

// Ein Befehl, der gerade nicht gesendet werden konnte, wird kurz danach erneut versucht
static int s_retry_cmd = 0, s_retry_seconds = 0, s_retry_index = 0;
static AppTimer *s_retry_timer = NULL;

bool phone_ready(void) { return s_js_ready; }

// ---------------------------------------------------------------- Speicher

static void save_config(void) { persist_write_data(P_CONFIG, &g_config, sizeof(g_config)); }
static void save_status(void) { persist_write_data(P_STATUS, &g_status, sizeof(g_status)); }

static void load_persisted(void) {
  if (persist_read_int(P_VERSION) != DATA_VERSION) {
    // Neues Datenformat: alte Werte verwerfen
    persist_delete(P_CONFIG);
    persist_delete(P_STATUS);
    persist_write_int(P_VERSION, DATA_VERSION);
    return;
  }
  if (persist_exists(P_CONFIG)) persist_read_data(P_CONFIG, &g_config, sizeof(g_config));
  if (persist_exists(P_STATUS)) persist_read_data(P_STATUS, &g_status, sizeof(g_status));
}

// ---------------------------------------------------------------- Wecker fürs Pausenende

void schedule_pause_end(void) {
  if (persist_exists(P_WAKEUP)) {
    wakeup_cancel((WakeupId)persist_read_int(P_WAKEUP));
    persist_delete(P_WAKEUP);
  }
  time_t now = time(NULL);
  if (g_config.vibrate && g_status.loaded && g_status.blocking == BLOCKING_DISABLED &&
      g_status.pause_end > now + 5) {
    WakeupId id = wakeup_schedule(g_status.pause_end + 1, WAKEUP_COOKIE, false);
    if (id >= 0) persist_write_int(P_WAKEUP, (int)id);
  }
}

static void pause_ended_alert(void) {
  if (g_config.vibrate) {
    static const uint32_t segs[] = { 200, 120, 200 };
    VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
    vibes_enqueue_custom_pattern(pat);
  }
}

static void wakeup_handler(WakeupId id, int32_t cookie) {
  persist_delete(P_WAKEUP);
  pause_ended_alert();
  if (s_js_ready) send_command(CMD_REFRESH, 0, 0);
}

// ---------------------------------------------------------------- Senden

static void retry_timer_cb(void *ctx) {
  s_retry_timer = NULL;
  if (s_retry_cmd) {
    int c = s_retry_cmd;
    s_retry_cmd = 0;
    send_command(c, s_retry_seconds, s_retry_index);
  }
}

void send_command(int command, int seconds, int index) {
  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK || !iter) {
    s_retry_cmd = command; s_retry_seconds = seconds; s_retry_index = index;
    if (!s_retry_timer) s_retry_timer = app_timer_register(400, retry_timer_cb, NULL);
    return;
  }
  dict_write_int32(iter, MESSAGE_KEY_Command, command);
  dict_write_int32(iter, MESSAGE_KEY_Seconds, seconds);
  dict_write_int32(iter, MESSAGE_KEY_Index, index);
  app_message_outbox_send();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_Command);
  if (!t) return;
  s_retry_cmd = t->value->int32;
  Tuple *s = dict_find(iter, MESSAGE_KEY_Seconds);
  Tuple *i = dict_find(iter, MESSAGE_KEY_Index);
  s_retry_seconds = s ? s->value->int32 : 0;
  s_retry_index = i ? i->value->int32 : 0;
  if (!s_retry_timer) s_retry_timer = app_timer_register(800, retry_timer_cb, NULL);
}

// ---------------------------------------------------------------- Empfangen

static int get_int(DictionaryIterator *it, uint32_t key, int def) {
  Tuple *t = dict_find(it, key);
  return t ? (int)t->value->int32 : def;
}

static void copy_str(DictionaryIterator *it, uint32_t key, char *dst, size_t len) {
  Tuple *t = dict_find(it, key);
  if (t && t->type == TUPLE_CSTRING) {
    strncpy(dst, t->value->cstring, len - 1);
    dst[len - 1] = '\0';
  } else {
    dst[0] = '\0';
  }
}

static void handle_status(DictionaryIterator *it) {
  time_t now = time(NULL);
  g_status.loaded = true;
  g_status.blocking = get_int(it, MESSAGE_KEY_Blocking, BLOCKING_UNKNOWN);
  int timer = get_int(it, MESSAGE_KEY_Timer, -1);
  g_status.pause_end = timer > 0 ? now + timer : 0;
  g_status.percent_x10 = get_int(it, MESSAGE_KEY_PercentX10, 0);
  copy_str(it, MESSAGE_KEY_Total, g_status.total, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Blocked, g_status.blocked, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Percent, g_status.percent, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Clients, g_status.clients, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Gravity, g_status.gravity, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Updated, g_status.updated, VALUE_LEN);
  copy_str(it, MESSAGE_KEY_Host, g_status.host, sizeof(g_status.host));
  g_status.fetched_at = now;
  save_status();
  schedule_pause_end();
  status_window_set_busy(false);
  status_window_update();
}

static void handle_config(DictionaryIterator *it) {
  g_config.lang = get_int(it, MESSAGE_KEY_Lang, g_config.lang);
  g_config.default_pause = get_int(it, MESSAGE_KEY_Seconds, g_config.default_pause);
  g_config.quick_launch = get_int(it, MESSAGE_KEY_QuickLaunch, g_config.quick_launch) != 0;
  g_config.vibrate = get_int(it, MESSAGE_KEY_Vibrate, g_config.vibrate) != 0;
  save_config();
  if (get_int(it, MESSAGE_KEY_Flags, 0) & 1) {
    // anderer Server: alte Werte nicht mehr anzeigen
    memset(&g_status, 0, sizeof(g_status));
    save_status();
    schedule_pause_end();
  }
  bool first = !s_js_ready;
  s_js_ready = true;
  if (first && s_quick_pending) {
    s_quick_pending = false;
    status_window_set_busy(true);
    send_command(CMD_QUICK, g_config.default_pause, 0);
  } else {
    status_window_set_busy(true);
    send_command(CMD_REFRESH, 0, 0);
  }
  status_window_update();
  blocked_window_update();
}

static void handle_error(DictionaryIterator *it) {
  char text[160];
  copy_str(it, MESSAGE_KEY_Text, text, sizeof(text));
  if (g_blocked_loading || blocked_window_detail_busy()) {
    g_blocked_loading = false;
    blocked_window_show_error(text);
  } else {
    status_window_set_busy(false);
    status_window_show_error(text);
  }
}

static void handle_blocked_item(DictionaryIterator *it) {
  int i = get_int(it, MESSAGE_KEY_Index, -1);
  if (i < 0 || i >= MAX_BLOCKED) return;
  BlockedItem *b = &g_blocked[i];
  copy_str(it, MESSAGE_KEY_Domain, b->domain, DOMAIN_LEN);
  copy_str(it, MESSAGE_KEY_Cname, b->cname, DOMAIN_LEN);
  copy_str(it, MESSAGE_KEY_Client, b->client, CLIENT_LEN);
  b->time = (time_t)get_int(it, MESSAGE_KEY_Time, 0);
  b->count = get_int(it, MESSAGE_KEY_Count, 1);
  b->reason = get_int(it, MESSAGE_KEY_Reason, REASON_OTHER);
  b->flags = get_int(it, MESSAGE_KEY_Flags, 0);
  if (i + 1 > g_blocked_count) g_blocked_count = i + 1;
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  switch (get_int(it, MESSAGE_KEY_Type, 0)) {
    case MSG_CONFIG: handle_config(it); break;
    case MSG_STATUS: handle_status(it); break;
    case MSG_ERROR: handle_error(it); break;
    case MSG_BUSY: status_window_set_busy(true); break;
    case MSG_BLK_BEGIN:
      g_blocked_count = 0;
      g_blocked_error[0] = '\0';
      break;
    case MSG_BLK_ITEM: handle_blocked_item(it); break;
    case MSG_BLK_END:
      g_blocked_loading = false;
      blocked_window_update();
      break;
    case MSG_ALLOWED: {
      int i = get_int(it, MESSAGE_KEY_Index, -1);
      if (i >= 0 && i < g_blocked_count) {
        if (get_int(it, MESSAGE_KEY_Flags, 0) & FLAG_ALLOWED) g_blocked[i].flags |= FLAG_ALLOWED;
        else g_blocked[i].flags &= ~FLAG_ALLOWED;
        vibes_short_pulse();
        blocked_window_item_changed(i);
      }
      break;
    }
  }
}

// ---------------------------------------------------------------- AppGlance

static void format_clock(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, clock_is_24h_style() ? "%H:%M" : "%I:%M %p", tm);
  if (buf[0] == '0' && !clock_is_24h_style()) memmove(buf, buf + 1, strlen(buf));  // „2:47 PM“
}

static void glance_cb(AppGlanceReloadSession *session, size_t limit, void *ctx) {
  if (!g_status.loaded || limit < 1) return;
  static char a[80], b[80];
  time_t now = time(NULL);
  snprintf(b, sizeof(b), tr(S_GLANCE_ACTIVE), g_status.percent);
  if (g_status.blocking == BLOCKING_DISABLED) {
    if (g_status.pause_end > now) {
      char clock[12];
      format_clock(g_status.pause_end, clock, sizeof(clock));
      snprintf(a, sizeof(a), tr(S_GLANCE_PAUSED_UNTIL), clock);
      AppGlanceSlice s1 = { .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = a },
                            .expiration_time = g_status.pause_end };
      app_glance_add_slice(session, s1);
      if (limit >= 2) {
        AppGlanceSlice s2 = { .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = b },
                              .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION };
        app_glance_add_slice(session, s2);
      }
    } else if (g_status.pause_end == 0) {
      AppGlanceSlice s = { .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
                                       .subtitle_template_string = tr(S_GLANCE_PAUSED) },
                           .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION };
      app_glance_add_slice(session, s);
    } else {
      // Pause schon vorbei → Pi-hole blockiert wieder
      AppGlanceSlice s = { .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = b },
                           .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION };
      app_glance_add_slice(session, s);
    }
  } else if (g_status.blocking == BLOCKING_ENABLED) {
    AppGlanceSlice s = { .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = b },
                         .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION };
    app_glance_add_slice(session, s);
  }
}

// ---------------------------------------------------------------- Start / Ende

static void exit_timer_cb(void *ctx) {
  s_exit_timer = NULL;
  window_stack_pop_all(true);
}

void app_user_activity(void) {
  if (s_exit_timer) {
    app_timer_cancel(s_exit_timer);
    s_exit_timer = NULL;
  }
}

static void init(void) {
  load_persisted();
  AppLaunchReason reason = launch_reason();

  wakeup_service_subscribe(wakeup_handler);
  if (reason == APP_LAUNCH_WAKEUP) {
    WakeupId id = 0;
    int32_t cookie = 0;
    wakeup_get_launch_event(&id, &cookie);
    persist_delete(P_WAKEUP);
    pause_ended_alert();
    // Von selbst gestartet: nach 30 s wieder schließen, falls niemand eine Taste drückt
    s_exit_timer = app_timer_register(30000, exit_timer_cb, NULL);
  }
  s_quick_pending = (reason == APP_LAUNCH_QUICK_LAUNCH) && g_config.quick_launch;

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(1024, 256);

  status_window_push();
  if (s_quick_pending) status_window_set_busy(true);
}

static void deinit(void) {
  app_glance_reload(glance_cb, NULL);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
