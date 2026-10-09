// Gemeinsame Definitionen für die Pi-hole-App.
// Die Zahlen in MSG_* und CMD_* müssen zu src/pkjs/index.js passen.
#pragma once
#include <pebble.h>

// Nachrichten vom Handy an die Uhr (Schlüssel "Type")
enum {
  MSG_STATUS = 1,     // Blocking, Timer, Zahlen als fertige Texte
  MSG_ERROR = 2,      // Text
  MSG_BLK_BEGIN = 3,  // Num
  MSG_BLK_ITEM = 4,   // Index, Domain, Cname, Client, Time, Count, Reason, Flags
  MSG_BLK_END = 5,
  MSG_ALLOWED = 6,    // Index, Flags (Bit 0 = jetzt freigegeben)
  MSG_CONFIG = 7,     // Lang, Seconds (Standardpause), QuickLaunch, Vibrate, Flags (1 = anderer Server)
  MSG_BUSY = 8        // Handy arbeitet (z. B. Anmeldung)
};

// Befehle von der Uhr ans Handy (Schlüssel "Command")
enum {
  CMD_REFRESH = 1,
  CMD_SET_BLOCKING = 2,  // Seconds: 0 = wieder aktivieren, >0 = so lange pausieren, -1 = unbegrenzt
  CMD_RECENT = 3,
  CMD_ALLOW = 4,         // Index
  CMD_UNALLOW = 5,       // Index
  CMD_QUICK = 6          // Schnellstart: aktiv → Standardpause, pausiert → aktivieren
};

// Blockierstatus
enum {
  BLOCKING_UNKNOWN = 0,
  BLOCKING_ENABLED = 1,
  BLOCKING_DISABLED = 2,
  BLOCKING_FAILED = 3
};

// Warum eine Domain blockiert wurde
enum {
  REASON_OTHER = 0,
  REASON_GRAVITY = 1,
  REASON_REGEX = 2,
  REASON_DENYLIST = 3,
  REASON_CNAME = 4,
  REASON_UPSTREAM = 5,
  REASON_SPECIAL = 6
};

// Bits in Flags eines Eintrags
#define FLAG_ALLOWED 1      // steht auf der Allowlist
#define FLAG_NOT_ALLOWABLE 2 // Freigeben hilft nicht (Upstream/Spezialdomain)

#define MAX_BLOCKED 30
#define DOMAIN_LEN 100
#define CLIENT_LEN 40
#define VALUE_LEN 24

typedef struct {
  char domain[DOMAIN_LEN];
  char cname[DOMAIN_LEN];
  char client[CLIENT_LEN];
  time_t time;
  int count;
  int reason;
  int flags;
} BlockedItem;

typedef struct {
  bool loaded;
  int blocking;
  time_t pause_end;       // 0 = keine Zeitbegrenzung
  int percent_x10;
  char total[VALUE_LEN];
  char blocked[VALUE_LEN];
  char percent[VALUE_LEN];
  char clients[VALUE_LEN];
  char gravity[VALUE_LEN];
  char updated[VALUE_LEN];
  char host[40];
  time_t fetched_at;
} Status;

typedef struct {
  int lang;            // 0 = Deutsch, 1 = Englisch
  int default_pause;   // Sekunden
  bool quick_launch;   // Schnellstart schaltet sofort um
  bool vibrate;        // am Ende einer Pause vibrieren
} Config;

// --- main.c ---
extern Status g_status;
extern Config g_config;
extern BlockedItem g_blocked[MAX_BLOCKED];
extern int g_blocked_count;
extern bool g_blocked_loading;
extern char g_blocked_error[160];

void send_command(int command, int seconds, int index);
bool phone_ready(void);
void app_user_activity(void);
void schedule_pause_end(void);

// --- i18n.c ---
typedef enum {
  S_ACTIVE, S_PAUSED, S_OFF, S_FAILED, S_UNKNOWN, S_LOADING, S_CONNECTING,
  S_LEFT, S_UNTIL, S_UNLIMITED,
  S_QUERIES, S_BLOCKED, S_CLIENTS, S_BLOCKLIST, S_LISTS_UPDATED, S_AS_OF,
  S_WORKING,
  S_MENU_PAUSE_TITLE, S_MENU_RESUME_TITLE, S_RESUME_NOW, S_PAUSE_FOR, S_NEW_PAUSE,
  S_30S, S_5M, S_15M, S_1H, S_FOREVER,
  S_RECENT_TITLE, S_NOTHING_BLOCKED, S_LOADING_LIST,
  S_DEVICE, S_TIME, S_COUNT, S_REASON, S_BLOCKED_AS,
  S_R_OTHER, S_R_GRAVITY, S_R_REGEX, S_R_DENYLIST, S_R_CNAME, S_R_UPSTREAM, S_R_SPECIAL,
  S_ALLOW_HINT, S_UNALLOW_HINT, S_ALLOWING, S_ALLOWED_OK, S_UNALLOWED_OK,
  S_NOT_ALLOWABLE, S_ON_ALLOWLIST, S_TIMES, S_AGO_MIN, S_AGO_H, S_JUST_NOW,
  S_GLANCE_ACTIVE, S_GLANCE_PAUSED_UNTIL, S_GLANCE_PAUSED, S_GLANCE_OFF,
  S_PAUSE_ENDED,
  S_COUNT_
} StrId;

const char *tr(StrId id);

// --- status_window.c ---
void status_window_push(void);
void status_window_update(void);
void status_window_show_error(const char *text);
void status_window_set_busy(bool busy);

// --- pause_menu.c ---
void pause_menu_push(void);
void pause_menu_close(void);

// --- blocked_window.c ---
void blocked_window_push(void);
void blocked_window_update(void);
void blocked_window_item_changed(int index);
void blocked_window_show_error(const char *text);
bool blocked_window_detail_busy(void);
