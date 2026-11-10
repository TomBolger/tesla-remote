// Tesla Fleet API + Vehicle Command Protocol, running entirely on the phone. ES5.
var C = require('./crypto.js');
var P = require('./proto.js');

var FLEET = {
  na: 'https://fleet-api.prd.na.vn.cloud.tesla.com',
  eu: 'https://fleet-api.prd.eu.vn.cloud.tesla.com'
};
var TOKEN_URL = 'https://fleet-auth.prd.vn.cloud.tesla.com/oauth2/v3/token';
var SCOPES = 'openid offline_access vehicle_device_data vehicle_location vehicle_cmds vehicle_charging_cmds';

var DOMAIN_VCSEC = 2;
var DOMAIN_INFO = 3;
var FLAG_ENCRYPT_RESPONSE = 2; // 1 << Flags.FLAG_ENCRYPT_RESPONSE
var EXPIRES_IN_MS = 15000;

// ---------- storage ----------
function get(k, d) { var v = localStorage.getItem(k); return v === null || v === undefined ? d : v; }
function set(k, v) { localStorage.setItem(k, v); }
function getJSON(k) { try { return JSON.parse(get(k, 'null')); } catch (e) { return null; } }
function setJSON(k, v) { set(k, JSON.stringify(v)); }

function cfg() { return getJSON('tr_config') || {}; }
function saveCfg(c) { setJSON('tr_config', c); }
function tokens() { return getJSON('tr_tokens') || {}; }
function base() { var c = cfg(); return c.base || FLEET[c.region === 'eu' ? 'eu' : 'na']; }

// ---------- errors ----------
function TErr(code, text, extra) { this.code = code; this.text = text; this.extra = extra; }
function err(code, text, extra) { return new TErr(code, text, extra); }

// ---------- HTTP ----------
// The Pebble phone app blocks direct calls to Tesla's car server (no CORS headers),
// so those go through the project's tiny relay. Login calls go direct (Tesla allows those).
function viaRelay(url) {
  var r = cfg().relay;
  if (!r || r === 'off' || url.indexOf('https://fleet-api.') !== 0) return url;
  return r.replace(/\/+$/, '') + '/' + url.slice(8);
}
function http(method, url, headers, body, cb) {
  url = viaRelay(url);
  var x = new XMLHttpRequest();
  x.open(method, url, true);
  x.timeout = 45000;
  Object.keys(headers || {}).forEach(function (h) { x.setRequestHeader(h, headers[h]); });
  var done = false;
  function finish(status, text) { if (done) return; done = true; cb(status, text); }
  x.onreadystatechange = function () { if (x.readyState === 4) finish(x.status, x.responseText || ''); };
  x.ontimeout = function () { finish(0, 'timeout'); };
  x.onerror = function () { finish(0, 'network'); };
  x.send(body || null);
}
function form(o) {
  return Object.keys(o).map(function (k) { return encodeURIComponent(k) + '=' + encodeURIComponent(o[k]); }).join('&');
}
function parse(text) { try { return JSON.parse(text); } catch (e) { return null; } }

// ---------- OAuth ----------
function saveTokens(j) {
  var t = tokens();
  t.access = j.access_token;
  if (j.refresh_token) t.refresh = j.refresh_token;
  t.expiresAt = Date.now() + (j.expires_in || 28800) * 1000;
  setJSON('tr_tokens', t);
}
function exchangeCode(code, cb) {
  var c = cfg();
  http('POST', TOKEN_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, form({
    grant_type: 'authorization_code', client_id: c.clientId, client_secret: c.clientSecret,
    code: code, audience: base(), redirect_uri: c.redirectUri, scope: SCOPES
  }), function (s, t) {
    var j = parse(t);
    if (s !== 200 || !j || !j.access_token) return cb(err('auth', 'Sign-in failed', (j && (j.error_description || j.error)) || s));
    saveTokens(j); cb(null);
  });
}
function refresh(cb) {
  var c = cfg(), t = tokens();
  if (!t.refresh) return cb(err('setup', 'Sign in on phone'));
  http('POST', TOKEN_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, form({
    grant_type: 'refresh_token', client_id: c.clientId, refresh_token: t.refresh
  }), function (s, txt) {
    var j = parse(txt);
    if (s === 200 && j && j.access_token) { saveTokens(j); return cb(null); }
    if (s === 0) return cb(err('net', 'No connection'));
    cb(err('setup', 'Sign in again'));
  });
}
function withToken(cb) {
  var t = tokens();
  if (!t.access && !t.refresh) return cb(err('setup', 'Set up on phone'));
  if (!t.access || Date.now() > (t.expiresAt || 0) - 120000) return refresh(cb);
  cb(null);
}

// Authenticated Fleet API call -> cb(err, json)
function api(method, path, body, cb, retried) {
  withToken(function (e) {
    if (e) return cb(e);
    var h = { Authorization: 'Bearer ' + tokens().access, 'Content-Type': 'application/json' };
    http(method, base() + path, h, body ? JSON.stringify(body) : null, function (s, txt) {
      var j = parse(txt);
      var msg = (j && (j.error_description || j.error)) || '';
      if (s >= 200 && s < 300) return cb(null, j || {});
      if (s === 401 && !retried) return refresh(function (re) { if (re) return cb(re); api(method, path, body, cb, true); });
      if (s === 408 || /offline or asleep|vehicle unavailable/i.test(msg)) return cb(err('asleep', 'Car is asleep'));
      if (s === 0) return cb(err('net', 'Can\'t reach Tesla', 'no reply (offline, or blocked by the phone app)'));
      if (s === 401) return cb(err('setup', 'Sign in again'));
      if (s === 402) return cb(err('billing', 'Tesla billing needed'));
      if (s === 403) return cb(err('perm', 'Missing permission', msg));
      if (s === 412) return cb(err('register', 'App not registered', msg));
      if (s === 429) return cb(err('limit', 'Rate limited'));
      if (s >= 500) return cb(err('server', 'Tesla server error', s));
      cb(err('http', 'Error ' + s, msg));
    });
  });
}

// ---------- first-time setup after sign-in ----------
function registerPartner(cb) {
  var c = cfg();
  http('POST', TOKEN_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, form({
    grant_type: 'client_credentials', client_id: c.clientId, client_secret: c.clientSecret,
    scope: 'openid vehicle_device_data vehicle_cmds vehicle_charging_cmds', audience: base()
  }), function (s, t) {
    var j = parse(t);
    if (s !== 200 || !j || !j.access_token) return cb(err('register', 'Partner token failed', 'HTTP ' + s + ((j && (j.error_description || j.error)) ? ': ' + (j.error_description || j.error) : '')));
    http('POST', base() + '/api/1/partner_accounts',
      { Authorization: 'Bearer ' + j.access_token, 'Content-Type': 'application/json' },
      JSON.stringify({ domain: c.domain }), function (s2, t2) {
        if (s2 >= 200 && s2 < 300) return cb(null);
        var j2 = parse(t2) || {};
        var m = j2.error_description || j2.error || t2 || '';
        if (/already/i.test(m)) return cb(null);
        cb(err('register', 'Registration failed', s2 === 0 ? 'no reply from Tesla (blocked by the phone app; check the relay address)' : 'HTTP ' + s2 + (m ? ': ' + String(m).slice(0, 120) : '')));
      });
  });
}
function findRegion(cb) {
  api('GET', '/api/1/users/region', null, function (e, j) {
    if (!e && j.response && j.response.fleet_api_base_url) {
      var c = cfg(); c.base = j.response.fleet_api_base_url; saveCfg(c);
    }
    cb(null); // non-fatal: keep the region the user picked
  });
}
function listVehicles(cb) {
  api('GET', '/api/1/vehicles', null, function (e, j) {
    if (e) return cb(e);
    var list = (j.response || []).map(function (v) {
      return { vin: v.vin, name: v.display_name || 'Tesla', state: v.state };
    });
    var c = cfg();
    c.vehicles = list;
    if (!c.vin || !list.some(function (v) { return v.vin === c.vin; })) c.vin = list.length ? list[0].vin : '';
    saveCfg(c);
    if (!list.length) return cb(err('novehicle', 'No cars on account'));
    cb(null, list);
  });
}
function retryRegistration(progress, cb) {
  progress('Registering app');
  findRegion(function () {
    registerPartner(function (re) {
      var c = cfg(); c.registerError = re ? (re.text + ': ' + re.extra) : ''; saveCfg(c);
      if (re) return cb(re);
      progress('Finding car');
      listVehicles(function (le) { cb(le || null); });
    });
  });
}
// Full flow after the settings page returns a sign-in code.
function completeSignIn(code, progress, cb) {
  progress('Signing in');
  exchangeCode(code, function (e) {
    if (e) return cb(e);
    findRegion(function () {
      progress('Registering app');
      registerPartner(function (re) {
        var c = cfg(); c.registerError = re ? (re.text + ': ' + re.extra) : ''; saveCfg(c);
        progress('Finding car');
        listVehicles(function (le) { cb(le || null); });
      });
    });
  });
}

// ---------- vehicle state / data ----------
function vin() { return cfg().vin; }
function vehicleState(cb) {
  api('GET', '/api/1/vehicles/' + vin(), null, function (e, j) {
    if (e) return cb(e);
    cb(null, (j.response && j.response.state) || 'unknown');
  });
}
function vehicleData(cb) {
  if (!vin()) return cb(err('setup', 'Set up on phone'));
  var parts = 'charge_state;climate_state;drive_state;vehicle_state;vehicle_config;gui_settings';
  var withLoc = !cfg().noLocation;
  api('GET', '/api/1/vehicles/' + vin() + '/vehicle_data?endpoints=' + encodeURIComponent(parts + (withLoc ? ';location_data' : '')),
    null, function (e, j) {
      if (e && withLoc && (e.code === 'perm' || e.code === 'http')) {
        // No location permission yet: remember, and try again without it.
        var c = cfg(); c.noLocation = true; saveCfg(c);
        return vehicleData(cb);
      }
      if (e) return cb(e);
      var d = j.response || {};
      setJSON('tr_last', { at: Date.now(), d: d });
      cb(null, d);
    });
}
function wake(progress, cb) {
  progress('Waking car');
  api('POST', '/api/1/vehicles/' + vin() + '/wake_up', null, function (e) {
    if (e && e.code !== 'asleep') return cb(e);
    var tries = 0;
    (function poll() {
      tries++;
      setTimeout(function () {
        vehicleState(function (se, st) {
          if (!se && st === 'online') return cb(null);
          if (tries >= 15) return cb(err('wake', 'Car did not wake'));
          progress('Waking car ' + tries);
          poll();
        });
      }, tries === 1 ? 2000 : 3000);
    })();
  });
}

function nearbyChargers(cb) {
  api('GET', '/api/1/vehicles/' + vin() + '/nearby_charging_sites', null, function (e, j) {
    if (e) return cb(e);
    cb(null, j.response || {});
  });
}

// ---------- Vehicle Command Protocol ----------
function sessKey(domain) { return 'tr_sess_' + vin() + '_' + domain; }
function vinBytes() { return C.strToBytes(vin()); }
function tlv(tag, bytes) { return [tag, bytes.length].concat(bytes); }
function u32(n) { return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; }
function localPub() { return C.hexToBytes(cfg().pubHex); }

function postRoutable(bytes, cb) {
  api('POST', '/api/1/vehicles/' + vin() + '/signed_command',
    { routable_message: C.b64encode(bytes) }, function (e, j) {
      if (e) return cb(e);
      if (!j.response) return cb(err('proto', 'Empty car reply'));
      var raw;
      try { raw = P.decode(C.b64decode(j.response)); } catch (x) { return cb(err('proto', 'Bad car reply')); }
      cb(null, raw);
    });
}
function faultOf(msg) {
  var st = P.sub(msg, 12);
  return st ? (P.first(st, 2) || 0) : 0;
}
var FAULT_TEXT = {
  1: 'Car busy', 2: 'Car timed out', 3: 'Pair key with car', 4: 'Key disabled', 7: 'Not allowed',
  8: 'Unsupported', 9: 'Unsupported', 11: 'Car still booting', 13: 'Bad parameter',
  21: 'Mobile access off', 28: 'Update app'
};
var SYNC_FAULTS = { 5: 1, 6: 1, 15: 1, 17: 1, 26: 1 };

function handshake(domain, cb) {
  var uuid = C.randomBytes(16);
  var msg = P.cat(
    P.fMsg(6, P.fVarint(1, domain)),
    P.fMsg(7, P.fBytes(2, C.randomBytes(16))),
    P.fMsg(14, P.fBytes(1, localPub())),
    P.fBytes(51, uuid)
  );
  var sentAt = Date.now();
  postRoutable(msg, function (e, r) {
    if (e) return cb(e);
    var f = faultOf(r);
    if (f) return cb(err('fault', FAULT_TEXT[f] || ('Car fault ' + f), f));
    var infoBytes = P.first(r, 15);
    if (!infoBytes) return cb(err('proto', 'No session info'));
    var info = P.decode(infoBytes);
    if ((P.first(info, 5) || 0) === 1) return cb(err('fault', 'Pair key with car', 3));
    var vpub = P.first(info, 2);
    var K;
    try { K = C.deriveSessionKey(cfg().privHex, vpub); } catch (x) { return cb(err('proto', 'Key error')); }
    // Verify the car's answer really came from the car (HMAC over VIN + our challenge).
    var sig = P.sub(r, 13), tagMsg = sig && P.sub(sig, 6), tag = tagMsg && P.first(tagMsg, 1);
    var meta = P.cat(tlv(0, [6]), tlv(2, vinBytes()), tlv(6, uuid), [255]);
    var expect = C.hmacSha256(C.hmacSha256(K, C.strToBytes('session info')), meta.concat(infoBytes));
    if (!tag || !C.equalBytes(tag, expect)) return cb(err('proto', 'Car reply not trusted'));
    var clock = P.first(info, 4) || 0;
    var s = {
      k: C.bytesToHex(K), epoch: C.bytesToHex(P.first(info, 3) || []),
      counter: P.first(info, 1) || 0, timeZero: sentAt - clock * 1000
    };
    setJSON(sessKey(domain), s);
    cb(null, s);
  });
}
function getSession(domain, cb) {
  var s = getJSON(sessKey(domain));
  if (s && s.k && s.epoch) return cb(null, s);
  handshake(domain, cb);
}
function decryptReply(r, s) {
  var payload = P.first(r, 10);
  if (!payload) return null;
  var sig = P.sub(r, 13), gcm = sig && P.sub(sig, 9);
  if (gcm) {
    var nonce = P.first(gcm, 1);
    if (!nonce) return null;
    payload = C.aesGcmDecryptNoAuth(C.hexToBytes(s.k), nonce, payload);
  }
  return payload;
}
function checkReply(domain, payload) {
  if (!payload || !payload.length) return null;
  var m;
  try { m = P.decode(payload); } catch (x) { return null; }
  if (domain === DOMAIN_INFO) {
    var st = P.sub(m, 1);
    if (st && P.first(st, 1) === 1) {
      var reason = P.sub(st, 2), txt = reason && P.first(reason, 1);
      return txt ? C.bytesToStr(txt) : 'Car refused';
    }
  } else {
    var ne = P.sub(m, 46);
    if (ne) {
      var code = P.first(ne, 1) || 0;
      var map = { 2: 'Close doors first', 3: 'Already done', 5: 'Shift to Park', 6: 'Not authorized', 7: 'Not allowed remotely' };
      if (code) return map[code] || ('Car error ' + code);
    }
    var cs = P.sub(m, 4);
    if (cs && P.first(cs, 1) === 2) return 'Car refused';
  }
  return null;
}
function sendSigned(domain, payload, cb, attempt) {
  attempt = attempt || 0;
  getSession(domain, function (e, s) {
    if (e) return cb(e);
    s.counter = (s.counter || 0) + 1;
    setJSON(sessKey(domain), s);
    var expires = Math.floor((Date.now() + EXPIRES_IN_MS - s.timeZero) / 1000);
    var epoch = C.hexToBytes(s.epoch);
    var meta = P.cat(tlv(0, [8]), tlv(1, [domain]), tlv(2, vinBytes()), tlv(3, epoch),
      tlv(4, u32(expires)), tlv(5, u32(s.counter)), tlv(7, u32(FLAG_ENCRYPT_RESPONSE)), [255]);
    var cmdKey = C.hmacSha256(C.hexToBytes(s.k), C.strToBytes('authenticated command'));
    var tag = C.hmacSha256(cmdKey, meta.concat(payload));
    var msg = P.cat(
      P.fMsg(6, P.fVarint(1, domain)),
      P.fMsg(7, P.fBytes(2, C.randomBytes(16))),
      P.fBytes(10, payload),
      P.fMsg(13,
        P.fMsg(1, P.fBytes(1, localPub())),
        P.fMsg(8, P.fBytes(1, epoch), P.fVarint(2, s.counter), P.fFixed32(3, expires), P.fBytes(4, tag))),
      P.fBytes(51, C.randomBytes(16)),
      P.fVarint(52, FLAG_ENCRYPT_RESPONSE)
    );
    postRoutable(msg, function (pe, r) {
      if (pe) return cb(pe);
      var f = faultOf(r);
      if (f && SYNC_FAULTS[f] && attempt < 1) {
        localStorage.removeItem(sessKey(domain));
        return sendSigned(domain, payload, cb, attempt + 1);
      }
      if (f) return cb(err('fault', FAULT_TEXT[f] || ('Car fault ' + f), f));
      var problem = checkReply(domain, decryptReply(r, s));
      if (problem) return cb(err('refused', problem));
      cb(null);
    });
  });
}

// ---------- commands ----------
function infoAction(vehicleActionBytes) { return P.fMsg(2, vehicleActionBytes); }
var COMMANDS = {
  lock: function () { return [DOMAIN_VCSEC, P.fVarint(2, 1)]; },
  unlock: function () { return [DOMAIN_VCSEC, P.fVarint(2, 0)]; },
  // Closures: 3 = open, 4 = close (explicit, so a second tap can't toggle the wrong way)
  frunk: function () { return [DOMAIN_VCSEC, P.fMsg(4, P.fVarint(6, 3))]; },
  trunk: function () { return [DOMAIN_VCSEC, P.fMsg(4, P.fVarint(5, 3))]; },
  trunk_close: function () { return [DOMAIN_VCSEC, P.fMsg(4, P.fVarint(5, 4))]; },
  auto_secure: function () { return [DOMAIN_VCSEC, P.fVarint(2, 29)]; },
  charge_start: function () { return [DOMAIN_INFO, infoAction(P.fMsg(6, P.fMsg(2)))]; },
  charge_stop: function () { return [DOMAIN_INFO, infoAction(P.fMsg(6, P.fMsg(5)))]; },
  charge_limit: function (p) { return [DOMAIN_INFO, infoAction(P.fMsg(5, P.fVarint(1, p)))]; },
  charge_amps: function (a) { return [DOMAIN_INFO, infoAction(P.fMsg(43, P.fVarint(1, a)))]; },
  port_open: function () { return [DOMAIN_INFO, infoAction(P.fMsg(62))]; },
  port_close: function () { return [DOMAIN_INFO, infoAction(P.fMsg(61))]; },
  sentry: function (on) { return [DOMAIN_INFO, infoAction(P.fMsg(30, on ? P.fBool(1, true) : []))]; },
  guest: function (on) { return [DOMAIN_INFO, infoAction(P.fMsg(65, on ? P.fBool(1, true) : []))]; },
  keeper: function (m) { return [DOMAIN_INFO, infoAction(P.fMsg(44, m ? P.fVarint(1, m) : []))]; },
  cop: function (v) { // 0 off, 1 on, 2 fan only
    return [DOMAIN_INFO, infoAction(P.fMsg(50, v ? P.fBool(1, true) : [], v === 2 ? P.fBool(2, true) : []))];
  },
  cop_temp: function (lv) { return [DOMAIN_INFO, infoAction(P.fMsg(66, P.fVarint(1, lv)))]; },
  bioweapon: function (on) { return [DOMAIN_INFO, infoAction(P.fMsg(35, on ? P.fBool(1, true) : []))]; },
  auto_seat: function (v) { // v = position * 10 + on (position 1 driver, 2 passenger)
    var pos = Math.floor(v / 10), on = v % 10;
    return [DOMAIN_INFO, infoAction(P.fMsg(48, P.fMsg(1, on ? P.fBool(1, true) : [], P.fVarint(2, pos))))];
  },
  low_power: function (on) { return [DOMAIN_INFO, infoAction(P.fMsg(130, on ? P.fBool(1, true) : []))]; },
  accessory: function (on) { return [DOMAIN_INFO, infoAction(P.fMsg(138, on ? P.fBool(1, true) : []))]; },
  climate_on: function () { return [DOMAIN_INFO, infoAction(P.fMsg(10, P.fBool(1, true)))]; },
  climate_off: function () { return [DOMAIN_INFO, infoAction(P.fMsg(10))]; },
  set_temp: function (c) {
    return [DOMAIN_INFO, infoAction(P.fMsg(14, P.fMsg(5, P.fMsg(3)), P.fFloat(6, c), P.fFloat(7, c)))];
  },
  defrost_on: function () { return [DOMAIN_INFO, infoAction(P.fMsg(12, P.fBool(1, true)))]; },
  defrost_off: function () { return [DOMAIN_INFO, infoAction(P.fMsg(12))]; },
  // value = position * 16 + level. Positions: 0 driver, 1 passenger, 2 rear left, 3 rear centre, 4 rear right.
  seat: function (v) {
    var pos = [7, 8, 9, 11, 12][Math.max(0, Math.min(4, (v >> 4) & 15))];
    var lv = [2, 3, 4, 5][Math.max(0, Math.min(3, v & 15))];
    return [DOMAIN_INFO, infoAction(P.fMsg(36, P.fMsg(1, P.fMsg(lv), P.fMsg(pos))))];
  },
  // Seat cooling (ventilated front seats). value = position * 16 + level (0 off .. 3 high)
  cool: function (v) {
    var pos = ((v >> 4) & 15) === 1 ? 2 : 1, lv = Math.max(0, Math.min(3, v & 15)) + 1;
    return [DOMAIN_INFO, infoAction(P.fMsg(49, P.fMsg(1, P.fVarint(1, lv), P.fVarint(2, pos))))];
  },
  wheel_on: function () { return [DOMAIN_INFO, infoAction(P.fMsg(13, P.fBool(1, true)))]; },
  wheel_off: function () { return [DOMAIN_INFO, infoAction(P.fMsg(13))]; },
  vent: function () { return [DOMAIN_INFO, infoAction(P.fMsg(34, P.fMsg(3)))]; },
  close_windows: function () { return [DOMAIN_INFO, infoAction(P.fMsg(34, P.fMsg(4)))]; },
  set_temps: function (t) { // t = [driverC, passengerC]
    return [DOMAIN_INFO, infoAction(P.fMsg(14, P.fMsg(5, P.fMsg(3)), P.fFloat(6, t[0]), P.fFloat(7, t[1])))];
  },
  flash: function () { return [DOMAIN_INFO, infoAction(P.fMsg(26))]; },
  honk: function () { return [DOMAIN_INFO, infoAction(P.fMsg(27))]; }
};

function command(name, value, progress, cb) {
  if (!C.haveBigInt()) return cb(err('engine', 'Phone app too old'));
  var c = cfg();
  if (!c.privHex || !c.pubHex) return cb(err('setup', 'Set up on phone'));
  if (!vin()) return cb(err('setup', 'Set up on phone'));
  var built = COMMANDS[name](value);
  progress('Sending');
  sendSigned(built[0], built[1], function (e) {
    if (e && e.code === 'asleep') {
      return wake(progress, function (we) {
        if (we) return cb(we);
        progress('Sending');
        sendSigned(built[0], built[1], cb);
      });
    }
    cb(e || null);
  });
}

module.exports = {
  FLEET: FLEET, SCOPES: SCOPES, cfg: cfg, saveCfg: saveCfg, tokens: tokens, getJSON: getJSON,
  completeSignIn: completeSignIn, retryRegistration: retryRegistration, listVehicles: listVehicles, vehicleData: vehicleData,
  vehicleState: vehicleState, wake: wake, nearbyChargers: nearbyChargers, command: command, COMMANDS: COMMANDS,
  _internal: { handshake: handshake, sendSigned: sendSigned, tlv: tlv }
};
