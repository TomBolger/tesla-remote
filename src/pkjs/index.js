// Tesla Remote - phone side. Talks to the watch and to Tesla directly (no helper server).
var T = require('./tesla.js');
var C = require('./crypto.js');

// Where the setup page is hosted (static page, see /config in the repo).
var CONFIG_URL = 'https://tombolger.github.io/tesla-remote/';

var TYPE = { STATUS: 1, CMD: 2, RESULT: 3, REFRESH: 4, PROGRESS: 5 };
var CMDS = [null, 'lock', 'unlock', 'frunk', 'trunk', 'climate_on', 'climate_off', 'set_temp',
  'defrost_on', 'defrost_off', 'seat', 'flash', 'honk'];
var STATE = { UNKNOWN: 0, ONLINE: 1, ASLEEP: 2, OFFLINE: 3, ERROR: 4, SETUP: 5, PAIR: 6, BUSY: 7 };
var NO_TEMP = -1000;

// ---------- message queue (Pebble only allows one in flight) ----------
var queue = [], sending = false;
function send(msg) { queue.push(msg); pump(); }
function pump() {
  if (sending || !queue.length) return;
  sending = true;
  var m = queue.shift(), tries = 0;
  (function attempt() {
    Pebble.sendAppMessage(m, function () { sending = false; pump(); }, function () {
      if (++tries < 3) return setTimeout(attempt, 300);
      sending = false; pump();
    });
  })();
}
function progress(text) { send({ Type: TYPE.PROGRESS, Text: String(text).slice(0, 40) }); }

// ---------- status ----------
function t10(v) { return (typeof v === 'number') ? Math.round(v * 10) : NO_TEMP; }
function modelCode(t) {
  t = String(t || '').toLowerCase();
  if (t.indexOf('models') >= 0) return 1;
  if (t.indexOf('model3') >= 0) return 2;
  if (t.indexOf('modelx') >= 0) return 3;
  if (t.indexOf('modely') >= 0) return 4;
  if (t.indexOf('cyber') >= 0) return 5;
  return 0;
}
function unitsFlags(d) {
  var c = T.cfg(), g = d.gui_settings || {};
  var f = c.tempUnit ? c.tempUnit === 'F' : g.gui_temperature_units === 'F';
  var mi = c.distUnit ? c.distUnit === 'mi' : (g.gui_distance_units ? /mi/.test(g.gui_distance_units) : true);
  return (f ? 1 : 0) | (mi ? 2 : 0);
}
function statusMsg(d, at, carState, text) {
  d = d || {};
  var ch = d.charge_state || {}, cl = d.climate_state || {}, vs = d.vehicle_state || {}, vc = d.vehicle_config || {};
  var units = unitsFlags(d);
  var rangeMi = typeof ch.battery_range === 'number' ? ch.battery_range : 0;
  var charging = { Disconnected: 0, Stopped: 1, Charging: 2, Complete: 3 }[ch.charging_state];
  var cfg = T.cfg();
  var name = cfg.carName || vs.vehicle_name || d.display_name || 'Tesla';
  return {
    Type: TYPE.STATUS,
    CarState: carState,
    Text: (text || '').slice(0, 40),
    HasData: d.charge_state ? 1 : 0,
    Battery: ch.battery_level || 0,
    Range: Math.round(units & 2 ? rangeMi : rangeMi * 1.609344),
    Charging: charging === undefined ? 0 : charging,
    ChargeLimit: ch.charge_limit_soc || 0,
    MinutesToFull: ch.minutes_to_full_charge || 0,
    Locked: vs.locked ? 1 : 0,
    Frunk: vs.ft ? 1 : 0,
    Trunk: vs.rt ? 1 : 0,
    Climate: cl.is_climate_on ? 1 : 0,
    InTemp: t10(cl.inside_temp),
    OutTemp: t10(cl.outside_temp),
    SetTemp: t10(typeof cl.driver_temp_setting === 'number' ? cl.driver_temp_setting : 21),
    Defrost: (cl.is_front_defroster_on || cl.defrost_mode) ? 1 : 0,
    SeatHeat: cl.seat_heater_left || 0,
    Units: units,
    Model: modelCode(vc.car_type),
    Age: at ? Math.max(0, Math.round((Date.now() - at) / 1000)) : 0,
    CarName: String(name).slice(0, 24)
  };
}
function cached() { return T.getJSON('tr_last') || { at: 0, d: {} }; }
function sendCached(state, text) { var c = cached(); send(statusMsg(c.d, c.at, state, text)); }
function stateForError(e) {
  if (!e) return STATE.ERROR;
  if (e.code === 'setup' || e.code === 'novehicle') return STATE.SETUP;
  if (e.code === 'asleep') return STATE.ASLEEP;
  if (e.code === 'fault' && (e.extra === 3 || e.extra === 4)) return STATE.PAIR;
  return STATE.ERROR;
}
function configured() { var c = T.cfg(); return !!(c.clientId && c.vin && (T.tokens().refresh || T.tokens().access)); }

function refresh(forceWake) {
  if (!configured()) return sendCached(STATE.SETUP, 'Open settings on phone');
  function getData() {
    T.vehicleData(function (e, d) {
      if (e) return sendCached(stateForError(e), e.text);
      send(statusMsg(d, Date.now(), STATE.ONLINE, ''));
    });
  }
  if (forceWake) {
    send({ Type: TYPE.PROGRESS, Text: 'Waking car' });
    return T.wake(progress, function (we) {
      if (we) return sendCached(stateForError(we), we.text);
      getData();
    });
  }
  // Checking if the car is awake is cheap; reading full data from a sleeping car is not possible.
  T.vehicleState(function (e, st) {
    if (e) return sendCached(stateForError(e), e.text);
    if (st === 'online') return getData();
    sendCached(st === 'offline' ? STATE.OFFLINE : STATE.ASLEEP, '');
  });
}

// Update the cached copy so the watch shows the new state right away.
function optimistic(name, value) {
  var c = cached(), d = c.d || {};
  d.vehicle_state = d.vehicle_state || {}; d.climate_state = d.climate_state || {};
  var vs = d.vehicle_state, cl = d.climate_state;
  switch (name) {
    case 'lock': vs.locked = true; break;
    case 'unlock': vs.locked = false; break;
    case 'frunk': vs.ft = 1; break;
    case 'trunk': vs.rt = vs.rt ? 0 : 1; break;
    case 'climate_on': cl.is_climate_on = true; break;
    case 'climate_off': cl.is_climate_on = false; cl.is_front_defroster_on = false; break;
    case 'set_temp': cl.driver_temp_setting = value; break;
    case 'defrost_on': cl.is_front_defroster_on = true; cl.is_climate_on = true; break;
    case 'defrost_off': cl.is_front_defroster_on = false; break;
    case 'seat': cl.seat_heater_left = value; break;
  }
  localStorage.setItem('tr_last', JSON.stringify({ at: c.at || Date.now(), d: d }));
  send(statusMsg(d, c.at, STATE.ONLINE, ''));
}

var refreshTimer = null;
function runCommand(id, value) {
  var name = CMDS[id];
  if (!name) return send({ Type: TYPE.RESULT, Cmd: id, Value: 0, Text: 'Unknown command' });
  var arg = value;
  if (name === 'set_temp') arg = value / 10;
  T.command(name, arg, progress, function (e) {
    if (e) {
      send({ Type: TYPE.RESULT, Cmd: id, Value: 0, Text: e.text });
      var st = stateForError(e);
      if (st === STATE.PAIR || st === STATE.SETUP) sendCached(st, e.text);
      return;
    }
    send({ Type: TYPE.RESULT, Cmd: id, Value: 1, Text: 'Done' });
    optimistic(name, arg);
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(function () { refresh(false); }, 4000);
  });
}

// ---------- Pebble events ----------
Pebble.addEventListener('ready', function () {
  if (!configured()) return sendCached(STATE.SETUP, 'Open settings on phone');
  sendCached(STATE.BUSY, '');
  refresh(false);
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Type === TYPE.CMD) runCommand(p.Cmd, p.Value || 0);
  else if (p.Type === TYPE.REFRESH) refresh(p.Value === 1);
});

Pebble.addEventListener('showConfiguration', function () {
  var c = T.cfg(), tk = T.tokens();
  var current = {
    clientId: c.clientId || '', hasSecret: !!c.clientSecret, domain: c.domain || '',
    region: c.region || 'na', pubHex: c.pubHex || '', signedIn: !!tk.refresh,
    vehicles: c.vehicles || [], vin: c.vin || '', carName: c.carName || '',
    tempUnit: c.tempUnit || '', distUnit: c.distUnit || '', registerError: c.registerError || '',
    bigint: C.haveBigInt()
  };
  Pebble.openURL(CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(current)));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var r;
  try { r = JSON.parse(decodeURIComponent(e.response)); } catch (x) {
    try { r = JSON.parse(e.response); } catch (y) { return; }
  }
  var c = T.cfg();
  ['clientId', 'domain', 'region', 'redirectUri', 'vin', 'carName', 'tempUnit', 'distUnit'].forEach(function (k) {
    if (r[k] !== undefined) c[k] = r[k];
  });
  if (r.clientSecret) c.clientSecret = r.clientSecret;
  if (r.region) c.base = T.FLEET[r.region === 'eu' ? 'eu' : 'na'];
  if (r.privHex) {
    var keyChanged = r.privHex !== c.privHex;
    c.privHex = r.privHex;
    try { c.pubHex = C.bytesToHex(C.publicFromPrivate(r.privHex)); } catch (x) { c.pubHex = r.pubHex || ''; }
    if (keyChanged) (c.vehicles || []).concat([{ vin: c.vin }]).forEach(function (v) {
      localStorage.removeItem('tr_sess_' + v.vin + '_2'); localStorage.removeItem('tr_sess_' + v.vin + '_3');
    });
  }
  if (r.signOut) { localStorage.removeItem('tr_tokens'); c.vin = ''; c.vehicles = []; }
  T.saveCfg(c);
  if (r.code) {
    T.completeSignIn(r.code, progress, function (err) {
      if (err) return sendCached(stateForError(err), err.text);
      refresh(false);
    });
  } else {
    refresh(false);
  }
});
