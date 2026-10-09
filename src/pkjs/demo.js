// Demo-Modus: ein nachgebauter Pi-hole (API v6) mit erfundenen Daten.
// Aktiv, wenn als Adresse „demo“ eingetragen ist.

var state = null;

var CLIENTS = {
  de: [
    { ip: '192.168.1.23', name: 'iPhone-Anna' },
    { ip: '192.168.1.40', name: 'Fernseher-Wohnzimmer' },
    { ip: '192.168.1.31', name: 'Laptop-Max' },
    { ip: '192.168.1.52', name: 'Tablet-Kinder' }
  ],
  en: [
    { ip: '192.168.1.23', name: 'annas-iphone' },
    { ip: '192.168.1.40', name: 'living-room-tv' },
    { ip: '192.168.1.31', name: 'max-laptop' },
    { ip: '192.168.1.52', name: 'kids-tablet' }
  ]
};

// Domain, Status, Abstand in Sekunden vor „jetzt“, Gerät, Wiederholungen, CNAME
var BLOCKED = [
  ['ads.samsungads.com', 'GRAVITY', 40, 1, 3],
  ['app-measurement.com', 'GRAVITY', 95, 0, 6],
  ['graph.facebook.com', 'DENYLIST', 260, 2, 2],
  ['cdn.shop-tracker.example', 'GRAVITY_CNAME', 410, 0, 1, 'collect.adnetwork.example'],
  ['googleads.g.doubleclick.net', 'GRAVITY', 700, 3, 4],
  ['telemetry.smart-tv.example', 'REGEX', 1300, 1, 9],
  ['in.appcenter.ms', 'GRAVITY', 2100, 2, 1],
  ['mask.icloud.com', 'SPECIAL_DOMAIN', 3600, 0, 2],
  ['stats.gamevendor.example', 'GRAVITY', 5400, 3, 5],
  ['sdk.iad-tracking.example', 'GRAVITY', 8000, 0, 1]
];

function now() { return Date.now() / 1000; }

function init() {
  if (state) return;
  state = {
    blocking: true,
    timerEnd: null,      // Sekunden (Unixzeit), wann die Pause endet
    allow: {}            // Allowlist
  };
}

function remaining() {
  if (state.timerEnd === null) return null;
  var r = state.timerEnd - now();
  if (r <= 0) {
    state.blocking = !state.blocking;
    state.timerEnd = null;
    return null;
  }
  return Math.round(r * 10) / 10;
}

function blockingJson() {
  var t = remaining();
  return { blocking: state.blocking ? 'enabled' : 'disabled', timer: t, took: 0.001 };
}

function parseQuery(path) {
  var q = {};
  var i = path.indexOf('?');
  if (i < 0) return q;
  path.substring(i + 1).split('&').forEach(function (p) {
    var kv = p.split('=');
    q[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
  });
  return q;
}

function handle(method, path, body, lang) {
  init();
  var base = path.split('?')[0];
  var q = parseQuery(path);

  if (base === '/auth' && method === 'POST') {
    return [200, { session: { valid: true, totp: false, sid: 'demo-sid', csrf: 'demo', validity: 1800, message: 'correct password' } }];
  }
  if (base === '/dns/blocking' && method === 'GET') return [200, blockingJson()];
  if (base === '/dns/blocking' && method === 'POST') {
    if (!body || typeof body.blocking !== 'boolean') {
      return [400, { error: { key: 'body_error', message: 'No "blocking" boolean in body data', hint: null } }];
    }
    state.blocking = body.blocking;
    state.timerEnd = (typeof body.timer === 'number' && body.timer > 0) ? now() + body.timer : null;
    return [200, blockingJson()];
  }
  if (base === '/stats/summary') {
    // leicht schwankende Zahlen, damit es lebendig aussieht
    var minute = Math.floor(now() / 60) % 60;
    var total = 48210 + minute * 13;
    var blocked = 6124 + minute * 2;
    return [200, {
      queries: {
        total: total, blocked: blocked, percent_blocked: blocked * 100 / total,
        unique_domains: 3921, forwarded: 30112, cached: 11974, frequency: 0.56
      },
      clients: { active: 17, total: 23 },
      gravity: { domains_being_blocked: 176418, last_update: Math.floor(now() - 2.6 * 86400) },
      took: 0.002
    }];
  }
  if (base === '/queries') {
    var length = parseInt(q.length || '100', 10);
    var clients = CLIENTS[lang] || CLIENTS.de;
    var list = [];
    var id = 900000;
    BLOCKED.forEach(function (b) {
      if (state.allow[b[0]] || (b[5] && state.allow[b[5]])) return;  // freigegeben → nicht mehr blockiert
      for (var k = 0; k < b[4]; k++) {
        list.push({
          id: id--, time: now() - b[2] - k * 600, type: 'A', domain: b[0], cname: b[5] || null,
          status: b[1], client: clients[b[3]], dnssec: 'UNKNOWN', reply: { type: 'IP', time: 0.1 },
          list_id: 3, upstream: null, ede: { code: -1, text: null }
        });
      }
    });
    list.sort(function (a, c) { return c.time - a.time; });
    if (q.upstream !== 'blocklist') list = [];
    return [200, { queries: list.slice(0, length), cursor: id, recordsTotal: 48210, recordsFiltered: list.length, draw: 0, took: 0.01 }];
  }
  if (base === '/domains/allow/exact' && method === 'GET') {
    return [200, {
      domains: Object.keys(state.allow).map(function (d, i) {
        return { domain: d, unicode: d, type: 'allow', kind: 'exact', comment: 'Pebble', groups: [0], enabled: true, id: i + 1 };
      }),
      took: 0.001
    }];
  }
  if (base === '/domains/allow/exact' && method === 'POST') {
    if (state.allow[body.domain]) {
      return [400, { error: { key: 'database_error', message: 'Could not add to gravity database', hint: 'UNIQUE constraint failed: domainlist.domain, domainlist.type – The item is already present' } }];
    }
    state.allow[body.domain] = true;
    return [201, { domains: [{ domain: body.domain, type: 'allow', kind: 'exact', enabled: true }], processed: { success: [{ item: body.domain }], errors: [] } }];
  }
  if (base.indexOf('/domains/allow/exact/') === 0 && method === 'DELETE') {
    var d = decodeURIComponent(base.substring('/domains/allow/exact/'.length));
    if (!state.allow[d]) return [404, { took: 0.001 }];
    delete state.allow[d];
    return [204, null];
  }
  return [404, { error: { key: 'not_found', message: 'Not found', hint: path } }];
}

// Wie eine echte Anfrage: Antwort kommt asynchron
function request(method, path, body, lang, cb) {
  var r = handle(method, path, body ? JSON.parse(JSON.stringify(body)) : null, lang);
  setTimeout(function () { cb(r[0], r[1]); }, 250);
}

module.exports = {
  request: request,
  handle: handle,
  reset: function () { state = null; }
};
