// Handy-Teil der Pi-hole-App: spricht mit der Pi-hole-API (Version 6) und
// schickt fertig aufbereitete Daten an die Uhr.
// Bewusst ES5 (Pebble-SDK nutzt webpack 1; iOS führt PebbleKit JS in JavaScriptCore aus).

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var demo = require('./demo');
var clay = null;  // wird beim Öffnen der Einstellungen in der aktuellen Sprache erzeugt

var SETTINGS_KEY = 'pihole-settings';
var SESSION_KEY = 'pihole-session';

// Muss zu src/c/pihole.h passen
var MSG = { STATUS: 1, ERROR: 2, BLK_BEGIN: 3, BLK_ITEM: 4, BLK_END: 5, ALLOWED: 6, CONFIG: 7, BUSY: 8 };
var CMD = { REFRESH: 1, SET_BLOCKING: 2, RECENT: 3, ALLOW: 4, UNALLOW: 5, QUICK: 6 };
var BLOCKING = { unknown: 0, enabled: 1, disabled: 2, failed: 3 };
var REASON = { OTHER: 0, GRAVITY: 1, REGEX: 2, DENYLIST: 3, CNAME: 4, UPSTREAM: 5, SPECIAL: 6 };
var FLAG = { ALLOWED: 1, NOT_ALLOWABLE: 2 };

var MAX_ITEMS = 30;       // wie MAX_BLOCKED in pihole.h
var DOMAIN_MAX = 98;      // DOMAIN_LEN in pihole.h minus Endnull/Reserve
var CLIENT_MAX = 38;
var QUERY_WINDOW = 400;   // so viele blockierte Anfragen werden ausgewertet
var TIMEOUT_MS = 10000;

// Texte, die das Handy an die Uhr schickt
var TEXT = {
  de: {
    setup: 'Bitte in der Pebble-App unter Einstellungen die Adresse deines Pi-hole eintragen – oder „demo“ zum Ausprobieren.',
    unreachable: 'Pi-hole nicht erreichbar. Ist das Handy im richtigen WLAN und stimmt die Adresse?',
    timeout: 'Zeitüberschreitung – Pi-hole antwortet nicht.',
    wrongPassword: 'Passwort falsch. Am besten ein App-Passwort verwenden (siehe Einstellungen).',
    twoFactor: 'Zwei-Faktor-Anmeldung ist aktiv. Bitte ein App-Passwort eintragen – das funktioniert ohne Code.',
    seats: 'Pi-hole hat keine freien API-Sitzungen mehr. In ein paar Minuten erneut versuchen.',
    rateLimit: 'Zu viele Anmeldeversuche. Bitte kurz warten.',
    notV6: 'Keine Pi-hole-6-API unter dieser Adresse gefunden. Läuft Pi-hole 6 und stimmt der Port?',
    forbidden: 'Zugriff verweigert (%s).',
    serverError: 'Fehler im Pi-hole (%s).',
    invalidResponse: 'Unerwartete Antwort vom Pi-hole.',
    rejected: 'Abgelehnt: %s',
    never: '–',
    today: 'heute',
    yesterday: 'gestern',
    daysAgo: 'vor %s Tagen',
    hoursAgo: 'vor %s Std.'
  },
  en: {
    setup: 'Please enter your Pi-hole address in the Pebble app settings – or "demo" to try it out.',
    unreachable: 'Pi-hole not reachable. Is the phone on the right Wi-Fi and is the address correct?',
    timeout: 'Timeout – Pi-hole does not respond.',
    wrongPassword: 'Wrong password. Best use an app password (see settings).',
    twoFactor: 'Two-factor authentication is on. Please enter an app password – it works without a code.',
    seats: 'Pi-hole has no free API sessions left. Try again in a few minutes.',
    rateLimit: 'Too many login attempts. Please wait a moment.',
    notV6: 'No Pi-hole 6 API found at this address. Is Pi-hole 6 running and is the port right?',
    forbidden: 'Access denied (%s).',
    serverError: 'Pi-hole error (%s).',
    invalidResponse: 'Unexpected response from Pi-hole.',
    rejected: 'Rejected: %s',
    never: '–',
    today: 'today',
    yesterday: 'yesterday',
    daysAgo: '%s days ago',
    hoursAgo: '%s h ago'
  }
};

// ------------------------------------------------------------------ Einstellungen

function phoneLang() {
  var l = (typeof navigator !== 'undefined' && navigator.language) ? navigator.language : 'de';
  return l.toLowerCase().indexOf('de') === 0 ? 'de' : 'en';
}

function loadSettings() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem(SETTINGS_KEY)) || {}; } catch (e) { s = {}; }
  return {
    url: s.url || '',
    password: s.password || '',
    lang: s.lang === 'en' || s.lang === 'de' ? s.lang : phoneLang(),
    pause: parseInt(s.pause, 10) || 300,
    quick: s.quick !== false,
    vibrate: s.vibrate !== false,
    max: Math.max(5, Math.min(MAX_ITEMS, parseInt(s.max, 10) || 20))
  };
}

function saveSettings(s) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
}

function T(key, arg) {
  var table = TEXT[loadSettings().lang] || TEXT.de;
  var text = table[key] || TEXT.de[key] || key;
  return arg === undefined ? text : text.replace('%s', arg);
}

// „pi.hole/admin/“ → „http://pi.hole“, „demo“ bleibt „demo“
function normalizeUrl(u) {
  u = String(u || '').trim();
  if (!u) return '';
  if (u.toLowerCase() === 'demo') return 'demo';
  if (!/^https?:\/\//i.test(u)) u = 'http://' + u;
  // alles ab /admin bzw. /api abschneiden (die App hängt /api selbst an)
  u = u.replace(/\/(admin|api)(\/.*)?$/i, '');
  u = u.replace(/[?#].*$/, '');
  return u.replace(/\/+$/, '');
}

function hostOf(url) {
  if (url === 'demo') return 'Demo';
  var m = /^https?:\/\/([^\/?#]+)/i.exec(url);
  return m ? m[1] : url;
}

// ------------------------------------------------------------------ Formatierung

function fmtInt(n) {
  n = Math.round(Number(n) || 0);
  var sep = loadSettings().lang === 'en' ? ',' : '.';
  var s = String(Math.abs(n));
  var out = '';
  while (s.length > 3) {
    out = sep + s.substring(s.length - 3) + out;
    s = s.substring(0, s.length - 3);
  }
  return (n < 0 ? '-' : '') + s + out;
}

function fmtPct(p) {
  p = Number(p) || 0;
  var s = p.toFixed(1);
  return loadSettings().lang === 'en' ? s + '%' : s.replace('.', ',') + ' %';
}

function fmtAge(ts) {
  if (!ts) return T('never');
  var secs = Date.now() / 1000 - ts;
  if (secs < 0) secs = 0;
  if (secs < 6 * 3600) return T('hoursAgo', String(Math.max(1, Math.round(secs / 3600))));
  var startOfToday = new Date(); startOfToday.setHours(0, 0, 0, 0);
  var then = new Date(ts * 1000);
  if (then >= startOfToday) return T('today');
  var days = Math.round((startOfToday - new Date(then.getFullYear(), then.getMonth(), then.getDate())) / 86400000);
  if (days <= 1) return T('yesterday');
  return T('daysAgo', String(days));
}

function truncLeft(s, max) {
  s = String(s || '');
  return s.length <= max ? s : '…' + s.substring(s.length - max + 1);
}

function truncRight(s, max) {
  s = String(s || '');
  return s.length <= max ? s : s.substring(0, max - 1) + '…';
}

// ------------------------------------------------------------------ Nachrichten an die Uhr

var queue = [];
var sending = false;

function send(msg) {
  queue.push({ msg: msg, tries: 0 });
  pump();
}

function pump() {
  if (sending || queue.length === 0) return;
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.msg, function () {
    queue.shift();
    sending = false;
    pump();
  }, function () {
    item.tries++;
    sending = false;
    if (item.tries >= 4) queue.shift();
    setTimeout(pump, 300);
  });
}

function sendError(text) {
  send({ Type: MSG.ERROR, Text: truncRight(text, 150) });
}

// serverChanged: die Uhr verwirft dann die zwischengespeicherten Werte des alten Servers
function sendConfig(serverChanged) {
  var s = loadSettings();
  send({
    Type: MSG.CONFIG,
    Flags: serverChanged ? 1 : 0,
    Lang: s.lang === 'en' ? 1 : 0,
    Seconds: s.pause,
    QuickLaunch: s.quick ? 1 : 0,
    Vibrate: s.vibrate ? 1 : 0
  });
}

// ------------------------------------------------------------------ HTTP / Anmeldung

function rawRequest(method, url, path, body, sid, cb) {
  var s = loadSettings();
  if (url === 'demo') {
    demo.request(method, path, body, s.lang, function (status, json) { cb(status, json); });
    return;
  }
  var xhr = new XMLHttpRequest();
  var done = false;
  var timer = setTimeout(function () {
    if (done) return;
    done = true;
    try { xhr.abort(); } catch (e) { /* egal */ }
    cb('timeout', null);
  }, TIMEOUT_MS);
  xhr.onload = function () {
    if (done) return;
    done = true;
    clearTimeout(timer);
    var json = null;
    if (xhr.responseText) {
      try { json = JSON.parse(xhr.responseText); } catch (e) { json = undefined; }
    }
    cb(xhr.status, json);
  };
  xhr.onerror = function () {
    if (done) return;
    done = true;
    clearTimeout(timer);
    cb(0, null);
  };
  xhr.open(method, url + '/api' + path, true);
  xhr.setRequestHeader('Accept', 'application/json');
  if (body !== null && body !== undefined) xhr.setRequestHeader('Content-Type', 'application/json');
  if (sid) xhr.setRequestHeader('X-FTL-SID', sid);
  xhr.send(body !== null && body !== undefined ? JSON.stringify(body) : null);
}

// einfacher Hash, damit eine gespeicherte Sitzung nach Passwortwechsel verworfen wird
function hashOf(str) {
  var h = 5381;
  for (var i = 0; i < str.length; i++) h = ((h << 5) + h + str.charCodeAt(i)) | 0;
  return String(h);
}

function sessionKey(s) { return normalizeUrl(s.url) + '|' + hashOf(s.password); }

function loadSession() {
  try { return JSON.parse(localStorage.getItem(SESSION_KEY)) || null; } catch (e) { return null; }
}

function saveSession(sess) {
  if (sess) localStorage.setItem(SESSION_KEY, JSON.stringify(sess));
  else localStorage.removeItem(SESSION_KEY);
}

function errorFor(status, json) {
  var key = json && json.error && json.error.key;
  var message = json && json.error && json.error.message;
  if (status === 'timeout') return T('timeout');
  if (status === 0) return T('unreachable');
  if (status === 401) return T('wrongPassword');
  if (status === 403) return T('forbidden', String(status));
  if (status === 404) return T('notV6');
  if (status === 429) return key === 'api_seats_exceeded' ? T('seats') : T('rateLimit');
  if (status >= 500) return T('serverError', String(status));
  if (status === 400 && message) return T('rejected', message);
  if (json === undefined) return T('invalidResponse');
  return T('serverError', String(status));
}

// Liefert per cb(err) eine gültige Sitzung (oder err = Fehlertext)
function login(s, cb) {
  var url = normalizeUrl(s.url);
  rawRequest('POST', url, '/auth', { password: s.password }, null, function (status, json) {
    var sess = json && json.session;
    if (status === 200 && sess && sess.valid) {
      var validity = (typeof sess.validity === 'number' && sess.validity > 0) ? sess.validity : 1800;
      saveSession({ key: sessionKey(s), sid: sess.sid || '', validity: validity, until: Date.now() + (validity - 30) * 1000 });
      cb(null);
      return;
    }
    if (status === 400 && json && json.error && /2FA|TOTP/i.test(json.error.message || '')) { cb(T('twoFactor')); return; }
    if ((status === 401 || status === 200) && sess && sess.valid === false) {
      cb(sess.totp ? T('twoFactor') : T('wrongPassword'));
      return;
    }
    if (status === 200 && !sess) { cb(T('notV6')); return; }  // z. B. eine andere Webseite
    cb(errorFor(status, json));
  });
}

// Laufen mehrere Anfragen gleichzeitig, meldet sich die App trotzdem nur einmal an
var loginWaiters = null;

function ensureSession(s, cb) {
  if (normalizeUrl(s.url) === 'demo') { cb(null); return; }
  var sess = loadSession();
  if (sess && sess.key === sessionKey(s) && sess.until > Date.now()) { cb(null); return; }
  if (loginWaiters) { loginWaiters.push(cb); return; }
  loginWaiters = [cb];
  login(s, function (err) {
    var waiters = loginWaiters;
    loginWaiters = null;
    waiters.forEach(function (w) { w(err); });
  });
}

// Anfrage mit Anmeldung; bei 401 einmal neu anmelden
function api(method, path, body, cb, retried) {
  var s = loadSettings();
  var url = normalizeUrl(s.url);
  if (!url) { cb(T('setup')); return; }
  ensureSession(s, function (err) {
    if (err) { cb(err); return; }
    var sess = loadSession();
    var sid = (url !== 'demo' && sess) ? sess.sid : null;
    rawRequest(method, url, path, body, sid, function (status, json) {
      if (status === 401 && !retried && url !== 'demo') {
        // nur verwerfen, wenn nicht schon eine parallele Anfrage neu angemeldet hat
        var current = loadSession();
        if (current && current.sid === sid) saveSession(null);
        api(method, path, body, cb, true);
        return;
      }
      if (typeof status === 'number' && status >= 200 && status < 300) {
        // jede erfolgreiche Anfrage verlängert die Sitzung
        if (sess && sess.key === sessionKey(s)) {
          sess.until = Date.now() + (sess.validity - 30) * 1000;
          saveSession(sess);
        }
        if (json === undefined) { cb(T('invalidResponse')); return; }
        cb(null, status, json);
        return;
      }
      cb(errorFor(status, json), status, json);
    });
  });
}

// ------------------------------------------------------------------ Status

var lastSummary = null;

function sendStatus(blk, summary) {
  var s = loadSettings();
  var q = (summary && summary.queries) || {};
  var pct = Number(q.percent_blocked) || 0;
  var timer = (blk && typeof blk.timer === 'number' && blk.timer > 0) ? Math.ceil(blk.timer) : -1;
  var clients = summary && summary.clients ? summary.clients.active : 0;
  var gravity = summary && summary.gravity ? summary.gravity : {};
  send({
    Type: MSG.STATUS,
    Blocking: BLOCKING[blk && blk.blocking] || 0,
    Timer: timer,
    PercentX10: Math.round(pct * 10),
    Total: fmtInt(q.total),
    Blocked: fmtInt(q.blocked),
    Percent: fmtPct(pct),
    Clients: fmtInt(clients),
    Gravity: fmtInt(gravity.domains_being_blocked),
    Updated: fmtAge(gravity.last_update),
    Host: truncRight(hostOf(normalizeUrl(s.url)), 38)
  });
}

// Holt Blockierstatus und Statistik (parallel)
function fetchStatus(blkKnown) {
  var blk = blkKnown || null, summary = null, failed = false, pending = blkKnown ? 1 : 2;
  function done(err) {
    if (failed) return;
    if (err) { failed = true; sendError(err); return; }
    if (--pending === 0) {
      lastSummary = summary;
      sendStatus(blk, summary);
    }
  }
  api('GET', '/stats/summary', null, function (err, status, json) { summary = json; done(err); });
  if (!blkKnown) api('GET', '/dns/blocking', null, function (err, status, json) { blk = json; done(err); });
}

function refresh() {
  var s = loadSettings();
  if (!normalizeUrl(s.url)) { sendError(T('setup')); return; }
  // erst anmelden, dann parallel abfragen (sonst zwei Anmeldungen gleichzeitig)
  ensureSession(s, function (err) {
    if (err) { sendError(err); return; }
    fetchStatus(null);
  });
}

// seconds: 0 = aktivieren, > 0 = pausieren, -1 = unbegrenzt pausieren
function setBlocking(seconds) {
  var body = seconds === 0 ? { blocking: true, timer: null }
    : { blocking: false, timer: seconds > 0 ? seconds : null };
  api('POST', '/dns/blocking', body, function (err, status, json) {
    if (err) { sendError(err); return; }
    if (lastSummary) sendStatus(json, lastSummary);  // sofort zeigen …
    fetchStatus(json);                                // … dann Zahlen aktualisieren
  });
}

function quickToggle(seconds) {
  api('GET', '/dns/blocking', null, function (err, status, json) {
    if (err) { sendError(err); return; }
    setBlocking(json && json.blocking === 'disabled' ? 0 : (seconds || 300));
  });
}

// ------------------------------------------------------------------ Zuletzt blockiert

var lastItems = [];

function reasonOf(status) {
  switch (status) {
    case 'GRAVITY': return REASON.GRAVITY;
    case 'REGEX': return REASON.REGEX;
    case 'DENYLIST': return REASON.DENYLIST;
    case 'GRAVITY_CNAME': case 'REGEX_CNAME': case 'DENYLIST_CNAME': return REASON.CNAME;
    case 'EXTERNAL_BLOCKED_IP': case 'EXTERNAL_BLOCKED_NULL': case 'EXTERNAL_BLOCKED_NXRA':
    case 'EXTERNAL_BLOCKED_EDE15': return REASON.UPSTREAM;
    case 'SPECIAL_DOMAIN': return REASON.SPECIAL;
    default: return REASON.OTHER;
  }
}

// Fasst die Anfragen pro Domain zusammen (neueste zuerst)
function groupQueries(queries, max, allowed) {
  var byDomain = {};
  var items = [];
  (queries || []).forEach(function (q) {
    if (!q || !q.domain) return;
    var it = byDomain[q.domain];
    if (it) { it.count++; return; }
    if (items.length >= max) return;
    var reason = reasonOf(q.status);
    var cname = reason === REASON.CNAME && q.cname ? q.cname : '';
    it = {
      domain: q.domain,
      cname: cname,
      allowDomain: cname || q.domain,
      client: (q.client && (q.client.name || q.client.ip)) || '?',
      time: Math.floor(Number(q.time) || 0),
      count: 1,
      reason: reason
    };
    byDomain[q.domain] = it;
    items.push(it);
  });
  items.forEach(function (it) {
    it.flags = 0;
    if (allowed[it.allowDomain]) it.flags |= FLAG.ALLOWED;
    if (it.reason === REASON.UPSTREAM || it.reason === REASON.SPECIAL) it.flags |= FLAG.NOT_ALLOWABLE;
  });
  return items;
}

function sendItems(items) {
  lastItems = items;
  send({ Type: MSG.BLK_BEGIN, Num: items.length });
  items.forEach(function (it, i) {
    send({
      Type: MSG.BLK_ITEM, Index: i,
      Domain: truncLeft(it.domain, DOMAIN_MAX),
      Cname: truncLeft(it.cname, DOMAIN_MAX),
      Client: truncRight(it.client, CLIENT_MAX),
      Time: it.time, Count: it.count, Reason: it.reason, Flags: it.flags
    });
  });
  send({ Type: MSG.BLK_END });
}

function fetchRecent() {
  var s = loadSettings();
  if (!normalizeUrl(s.url)) { sendError(T('setup')); return; }
  ensureSession(s, function (err) {
    if (err) { sendError(err); return; }
    var queries = null, allowed = {}, pending = 2, failed = false;
    function done(e) {
      if (failed) return;
      if (e) { failed = true; sendError(e); return; }
      if (--pending === 0) sendItems(groupQueries(queries, s.max, allowed));
    }
    api('GET', '/queries?upstream=blocklist&length=' + QUERY_WINDOW, null, function (e, status, json) {
      if (e && status === 400) {
        // ältere Pi-hole-Version ohne diesen Filter: nur Domains
        api('GET', '/stats/recent_blocked?count=' + s.max, null, function (e2, st2, j2) {
          queries = ((j2 && j2.blocked) || []).map(function (d) { return { domain: d, status: 'GRAVITY', client: null, time: 0 }; });
          done(e2);
        });
        return;
      }
      queries = json && json.queries;
      done(e);
    });
    api('GET', '/domains/allow/exact', null, function (e, status, json) {
      // Allowlist ist nur Zusatzinfo – Fehler hier ignorieren
      ((json && json.domains) || []).forEach(function (d) { if (d.enabled !== false) allowed[d.domain] = true; });
      done(null);
    });
  });
}

function setAllowed(index, allow) {
  var it = lastItems[index];
  if (!it) { sendError(T('invalidResponse')); return; }
  var done = function () {
    if (allow) it.flags |= FLAG.ALLOWED; else it.flags &= ~FLAG.ALLOWED;
    send({ Type: MSG.ALLOWED, Index: index, Flags: it.flags });
  };
  if (allow) {
    api('POST', '/domains/allow/exact', { domain: it.allowDomain, comment: 'Pebble', enabled: true }, function (err, status, json) {
      var text = json && json.error ? (json.error.message || '') + ' ' + (json.error.hint || '') : '';
      if (err && !(status === 400 && /already present|UNIQUE/i.test(text))) { sendError(err); return; }
      done();
    });
  } else {
    api('DELETE', '/domains/allow/exact/' + encodeURIComponent(it.allowDomain), null, function (err, status) {
      if (err && status !== 404) { sendError(err); return; }
      done();
    });
  }
}

// ------------------------------------------------------------------ Ereignisse

Pebble.addEventListener('ready', function () {
  sendConfig();  // die Uhr fordert danach selbst den Status an
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  switch (p.Command) {
    case CMD.REFRESH: refresh(); break;
    case CMD.SET_BLOCKING: setBlocking(p.Seconds | 0); break;
    case CMD.QUICK: quickToggle(p.Seconds | 0); break;
    case CMD.RECENT: fetchRecent(); break;
    case CMD.ALLOW: setAllowed(p.Index | 0, true); break;
    case CMD.UNALLOW: setAllowed(p.Index | 0, false); break;
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var s = loadSettings();
  clay = new Clay(clayConfig(s.lang), null, { autoHandleEvents: false });
  // aktuelle Werte vorbelegen (Clay liest sie aus 'clay-settings')
  localStorage.setItem('clay-settings', JSON.stringify({
    CfgLang: s.lang, CfgUrl: s.url, CfgPassword: s.password, CfgPause: String(s.pause),
    CfgQuick: s.quick, CfgVibrate: s.vibrate, CfgMax: s.max
  }));
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response || !clay) return;
  var raw;
  try { raw = clay.getSettings(e.response, false); } catch (err) { return; }
  var v = function (k) { return raw[k] && typeof raw[k] === 'object' ? raw[k].value : raw[k]; };
  var old = loadSettings();
  var s = {
    url: String(v('CfgUrl') || '').trim(),
    password: String(v('CfgPassword') || '').trim(),
    lang: v('CfgLang') || old.lang,
    pause: parseInt(v('CfgPause'), 10) || 300,
    quick: v('CfgQuick') !== false && v('CfgQuick') !== 'false',
    vibrate: v('CfgVibrate') !== false && v('CfgVibrate') !== 'false',
    max: parseInt(v('CfgMax'), 10) || 20
  };
  saveSettings(s);
  var serverChanged = normalizeUrl(s.url) !== normalizeUrl(old.url);
  if (serverChanged || s.password !== old.password) saveSession(null);
  if (normalizeUrl(s.url) === 'demo') demo.reset();
  sendConfig(serverChanged);  // die Uhr lädt danach neu
});

// Für Tests in Node
if (typeof module !== 'undefined' && Pebble.__test) {
  module.exports = {
    normalizeUrl: normalizeUrl, fmtInt: fmtInt, fmtPct: fmtPct, fmtAge: fmtAge,
    groupQueries: groupQueries, truncLeft: truncLeft, hostOf: hostOf
  };
}
