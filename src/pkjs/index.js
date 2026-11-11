// Tesla Remote - phone side. Talks to the watch and to Tesla (car-server calls go through the relay).
var T = require('./tesla.js');
var C = require('./crypto.js');

// Where the setup page is hosted (static page, see /docs in the repo).
var CONFIG_URL = 'https://tombolger.github.io/tesla-remote/';

var TYPE = { STATUS: 1, CMD: 2, RESULT: 3, REFRESH: 4, PROGRESS: 5, SETTING: 6, MENU_REQ: 7, MENU: 8, ACTION: 9, MENU_CLOSE: 10 };
// Watch button commands, by number (matches src/c/main.c)
var CMDS = [null, 'lock', 'unlock', 'frunk', 'trunk', 'climate_on', 'climate_off', 'set_temp',
  'defrost_on', 'defrost_off', 'seat', 'flash', 'honk', 'cool', 'wheel_on', 'wheel_off', 'vent', 'close_windows',
  'set_temps', 'trunk_close', 'tonneau_open', 'tonneau_close', 'tonneau_stop'];
var STATE = { UNKNOWN: 0, ONLINE: 1, ASLEEP: 2, OFFLINE: 3, ERROR: 4, SETUP: 5, PAIR: 6, BUSY: 7 };
var NO_TEMP = -1000;
var VERSION = '1.0.0';

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

// ---------- distance & direction to the car ----------
var phonePos = null;
function updatePhonePos(cb) {
  if (!navigator.geolocation) return cb && cb();
  navigator.geolocation.getCurrentPosition(function (p) {
    phonePos = { lat: p.coords.latitude, lon: p.coords.longitude }; if (cb) cb();
  }, function () { if (cb) cb(); }, { enableHighAccuracy: true, maximumAge: 30000, timeout: 10000 });
}
function rad(x) { return x * Math.PI / 180; }
function carVector(d) {
  var ds = (d && d.drive_state) || {};
  if (!phonePos || typeof ds.latitude !== 'number') return { dist: -1, bearing: 0 };
  var R = 6371000, la1 = rad(phonePos.lat), la2 = rad(ds.latitude), dl = rad(ds.longitude - phonePos.lon);
  var a = Math.pow(Math.sin((la2 - la1) / 2), 2) + Math.cos(la1) * Math.cos(la2) * Math.pow(Math.sin(dl / 2), 2);
  var dist = 2 * R * Math.asin(Math.min(1, Math.sqrt(a)));
  var y = Math.sin(dl) * Math.cos(la2), x = Math.cos(la1) * Math.sin(la2) - Math.sin(la1) * Math.cos(la2) * Math.cos(dl);
  return { dist: Math.round(dist), bearing: Math.round((Math.atan2(y, x) * 180 / Math.PI + 360) % 360) };
}

// ---------- status ----------
function num(v, d) { return typeof v === 'number' ? v : (v === true ? 1 : (v === false ? 0 : d)); }
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
  var c = T.cfg(), g = (d && d.gui_settings) || {};
  var f = c.tempUnit ? c.tempUnit === 'F' : g.gui_temperature_units === 'F';
  var mi = c.distUnit ? c.distUnit === 'mi' : (g.gui_distance_units ? /mi/.test(g.gui_distance_units) : true);
  return (f ? 1 : 0) | (mi ? 2 : 0);
}
// The car reports a rear-centre heater value even on cars without one (like the Model 3).
function hasRearCenter(d) {
  var cl = d.climate_state || {}, vc = d.vehicle_config || {};
  if (typeof cl.seat_heater_rear_center !== 'number') return false;
  return modelCode(vc.car_type) !== 2;
}
function hasThirdRow(d) {
  var cl = d.climate_state || {}, vc = d.vehicle_config || {};
  if (typeof cl.seat_heater_third_row_left === 'number') return true;
  var t = String(vc.third_row_seats || '');
  return !!t && !/none|unknown/i.test(t);
}
// Which seat map to draw: 0 the model's 5-seat map, 1 Model Y 7-seat, 2 Model X 6-seat, 3 Model X 7-seat.
function seatLayout(d) {
  var m = modelCode((d.vehicle_config || {}).car_type);
  if (m === 4 && hasThirdRow(d)) return 1;
  if (m === 3) return hasRearCenter(d) || !hasThirdRow(d) ? 3 : 2;
  return 0;
}
// Gear: 0 parked, 1 drive, 2 reverse, 3 neutral, 4 unknown (awake, someone may be in it), 5 moving (gear unknown).
function gear(d) {
  var ds = d.drive_state || {}, vs = d.vehicle_state || {};
  var g = ({ D: 1, R: 2, N: 3, P: 0 })[ds.shift_state];
  if (g !== undefined) return g;
  if ((ds.speed || 0) > 0) return 1;
  if (ds.moving) return 5;  // moving, gear not reported; speed estimated from GPS
  if (vs.is_user_present === false) return 0;
  return 4;
}
// Cybertruck tonneau: -1 none/unknown, else 0 closed, 1 open, 2 ajar, 5 opening, 6 closing.
function tonneau(d) {
  var vs = d.vehicle_state || {}, t = vs.tonneau_state;
  if (typeof t === 'string') t = ({ closed: 0, open: 1, ajar: 2, opening: 5, closing: 6 })[t.toLowerCase()];
  if (typeof t !== 'number') return -1;
  if (t === 3 || t === 4) return -1;
  return t;
}
function statusMsg(d, at, carState, text) {
  d = d || {};
  var ch = d.charge_state || {}, cl = d.climate_state || {}, vs = d.vehicle_state || {}, vc = d.vehicle_config || {};
  var units = unitsFlags(d);
  var rangeMi = typeof ch.battery_range === 'number' ? ch.battery_range : 0;
  var charging = { Disconnected: 0, Stopped: 1, Charging: 2, Complete: 3 }[ch.charging_state];
  var cfg = T.cfg();
  // A sleeping or offline car can't be running climate, seat heat or charging: never show stale "on" states.
  var live = carState === STATE.ONLINE || carState === STATE.BUSY;
  function on(v) { return live && !!v; }
  function lvl(v) { return live ? num(v, 0) : 0; }
  var vec = carVector(d);
  return {
    Type: TYPE.STATUS,
    CarState: carState,
    Text: (text || '').slice(0, 40),
    HasData: d.charge_state ? 1 : 0,
    Battery: ch.battery_level || 0,
    Range: Math.round(units & 2 ? rangeMi : rangeMi * 1.609344),
    Charging: charging === undefined ? 0 : (charging === 2 && !live ? 1 : charging),
    ChargeLimit: ch.charge_limit_soc || 0,
    MinutesToFull: ch.minutes_to_full_charge || 0,
    Locked: vs.locked ? 1 : 0,
    Frunk: vs.ft ? 1 : 0,
    Trunk: vs.rt ? 1 : 0,
    Climate: on(cl.is_climate_on) ? 1 : 0,
    InTemp: t10(cl.inside_temp),
    OutTemp: t10(cl.outside_temp),
    SetTemp: t10(typeof cl.driver_temp_setting === 'number' ? cl.driver_temp_setting : 21),
    PassTemp: t10(typeof cl.passenger_temp_setting === 'number' ? cl.passenger_temp_setting : cl.driver_temp_setting),
    Defrost: on(cl.is_front_defroster_on || num(cl.defrost_mode, 0) > 0) ? 1 : 0,
    RearDefrost: on(cl.is_rear_defroster_on) ? 1 : 0,
    SeatHeat: lvl(cl.seat_heater_left),
    SeatR: lvl(cl.seat_heater_right),
    SeatRL: lvl(cl.seat_heater_rear_left),
    SeatRC: lvl(cl.seat_heater_rear_center),
    SeatRR: lvl(cl.seat_heater_rear_right),
    HasRear: typeof cl.seat_heater_rear_left === 'number' ? 1 : 0,
    HasRearC: hasRearCenter(d) ? 1 : 0,
    CoolL: lvl(cl.seat_fan_front_left),
    CoolR: lvl(cl.seat_fan_front_right),
    CanCool: typeof cl.seat_fan_front_left === 'number' ? 1 : 0,
    Wheel: on(cl.steering_wheel_heater || num(cl.steering_wheel_heat_level, 0) > 0) ? 1 : 0,
    Windows: (vs.fd_window || vs.fp_window || vs.rd_window || vs.rp_window) ? 1 : 0,
    Units: units,
    Model: modelCode(vc.car_type),
    Age: at ? Math.max(0, Math.round((Date.now() - at) / 1000)) : 0,
    CarName: String(cfg.carName || vs.vehicle_name || d.display_name || 'Tesla').slice(0, 24),
    Split: cfg.split ? 1 : 0,
    // gear: 0 parked/unknown, 1 drive, 2 reverse, 3 neutral; speed in the user's units
    Shift: live ? gear(d) : 0,
    Seat3L: lvl(cl.seat_heater_third_row_left),
    Seat3R: lvl(cl.seat_heater_third_row_right),
    Has3rd: hasThirdRow(d) ? 1 : 0,
    Layout: seatLayout(d),
    Tonneau: tonneau(d),
    Speed: Math.round(((d.drive_state || {}).speed || (d.drive_state || {}).est_speed || 0) * (units & 2 ? 1 : 1.609344)),
    Dist: vec.dist,
    Bearing: vec.bearing
  };
}
function cached() { return T.getJSON('tr_last') || { at: 0, d: {} }; }
var lastState = STATE.UNKNOWN;
// Every status update goes through publish(): the watch's own screens redraw from it, and every menu
// page the watch has open is rebuilt from the same data, so no screen can show an older value.
function publish(d, at, state, text) {
  lastState = state;
  send(statusMsg(d, at, state, text));
  resendMenus();
}
function sendCached(state, text) { var c = cached(); publish(c.d, c.at, state, text); }
function stateForError(e) {
  if (!e) return STATE.ERROR;
  if (e.code === 'setup' || e.code === 'novehicle') return STATE.SETUP;
  if (e.code === 'asleep') return STATE.ASLEEP;
  if (e.code === 'fault' && (e.extra === 3 || e.extra === 4)) return STATE.PAIR;
  return STATE.ERROR;
}
function configured() { var c = T.cfg(); return !!(c.clientId && c.vin && (T.tokens().refresh || T.tokens().access)); }

function refresh(forceWake, done) {
  done = done || function () {};
  if (!configured()) { sendCached(STATE.SETUP, 'Open settings on phone'); return done(); }
  function getData() {
    T.vehicleData(function (e, d) {
      if (e) { sendCached(stateForError(e), e.text); return done(); }
      publish(d, Date.now(), STATE.ONLINE, '');
      watchDriving(d);
      done();
    });
  }
  if (forceWake) {
    send({ Type: TYPE.PROGRESS, Text: 'Waking car' });
    return T.wake(progress, function (we) {
      if (we) { sendCached(stateForError(we), we.text); return done(); }
      getData();
    });
  }
  // Checking if the car is awake is cheap; a sleeping car can't report full data.
  T.vehicleState(function (e, st) {
    if (e) { sendCached(stateForError(e), e.text); return done(); }
    if (st === 'online') return getData();
    sendCached(st === 'offline' ? STATE.OFFLINE : STATE.ASLEEP, '');
    done();
  });
}

// While the car is being driven, keep the watch current: one refresh a minute, only while the app is open.
var driveTimer = null;
function watchDriving(d) {
  clearTimeout(driveTimer); driveTimer = null;
  var g = gear(d);
  // in use but gear unknown: check again soon so movement can be measured from two GPS fixes
  var wait = g === 4 ? 20000 : 60000;
  if (g >= 1) driveTimer = setTimeout(function () { updatePhonePos(function () { refresh(false); }); }, wait);
}

// Once the car has accepted a command, show its result everywhere right away. The follow-up
// reads below then confirm it (or correct it) with what the car actually reports.
function applyConfirmed(name, arg) {
  var c = cached(), d = c.d || {};
  if (!d.charge_state) return;
  var ch = d.charge_state = d.charge_state || {}, cl = d.climate_state = d.climate_state || {};
  var vs = d.vehicle_state = d.vehicle_state || {};
  var seatKeys = ['seat_heater_left', 'seat_heater_right', 'seat_heater_rear_left', 'seat_heater_rear_center',
    'seat_heater_rear_right', 'seat_heater_third_row_left', 'seat_heater_third_row_right'];
  function windows(v) { vs.fd_window = vs.fp_window = vs.rd_window = vs.rp_window = v; }
  switch (name) {
    case 'lock': vs.locked = true; break;
    case 'unlock': vs.locked = false; break;
    case 'auto_secure': vs.locked = true; windows(0); break;
    case 'frunk': vs.ft = 1; break;
    case 'trunk': vs.rt = 1; break;
    case 'trunk_close': vs.rt = 0; break;
    case 'tonneau_open': vs.tonneau_state = 5; break;
    case 'tonneau_close': vs.tonneau_state = 6; break;
    case 'climate_on': cl.is_climate_on = true; break;
    case 'climate_off':
      cl.is_climate_on = false; cl.is_front_defroster_on = false; cl.defrost_mode = 0; break;
    case 'defrost_on':
      cl.is_climate_on = true; cl.is_front_defroster_on = true; cl.is_rear_defroster_on = true; cl.defrost_mode = 2; break;
    case 'defrost_off': cl.is_front_defroster_on = false; cl.defrost_mode = 0; break;
    case 'set_temp': cl.driver_temp_setting = arg; cl.passenger_temp_setting = arg; break;
    case 'set_temps': cl.driver_temp_setting = arg[0]; cl.passenger_temp_setting = arg[1]; break;
    case 'seat': cl[seatKeys[Math.min(6, (arg >> 4) & 15)]] = arg & 15; break;
    case 'cool': cl[((arg >> 4) & 15) === 1 ? 'seat_fan_front_right' : 'seat_fan_front_left'] = arg & 15; break;
    case 'wheel_on': case 'wheel_off':
      cl.steering_wheel_heater = name === 'wheel_on';
      if (typeof cl.steering_wheel_heat_level === 'number') cl.steering_wheel_heat_level = name === 'wheel_on' ? 3 : 0;
      break;
    case 'vent': windows(1); break;
    case 'close_windows': windows(0); break;
    case 'charge_start': if (ch.charging_state && ch.charging_state !== 'Disconnected') ch.charging_state = 'Charging'; break;
    case 'charge_stop': if (ch.charging_state === 'Charging') ch.charging_state = 'Stopped'; break;
    case 'charge_limit': ch.charge_limit_soc = arg; break;
    case 'charge_amps': ch.charge_current_request = arg; break;
    case 'port_open': ch.charge_port_door_open = true; break;
    case 'port_close': ch.charge_port_door_open = false; break;
    case 'sentry': vs.sentry_mode = !!arg; break;
    case 'guest': vs.guest_mode_enabled = !!arg; break;
    case 'keeper': cl.climate_keeper_mode = ['off', 'on', 'dog', 'camp'][arg] || 'off'; if (arg) cl.is_climate_on = true; break;
    case 'cop': cl.cabin_overheat_protection = ['Off', 'On', 'FanOnly'][arg] || 'Off'; break;
    case 'cop_temp': cl.cop_activation_temperature = ['', 'Low', 'Medium', 'High'][arg] || cl.cop_activation_temperature; break;
    case 'bioweapon': cl.bioweapon_mode = !!arg; break;
    case 'auto_seat': cl[Math.floor(arg / 10) === 1 ? 'auto_seat_climate_left' : 'auto_seat_climate_right'] = !!(arg % 10); break;
    default: return;
  }
  T.setJSON('tr_last', { at: Date.now(), d: d });
  publish(d, Date.now(), STATE.ONLINE, '');
}

// After every command, read the car's real state 3 s, 13 s and 43 s later. The watch only
// ever shows what the car reports, never a guess.
var followUps = [];
function scheduleFollowUps() {
  followUps.forEach(clearTimeout);
  followUps = [3000, 13000, 43000].map(function (ms) {
    return setTimeout(function () { refresh(false); }, ms);
  });
}

// ---------- commands ----------
function runNamed(name, arg, cmdId) {
  if (!T.COMMANDS[name]) return send({ Type: TYPE.RESULT, Cmd: cmdId || 0, Value: 0, Text: 'Unknown command' });
  T.command(name, arg, progress, function (e) {
    if (e) {
      send({ Type: TYPE.RESULT, Cmd: cmdId || 0, Value: 0, Text: e.text });
      var st = stateForError(e), cfg = T.cfg();
      if (st === STATE.PAIR) { cfg.needPair = true; cfg.paired = false; T.saveCfg(cfg); sendCached(st, e.text); }
      else if (st === STATE.SETUP) sendCached(st, e.text);
    } else {
      var c = T.cfg();
      if (!c.paired || c.needPair) { c.paired = true; c.needPair = false; T.saveCfg(c); }
      send({ Type: TYPE.RESULT, Cmd: cmdId || 0, Value: 1, Text: 'Done' });
      applyConfirmed(name, arg);
    }
    scheduleFollowUps();
  });
}
function runCommand(id, value) {
  var name = CMDS[id], arg = value;
  if (name === 'set_temp') arg = value / 10;
  if (name === 'set_temps') arg = [((value >> 16) & 0xffff) / 10, (value & 0xffff) / 10];
  runNamed(name, arg, id);
}

// ---------- "More" menus, built here and shown by the watch ----------
// Each row: title, subtitle, action.  Actions: "p:page" open page, "c:cmd:arg" run,
// "k:Question?:cmd:arg" confirm first, "w:..." handled on the watch, "" info only.
var openPages = [];  // the watch's menu stack, bottom first, e.g. ['root', 'charging', 'limit']
function row(t, s, a) { return [String(t).slice(0, 28), String(s || '').slice(0, 34), a || '']; }
function onOff(v) { return v ? 'On' : 'Off'; }
function hm(mins) { mins = Math.round(mins || 0); return mins >= 60 ? Math.floor(mins / 60) + 'h ' + (mins % 60) + 'm' : mins + ' min'; }
function tempStr(c, units) {
  if (typeof c !== 'number') return '--';
  return (units & 1) ? Math.round(c * 9 / 5 + 32) + '°' : (Math.round(c * 2) / 2) + '°';
}
function pressure(bar, units) {
  if (typeof bar !== 'number') return '--';
  return (units & 1) ? Math.round(bar * 14.5038) + ' psi' : bar.toFixed(1) + ' bar';
}
function dist(mi, units) { return (units & 2) ? mi.toFixed(1) + ' mi' : (mi * 1.609344).toFixed(1) + ' km'; }

// Cybertruck: the tailgate opens remotely but must be closed by hand; the tonneau is powered.
function cargoRows(d, rows) {
  var vs = d.vehicle_state || {};
  // Cybertruck: the tailgate opens remotely but must be closed by hand; the tonneau is powered.
  rows.push(vs.rt ? row('Tailgate is open', 'Close it by hand', '') : row('Open tailgate', '', 'k:Open tailgate?:trunk'));
  var tn = tonneau(d), tp = vs.tonneau_percent_open;
  var tSub = ({ 0: 'Closed', 1: 'Open', 2: 'Partly open', 5: 'Opening', 6: 'Closing' })[tn] || 'Unknown';
  if (typeof tp === 'number' && tp > 0 && tp < 100) tSub += ' · ' + tp + '%';
  if (tn === 5 || tn === 6) rows.push(row('Stop tonneau', tSub, 'c:tonneau_stop'));
  rows.push(row('Open tonneau', tn === 1 ? 'Tonneau is open' : tSub, 'k:Open tonneau?:tonneau_open'));
  rows.push(row('Close tonneau', tn === 0 ? 'Tonneau is closed' : tSub, 'k:Close tonneau?:tonneau_close'));
}
function buildPage(page, cb) {
  var d = cached().d || {}, ch = d.charge_state || {}, cl = d.climate_state || {}, vs = d.vehicle_state || {};
  var u = unitsFlags(d), cfg = T.cfg(), rows = [], title = 'More', sel = 0;
  var live = lastState === STATE.ONLINE;
  var plugged = ch.charging_state && ch.charging_state !== 'Disconnected';
  switch (page) {
    case 'root':
      title = 'More';
      rows.push(row('Charging', ch.charging_state === 'Charging' ? 'Charging · ' + hm(ch.minutes_to_full_charge) + ' left'
        : (plugged ? 'Plugged in · limit ' + (ch.charge_limit_soc || '--') + '%' : 'Limit ' + (ch.charge_limit_soc || '--') + '%'), 'p:charging'));
      rows.push(row('Climate', cl.is_climate_on && live ? 'On · ' + tempStr(cl.driver_temp_setting, u) : 'Off', 'p:climate'));
      rows.push(row('Security', 'Sentry ' + onOff(vs.sentry_mode) + ' · ' + (vs.locked ? 'Locked' : 'Unlocked'), 'p:security'));
      rows.push(row('Controls', 'Lights, horn, windows, lids', 'p:controls'));
      rows.push(row('Car info', 'Tires, odometer, software', 'p:info'));
      rows.push(row('Car power', 'Low power, accessory power', 'p:power'));
      rows.push(row('Wake & refresh', live ? 'Car is awake' : 'Car is asleep', 'w:refresh'));
      break;
    case 'charging':
      title = 'Charging';
      if (ch.charging_state === 'Charging') rows.push(row('Stop charging', (ch.charger_power || 0) + ' kW · ' + hm(ch.minutes_to_full_charge) + ' left', 'c:charge_stop'));
      else rows.push(row('Start charging', plugged ? (ch.charging_state || '') : 'Not plugged in', 'c:charge_start'));
      rows.push(row('Charge limit', (ch.charge_limit_soc || '--') + '%', 'p:limit'));
      rows.push(row('Charging amps', (ch.charge_current_request || '--') + ' A' + (ch.charge_current_request_max ? ' of ' + ch.charge_current_request_max : ''), 'p:amps'));
      rows.push(ch.charge_port_door_open ? row('Close charge port', 'Port is open', 'c:port_close') : row('Open charge port', 'Port is closed', 'c:port_open'));
      rows.push(row('Nearby chargers', 'Superchargers & destination', 'p:chargers'));
      if (ch.charging_state === 'Charging') {
        rows.push(row('Rate', (ch.charger_power || 0) + ' kW · ' + (ch.charger_actual_current || 0) + ' A', ''));
        rows.push(row('Energy added', (ch.charge_energy_added || 0) + ' kWh', ''));
      }
      break;
    case 'limit':
      title = 'Charge limit';
      sel = 4;  // open on 80%
      [100, 95, 90, 85, 80, 75, 70, 60, 50].forEach(function (p) {
        rows.push(row(p + '%', p === ch.charge_limit_soc ? 'Current' : '', 'c:charge_limit:' + p));
      });
      break;
    case 'amps':
      title = 'Charging amps';
      var max = ch.charge_current_request_max || 48;
      [5, 8, 10, 12, 16, 20, 24, 28, 32, 40, 48].filter(function (a) { return a <= max; }).forEach(function (a) {
        rows.push(row(a + ' A', a === ch.charge_current_request ? 'Current' : '', 'c:charge_amps:' + a));
      });
      break;
    case 'chargers':
      title = 'Nearby chargers';
      return T.nearbyChargers(function (e, r) {
        if (e) return cb(title, [row('Could not load', e.text, '')]);
        (r.superchargers || []).slice(0, 8).forEach(function (s) {
          rows.push(row(s.name, dist(s.distance_miles || 0, u) + ' · ' + (s.available_stalls || 0) + '/' + (s.total_stalls || 0) + ' free', ''));
        });
        (r.destination_charging || []).slice(0, 5).forEach(function (s) {
          rows.push(row(s.name, dist(s.distance_miles || 0, u) + ' · destination', ''));
        });
        if (!rows.length) rows.push(row('None found', '', ''));
        cb(title, rows);
      });
    case 'climate':
      title = 'Climate';
      rows.push(row(cl.is_climate_on && live ? 'Turn climate off' : 'Turn climate on', tempStr(cl.driver_temp_setting, u), cl.is_climate_on && live ? 'c:climate_off' : 'c:climate_on'));
      rows.push(row('Defrost car', onOff(live && cl.is_front_defroster_on), live && cl.is_front_defroster_on ? 'c:defrost_off' : 'c:defrost_on'));
      rows.push(row('Split temperature', cfg.split ? 'On · driver & passenger' : 'Off', 'w:split'));
      rows.push(row('Keep climate', ({ off: 'Off', on: 'Keep', dog: 'Dog', camp: 'Camp' })[cl.climate_keeper_mode] || 'Off', 'p:keeper'));
      rows.push(row('Cabin overheat', ({ On: 'On', Off: 'Off', FanOnly: 'Fan only' })[cl.cabin_overheat_protection] || 'Off' + (cl.cop_activation_temperature ? ' · ' + cl.cop_activation_temperature : ''), 'p:cop'));
      rows.push(row('Auto seat (driver)', onOff(cl.auto_seat_climate_left), 'c:auto_seat:' + (cl.auto_seat_climate_left ? 10 : 11)));
      rows.push(row('Auto seat (passenger)', onOff(cl.auto_seat_climate_right), 'c:auto_seat:' + (cl.auto_seat_climate_right ? 20 : 21)));
      if (typeof cl.bioweapon_mode === 'boolean') rows.push(row('Bioweapon defense', onOff(cl.bioweapon_mode), 'c:bioweapon:' + (cl.bioweapon_mode ? 0 : 1)));
      break;
    case 'keeper':
      title = 'Keep climate';
      [['Off', 0, 'off'], ['Keep', 1, 'on'], ['Dog mode', 2, 'dog'], ['Camp mode', 3, 'camp']].forEach(function (k) {
        rows.push(row(k[0], cl.climate_keeper_mode === k[2] ? 'Current' : '', 'c:keeper:' + k[1]));
      });
      break;
    case 'cop':
      title = 'Cabin overheat';
      rows.push(row('Off', cl.cabin_overheat_protection === 'Off' ? 'Current' : '', 'c:cop:0'));
      rows.push(row('On (A/C)', cl.cabin_overheat_protection === 'On' ? 'Current' : '', 'c:cop:1'));
      rows.push(row('Fan only', cl.cabin_overheat_protection === 'FanOnly' ? 'Current' : '', 'c:cop:2'));
      rows.push(row('Start at: Low', 'about 30°C / 86°F', 'c:cop_temp:1'));
      rows.push(row('Start at: Medium', 'about 35°C / 95°F', 'c:cop_temp:2'));
      rows.push(row('Start at: High', 'about 40°C / 104°F', 'c:cop_temp:3'));
      break;
    case 'security':
      title = 'Security';
      rows.push(row('Sentry Mode', onOff(vs.sentry_mode), 'c:sentry:' + (vs.sentry_mode ? 0 : 1)));
      rows.push(vs.locked ? row('Unlock', 'Car is locked', 'k:Unlock car?:unlock') : row('Lock', 'Car is unlocked', 'c:lock'));
      rows.push(row('Close windows & lock', 'Auto-secure', 'c:auto_secure'));
      rows.push(row('Guest Mode', typeof vs.guest_mode_enabled === 'boolean' ? onOff(vs.guest_mode_enabled) : '',
        'c:guest:' + (vs.guest_mode_enabled ? 0 : 1)));
      break;
    case 'controls':
      title = 'Controls';
      rows.push(row('Flash lights', '', 'c:flash'));
      rows.push(row('Honk horn', '', 'k:Honk the horn?:honk'));
      rows.push(row('Vent windows', '', 'c:vent'));
      rows.push(row('Close windows', '', 'c:close_windows'));
      rows.push(row('Open frunk', vs.ft ? 'Frunk is open' : '', 'k:Open frunk?:frunk'));
      if (modelCode((d.vehicle_config || {}).car_type) === 5) cargoRows(d, rows);
      else rows.push(vs.rt ? row('Close trunk', 'Trunk is open', 'k:Close trunk?:trunk_close') : row('Open trunk', '', 'k:Open trunk?:trunk'));
      break;
    case 'cargo':
      title = 'Cargo';
      cargoRows(d, rows);
      break;

    case 'info':
      title = 'Car info';
      rows.push(row('Tires front', pressure(vs.tpms_pressure_fl, u) + ' / ' + pressure(vs.tpms_pressure_fr, u),
        (vs.tpms_soft_warning_fl || vs.tpms_soft_warning_fr) ? '' : ''));
      rows.push(row('Tires rear', pressure(vs.tpms_pressure_rl, u) + ' / ' + pressure(vs.tpms_pressure_rr, u), ''));
      if (typeof vs.odometer === 'number') rows.push(row('Odometer', (u & 2) ? Math.round(vs.odometer).toLocaleString() + ' mi' : Math.round(vs.odometer * 1.609344).toLocaleString() + ' km', ''));
      rows.push(row('Software', vs.car_version ? String(vs.car_version).split(' ')[0] : '--', ''));
      var su = vs.software_update || {};
      if (su.status) rows.push(row('Update', su.status + (su.version ? ' · ' + su.version : ''), ''));
      var doors = [['Driver door', vs.df], ['Passenger door', vs.pf], ['Rear left door', vs.dr], ['Rear right door', vs.pr]]
        .filter(function (x) { return x[1]; }).map(function (x) { return x[0]; });
      rows.push(row('Doors', doors.length ? doors.join(', ') + ' open' : 'All closed', ''));
      rows.push(row('Windows', (vs.fd_window || vs.fp_window || vs.rd_window || vs.rp_window) ? 'Open' : 'Closed', ''));
      rows.push(row('Inside / outside', tempStr(cl.inside_temp, u) + ' / ' + tempStr(cl.outside_temp, u), ''));
      if (typeof vs.is_user_present === 'boolean') rows.push(row('Someone in car', vs.is_user_present ? 'Yes' : 'No', ''));
      var at = cached().at;
      rows.push(row('Last update', at ? hm((Date.now() - at) / 60000) + ' ago' : 'Never', ''));
      var ns = T.getJSON('tr_nostate');
      if (ns && ns.at > Date.now() - 86400000) rows.push(row('Gear & speed', 'Estimated from GPS', ''));
      rows.push(row('Tesla Remote', 'Version ' + VERSION + ' · free & open source', ''));
      break;
    case 'power':
      title = 'Car power';
      rows.push(row('Low power mode on', 'Saves battery when parked long', 'c:low_power:1'));
      rows.push(row('Low power mode off', '', 'c:low_power:0'));
      rows.push(row('Keep accessory power on', 'Powers USB & 12 V after exit', 'c:accessory:1'));
      rows.push(row('Keep accessory power off', '', 'c:accessory:0'));
      break;
    default:
      rows.push(row('Unknown page', page, ''));
  }
  cb(title, rows, sel);
}
function sendPage(page) {
  buildPage(page, function (title, rows, sel) {
    send({ Type: TYPE.MENU, MenuPage: page, MenuTitle: title, MenuSel: sel || 0,
      MenuRows: rows.slice(0, 20).map(function (r) { return r.join('\t'); }).join('\n') });
  });
}
function resendMenus() {
  openPages.slice().reverse().forEach(function (pg) { if (pg !== 'chargers') sendPage(pg); });
}
function runAction(a) {
  var parts = String(a).split(':');
  if (parts[0] === 'c') return runNamed(parts[1], parts[2] !== undefined ? Number(parts[2]) : undefined, 0);
}

function stack(t) { return String(t || '').split('|').filter(function (x) { return x; }); }

// ---------- Pebble events ----------
Pebble.addEventListener('ready', function () {
  if (!configured()) return sendCached(STATE.SETUP, 'Open settings on phone');
  sendCached(STATE.BUSY, '');
  updatePhonePos(function () { refresh(false); });
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Type === TYPE.CMD) runCommand(p.Cmd, p.Value || 0);
  else if (p.Type === TYPE.REFRESH) updatePhonePos(function () { refresh(p.Value === 1); });
  else if (p.Type === TYPE.SETTING) { var c = T.cfg(); c.split = !!p.Value; T.saveCfg(c); sendCached(lastState, ''); }
  else if (p.Type === TYPE.MENU_REQ) { openPages = stack(p.Text || 'root'); sendPage(openPages[openPages.length - 1]); }
  else if (p.Type === TYPE.MENU_CLOSE) openPages = stack(p.Text);
  else if (p.Type === TYPE.ACTION) runAction(p.Text || '');
});

Pebble.addEventListener('showConfiguration', function () {
  var c = T.cfg(), tk = T.tokens();
  var current = {
    clientId: c.clientId || '', hasSecret: !!c.clientSecret, domain: c.domain || '',
    region: c.region || 'na', pubHex: c.pubHex || '', signedIn: !!tk.refresh,
    vehicles: c.vehicles || [], vin: c.vin || '', carName: c.carName || '',
    tempUnit: c.tempUnit || '', distUnit: c.distUnit || '', registerError: c.registerError || '',
    paired: !!c.paired, needPair: !!c.needPair, relay: c.relay || '', lastError: c.lastError || '',
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
  ['clientId', 'domain', 'region', 'redirectUri', 'vin', 'carName', 'tempUnit', 'distUnit', 'relay'].forEach(function (k) {
    if (r[k] !== undefined) c[k] = r[k];
  });
  if (r.clientSecret) c.clientSecret = r.clientSecret;
  if (r.region) c.base = T.FLEET[r.region === 'eu' ? 'eu' : 'na'];
  if (r.privHex) {
    var keyChanged = r.privHex !== c.privHex;
    c.privHex = r.privHex;
    try { c.pubHex = C.bytesToHex(C.publicFromPrivate(r.privHex)); } catch (x) { c.pubHex = r.pubHex || ''; }
    if (keyChanged) {
      c.paired = false; c.needPair = false;
      (c.vehicles || []).concat([{ vin: c.vin }]).forEach(function (v) {
        localStorage.removeItem('tr_sess_' + v.vin + '_2'); localStorage.removeItem('tr_sess_' + v.vin + '_3');
      });
    }
  }
  if (r.signOut) { localStorage.removeItem('tr_tokens'); c.vin = ''; c.vehicles = []; }
  if (r.code) c.noLocation = false;  // a fresh sign-in may have added the location permission
  T.saveCfg(c);
  function after(err) {
    var cc = T.cfg(); cc.lastError = err ? (err.text + (err.extra ? ': ' + err.extra : '')) : ''; T.saveCfg(cc);
    if (err) return sendCached(stateForError(err), err.text);
    refresh(false);
  }
  if (r.code) T.completeSignIn(r.code, progress, after);
  else if (r.retryRegister) T.retryRegistration(progress, after);
  else refresh(false);
});
