// Tesla Remote - watch side. Tesla-style dark UI with blue accents.
#include <pebble.h>

// ---------------- protocol (matches src/pkjs/index.js) ----------------
enum { T_STATUS = 1, T_CMD = 2, T_RESULT = 3, T_REFRESH = 4, T_PROGRESS = 5 };
enum { C_LOCK = 1, C_UNLOCK, C_FRUNK, C_TRUNK, C_CLIMATE_ON, C_CLIMATE_OFF, C_SET_TEMP,
       C_DEFROST_ON, C_DEFROST_OFF, C_SEAT, C_FLASH, C_HONK };
enum { S_UNKNOWN = 0, S_ONLINE, S_ASLEEP, S_OFFLINE, S_ERROR, S_SETUP, S_PAIR, S_BUSY };
#define NO_TEMP (-1000)
#define PERSIST_STATUS 1
#define STATUS_VERSION 2

typedef struct {
  uint8_t version;
  int8_t car_state;
  bool has_data, locked, frunk, trunk, climate, defrost;
  uint8_t battery, charging, charge_limit, seat, units, model;
  int16_t range, min_to_full, in_temp, out_temp, set_temp;
  int32_t updated;  // unix time of last real data
  char name[25];
  char text[41];
} Status;

static Status s;

// ---------------- colors ----------------
#define COL_BG GColorBlack
#define COL_TEXT GColorWhite
#define COL_DIM PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COL_FAINT PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
#define COL_ACCENT PBL_IF_COLOR_ELSE(GColorVeryLightBlue, GColorWhite)
#define COL_FILL PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorWhite)
#define COL_CARD PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define COL_CAR PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COL_GOOD PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite)
#define COL_BAD PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)

// ---------------- toast (progress / result banner) ----------------
enum { TOAST_NONE, TOAST_PROGRESS, TOAST_OK, TOAST_ERR };
static int s_toast_kind = TOAST_NONE;
static char s_toast[41];
static AppTimer *s_toast_timer;
static bool s_busy;

static Window *s_main_win, *s_climate_win;
static Layer *s_main_layer, *s_climate_layer;

static void redraw(void) {
  if (s_main_layer) layer_mark_dirty(s_main_layer);
  if (s_climate_layer) layer_mark_dirty(s_climate_layer);
}
static void toast_hide(void *ctx) { s_toast_timer = NULL; s_toast_kind = TOAST_NONE; s_busy = false; redraw(); }
static void toast(int kind, const char *text, uint32_t ms) {
  s_toast_kind = kind;
  strncpy(s_toast, text ? text : "", sizeof(s_toast) - 1);
  s_toast[sizeof(s_toast) - 1] = 0;
  if (s_toast_timer) app_timer_cancel(s_toast_timer);
  s_toast_timer = app_timer_register(ms, toast_hide, NULL);
  redraw();
}

// ---------------- messaging ----------------
static void send3(int type, int cmd, int value) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) { toast(TOAST_ERR, "Phone busy", 2000); return; }
  dict_write_int32(it, MESSAGE_KEY_Type, type);
  dict_write_int32(it, MESSAGE_KEY_Cmd, cmd);
  dict_write_int32(it, MESSAGE_KEY_Value, value);
  app_message_outbox_send();
}
static void send_cmd(int cmd, int value, const char *what) {
  if (s_busy) { vibes_short_pulse(); return; }
  s_busy = true;
  toast(TOAST_PROGRESS, what, 90000);
  send3(T_CMD, cmd, value);
}
static void send_refresh(bool wake) {
  toast(TOAST_PROGRESS, wake ? "Waking car" : "Refreshing", 60000);
  send3(T_REFRESH, 0, wake ? 1 : 0);
}

static int32_t tint(DictionaryIterator *it, uint32_t key, int32_t def) {
  Tuple *t = dict_find(it, key);
  return t ? t->value->int32 : def;
}
static void save_status(void) { persist_write_data(PERSIST_STATUS, &s, sizeof(s)); }

static void inbox(DictionaryIterator *it, void *ctx) {
  int type = tint(it, MESSAGE_KEY_Type, 0);
  Tuple *text = dict_find(it, MESSAGE_KEY_Text);
  if (type == T_STATUS) {
    s.car_state = tint(it, MESSAGE_KEY_CarState, s.car_state);
    bool has = tint(it, MESSAGE_KEY_HasData, 0);
    if (has) {
      s.has_data = true;
      s.battery = tint(it, MESSAGE_KEY_Battery, s.battery);
      s.range = tint(it, MESSAGE_KEY_Range, s.range);
      s.charging = tint(it, MESSAGE_KEY_Charging, s.charging);
      s.charge_limit = tint(it, MESSAGE_KEY_ChargeLimit, s.charge_limit);
      s.min_to_full = tint(it, MESSAGE_KEY_MinutesToFull, s.min_to_full);
      s.locked = tint(it, MESSAGE_KEY_Locked, s.locked);
      s.frunk = tint(it, MESSAGE_KEY_Frunk, s.frunk);
      s.trunk = tint(it, MESSAGE_KEY_Trunk, s.trunk);
      s.climate = tint(it, MESSAGE_KEY_Climate, s.climate);
      s.defrost = tint(it, MESSAGE_KEY_Defrost, s.defrost);
      s.seat = tint(it, MESSAGE_KEY_SeatHeat, s.seat);
      s.in_temp = tint(it, MESSAGE_KEY_InTemp, s.in_temp);
      s.out_temp = tint(it, MESSAGE_KEY_OutTemp, s.out_temp);
      s.set_temp = tint(it, MESSAGE_KEY_SetTemp, s.set_temp);
      s.units = tint(it, MESSAGE_KEY_Units, s.units);
      s.model = tint(it, MESSAGE_KEY_Model, s.model);
      s.updated = time(NULL) - tint(it, MESSAGE_KEY_Age, 0);
    }
    Tuple *name = dict_find(it, MESSAGE_KEY_CarName);
    if (name && name->value->cstring[0]) { strncpy(s.name, name->value->cstring, sizeof(s.name) - 1); }
    strncpy(s.text, text ? text->value->cstring : "", sizeof(s.text) - 1);
    if (s_toast_kind == TOAST_PROGRESS && !s_busy && s.car_state != S_BUSY) toast_hide(NULL);
    save_status();
  } else if (type == T_PROGRESS) {
    toast(TOAST_PROGRESS, text ? text->value->cstring : "Working", 90000);
  } else if (type == T_RESULT) {
    bool ok = tint(it, MESSAGE_KEY_Value, 0) == 1;
    s_busy = false;
    toast(ok ? TOAST_OK : TOAST_ERR, text ? text->value->cstring : (ok ? "Done" : "Failed"), ok ? 1800 : 4000);
    if (ok) vibes_short_pulse(); else vibes_double_pulse();
  }
  redraw();
}

// ---------------- temperature helpers ----------------
static bool is_f(void) { return s.units & 1; }
static int c10_to_disp(int c10) {  // F: whole degrees. C: tenths
  if (is_f()) return (c10 * 9 + (c10 >= 0 ? 25 : -25)) / 50 + 32;
  return c10;
}
static void fmt_temp(char *buf, size_t n, int c10, bool deg) {
  if (c10 == NO_TEMP) { snprintf(buf, n, "--"); return; }
  int v = c10_to_disp(c10);
  if (is_f()) snprintf(buf, n, deg ? "%d°" : "%d", v);
  else if (v % 10 == 0) snprintf(buf, n, deg ? "%d°" : "%d", v / 10);
  else snprintf(buf, n, deg ? "%d.%d°" : "%d.%d", v / 10, abs(v % 10));
}

// ---------------- drawing helpers ----------------
static GFont f14b, f18, f18b, f24b, f28b, f_big;

// Side profile of a crossover, facing left, in a 100 x 30 box.
static const int16_t CAR_TPL[][2] = {
  {3, 27}, {0, 21}, {2, 15}, {27, 10}, {43, 2}, {68, 1}, {87, 8}, {98, 11}, {100, 20}, {98, 27}
};
static const int16_t WIN_TPL[][2] = { {31, 10}, {45, 4}, {66, 3}, {82, 9} };

static GPoint tp(GRect r, int x, int y) { return GPoint(r.origin.x + x * r.size.w / 100, r.origin.y + y * r.size.h / 30); }

static void fill_poly(GContext *ctx, GRect r, const int16_t (*tpl)[2], int n) {
  GPoint pts[12];
  for (int i = 0; i < n; i++) pts[i] = tp(r, tpl[i][0], tpl[i][1]);
  GPathInfo info = { .num_points = n, .points = pts };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// Draw the car. r is the 100x30 template box.
static void draw_car(GContext *ctx, GRect r, bool frunk, bool trunk, bool mini, GColor body, GColor lid, GColor bg) {
  int wr = r.size.h * 6 / 30 + 1;
  graphics_context_set_fill_color(ctx, body);
  fill_poly(ctx, r, CAR_TPL, 10);
  if (!mini) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    fill_poly(ctx, r, WIN_TPL, 4);
  }
  // wheels: cut a black arch then draw the tyre
  GPoint w1 = tp(r, 21, 27), w2 = tp(r, 79, 27);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, w1, wr + (mini ? 1 : 2));
  graphics_fill_circle(ctx, w2, wr + (mini ? 1 : 2));
  graphics_context_set_fill_color(ctx, mini ? body : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_fill_circle(ctx, w1, wr);
  graphics_fill_circle(ctx, w2, wr);
  if (!mini) {
    graphics_context_set_fill_color(ctx, COL_BG);
    graphics_fill_circle(ctx, w1, wr / 2);
    graphics_fill_circle(ctx, w2, wr / 2);
  }
  // open lids in blue
  graphics_context_set_stroke_color(ctx, lid);
  graphics_context_set_stroke_width(ctx, mini ? 2 : 3);
  if (frunk) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, GRect(tp(r, 3, 9).x, tp(r, 0, 9).y, tp(r, 26, 0).x - tp(r, 3, 0).x, tp(r, 0, 16).y - tp(r, 0, 9).y), 0, GCornerNone);
    graphics_draw_line(ctx, tp(r, 27, 10), tp(r, mini ? 4 : 6, mini ? -16 : -2));
  }
  if (trunk) {
    graphics_draw_line(ctx, tp(r, 70, 1), tp(r, mini ? 94 : 97, mini ? -20 : -6));
  }
}

static void draw_lock(GContext *ctx, GPoint c, int sz, bool locked, GColor col, GColor bg) {
  int w = sz, h = sz * 3 / 4;
  GRect body = GRect(c.x - w / 2, c.y - h / 2 + sz / 5, w, h);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 2);
  int sw = w * 6 / 10;
  GRect shackle = GRect(c.x - sw / 2, body.origin.y - sz / 2 - (locked ? 0 : sz / 5), sw, sz);
  graphics_draw_round_rect(ctx, shackle, sw / 2);
  if (!locked) {  // gap on right leg
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, GRect(shackle.origin.x + sw - 2, body.origin.y - sz / 4, 4, sz / 4), 0, GCornerNone);
  }
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, body, 2, GCornersAll);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, GPoint(c.x, body.origin.y + h / 2), 1);
}

static GPoint polar(GPoint c, int32_t a, int d) {
  return GPoint(c.x + sin_lookup(a) * d / TRIG_MAX_RATIO, c.y - cos_lookup(a) * d / TRIG_MAX_RATIO);
}
static void draw_fan(GContext *ctx, GPoint c, int sz, GColor col) {
  int R = sz / 2;
  graphics_context_set_fill_color(ctx, col);
  for (int i = 0; i < 3; i++) {
    int32_t a = i * TRIG_MAX_ANGLE / 3;
    int32_t deg = TRIG_MAX_ANGLE / 360;
    GPoint pts[5] = { polar(c, a - 25 * deg, 2), polar(c, a - 12 * deg, R), polar(c, a + 18 * deg, R + 1),
                      polar(c, a + 50 * deg, R * 2 / 3), polar(c, a + 40 * deg, 2) };
    GPathInfo info = { .num_points = 5, .points = pts };
    GPath *p = gpath_create(&info);
    gpath_draw_filled(ctx, p);
    gpath_destroy(p);
  }
  graphics_fill_circle(ctx, c, 3);
}

static void draw_dots(GContext *ctx, GPoint c, int sz, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  for (int i = -1; i <= 1; i++) graphics_fill_circle(ctx, GPoint(c.x + i * sz / 3, c.y), 2);
}

static void draw_battery(GContext *ctx, GRect r, int pct, bool charging) {
  graphics_context_set_stroke_color(ctx, COL_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, r, 2);
  graphics_context_set_fill_color(ctx, COL_DIM);
  graphics_fill_rect(ctx, GRect(r.origin.x + r.size.w, r.origin.y + r.size.h / 3, 2, r.size.h / 3), 0, GCornerNone);
  int fw = (r.size.w - 4) * pct / 100;
  if (fw < 1 && pct > 0) fw = 1;
  graphics_context_set_fill_color(ctx, charging ? COL_GOOD : (pct <= 20 ? COL_BAD : COL_TEXT));
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 2, fw, r.size.h - 4), 1, GCornersAll);
}

static void text_at(GContext *ctx, const char *t, GFont f, GRect r, GTextAlignment a, GColor col) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, t, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static void draw_toast(GContext *ctx, GRect b) {
  if (s_toast_kind == TOAST_NONE) return;
  int w = b.size.w - PBL_IF_ROUND_ELSE(b.size.w / 3, 12), h = 26;
  int y = b.size.h - h - PBL_IF_ROUND_ELSE(b.size.h / 9, 4);
  GRect r = GRect((b.size.w - w) / 2, y, w, h);
  GColor bg = s_toast_kind == TOAST_OK ? COL_FILL : (s_toast_kind == TOAST_ERR ? COL_BAD : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, r, h / 2, GCornersAll);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s%s", s_toast, s_toast_kind == TOAST_PROGRESS ? "..." : "");
  text_at(ctx, buf, f18b, GRect(r.origin.x + 4, r.origin.y + 1, r.size.w - 8, h), GTextAlignmentCenter, fg);
}

// ---------------- main screen ----------------
enum { B_LOCK, B_CLIMATE, B_FRUNK, B_TRUNK, B_MORE, B_COUNT };
static int s_sel = B_LOCK;

static const char *state_label(void) {
  switch (s.car_state) {
    case S_ONLINE: return s.charging == 2 ? "Charging" : "Parked";
    case S_ASLEEP: return "Asleep";
    case S_OFFLINE: return "Offline";
    case S_SETUP: return "Setup";
    case S_PAIR: return "Pair key";
    case S_BUSY: return "Updating";
    case S_ERROR: return "Error";
    default: return "";
  }
}
static GColor state_color(void) {
  switch (s.car_state) {
    case S_ONLINE: return s.charging == 2 ? COL_GOOD : COL_ACCENT;
    case S_ERROR: case S_PAIR: case S_SETUP: return COL_BAD;
    default: return COL_FAINT;
  }
}
static const char *button_label(int b) {
  switch (b) {
    case B_LOCK: return s.locked ? "Locked" : "Unlocked";
    case B_CLIMATE: return s.climate ? (s.defrost ? "Defrosting" : "Climate on") : "Climate off";
    case B_FRUNK: return s.frunk ? "Frunk open" : "Open frunk";
    case B_TRUNK: return s.trunk ? "Trunk open" : "Open trunk";
    default: return "More";
  }
}
static bool button_active(int b) {
  switch (b) {
    case B_LOCK: return !s.locked;
    case B_CLIMATE: return s.climate;
    case B_FRUNK: return s.frunk;
    case B_TRUNK: return s.trunk;
    default: return false;
  }
}

static void draw_button(GContext *ctx, GPoint c, int r, int b, bool sel) {
  bool active = button_active(b) && s.has_data;
  GColor bg = sel ? COL_FILL : COL_CARD;
  GColor fg = sel ? PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack) : (active ? COL_ACCENT : COL_TEXT);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, c, r);
#ifdef PBL_BW
  if (!sel) { graphics_context_set_stroke_color(ctx, GColorWhite); graphics_context_set_stroke_width(ctx, 1); graphics_draw_circle(ctx, c, r); }
#endif
  int sz = r;
  switch (b) {
    case B_LOCK: draw_lock(ctx, c, sz * 3 / 4, s.locked || !s.has_data, fg, bg); break;
    case B_CLIMATE: draw_fan(ctx, c, sz + 7, fg); break;
    case B_FRUNK: case B_TRUNK: {
      int w = sz * 17 / 10, h = w * 30 / 100;
      GRect cr = GRect(c.x - w / 2, c.y - h / 2 + 4, w, h);
      draw_car(ctx, cr, b == B_FRUNK, b == B_TRUNK, true, fg, fg, bg);
      break;
    }
    default: draw_dots(ctx, c, sz + 2, fg); break;
  }
  if (active && !sel) {
    graphics_context_set_fill_color(ctx, COL_ACCENT);
    graphics_fill_circle(ctx, GPoint(c.x, c.y + r + 4), 2);
  }
}

static void main_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  bool round = PBL_IF_ROUND_ELSE(true, false);
  bool large = W >= 200;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // --- header: car name + state ---
  int hy = round ? H / 14 : 1;
  char hdr[48];
  const char *st = state_label();
  if (round) {
    text_at(ctx, s.name[0] ? s.name : "Tesla", f14b, GRect(W / 5, hy, W * 3 / 5, 18), GTextAlignmentCenter, COL_TEXT);
    snprintf(hdr, sizeof(hdr), "%s", st);
    text_at(ctx, hdr, f14b, GRect(W / 5, hy + 15, W * 3 / 5, 18), GTextAlignmentCenter, state_color());
  } else {
    GSize ss = graphics_text_layout_get_content_size(st, f14b, GRect(0, 0, W, 18), GTextOverflowModeFill, GTextAlignmentRight);
    text_at(ctx, s.name[0] ? s.name : "Tesla", f14b, GRect(4, hy, W - ss.w - 22, 18), GTextAlignmentLeft, COL_TEXT);
    text_at(ctx, st, f14b, GRect(W - 4 - ss.w, hy, ss.w + 2, 18), GTextAlignmentRight, state_color());
    graphics_context_set_fill_color(ctx, state_color());
    graphics_fill_circle(ctx, GPoint(W - 10 - ss.w, hy + 10), 3);
  }

  // --- car ---
  int cw = round ? W * (large ? 52 : 58) / 100 : W * 72 / 100;
  int ch = cw * 30 / 100;
  int cy = round ? H * 32 / 100 : (large ? 36 : 28);
  GRect car = GRect((W - cw) / 2, cy, cw, ch);
  draw_car(ctx, car, s.frunk && s.has_data, s.trunk && s.has_data, false, COL_CAR, COL_ACCENT, COL_BG);
  // lock badge next to the car
  draw_lock(ctx, GPoint(car.origin.x + cw / 2, cy - (large ? 8 : 6)), large ? 9 : 7, s.locked || !s.has_data,
            s.locked || !s.has_data ? COL_DIM : COL_ACCENT, COL_BG);

  // --- battery + range ---
  int ry = cy + ch + ch / 8 + (round ? 2 : (large ? 6 : 3));
  char num[16], unit[8];
  if (s.has_data) snprintf(num, sizeof(num), "%d", s.range); else snprintf(num, sizeof(num), "--");
  snprintf(unit, sizeof(unit), "%s", (s.units & 2) ? "mi" : "km");
  GFont nf = large ? f28b : f24b;
  GSize ns = graphics_text_layout_get_content_size(num, nf, GRect(0, 0, W, 40), GTextOverflowModeFill, GTextAlignmentLeft);
  GSize us = graphics_text_layout_get_content_size(unit, f18, GRect(0, 0, W, 30), GTextOverflowModeFill, GTextAlignmentLeft);
  int bw = large ? 26 : 20, bh = large ? 12 : 10;
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", s.battery);
  GSize ps = graphics_text_layout_get_content_size(pct, f14b, GRect(0, 0, W, 20), GTextOverflowModeFill, GTextAlignmentLeft);
  int total = ns.w + 2 + us.w + 10 + bw + 4 + ps.w;
  int x = (W - total) / 2;
  int nh = large ? 28 : 24;
  text_at(ctx, num, nf, GRect(x, ry - nh / 4 - 2, ns.w + 2, nh + 8), GTextAlignmentLeft, COL_TEXT);
  x += ns.w + 2;
  text_at(ctx, unit, f18, GRect(x, ry + 2, us.w + 2, 22), GTextAlignmentLeft, COL_DIM);
  x += us.w + 10;
  int by = ry + nh / 2 - bh / 2 + 1;
  draw_battery(ctx, GRect(x, by, bw, bh), s.has_data ? s.battery : 0, s.charging == 2);
  x += bw + 4;
  text_at(ctx, pct, f14b, GRect(x, by - 5, ps.w + 2, 18), GTextAlignmentLeft, COL_DIM);

  // --- control row ---
  int r = large ? 16 : 13;
  int rowy = round ? H * 72 / 100 : H - (large ? 62 : 46);
  int span = round ? W * 74 / 100 : W - 2 * (r + 3);
  for (int i = 0; i < B_COUNT; i++) {
    int cx = (W - span) / 2 + span * i / (B_COUNT - 1);
    draw_button(ctx, GPoint(cx, rowy), r, i, i == s_sel);
  }

  // --- label for selected control / status text ---
  int ly = rowy + r + (large ? 10 : 6);
  const char *lbl = button_label(s_sel);
  char agebuf[32] = "";
  if (s.car_state == S_SETUP || s.car_state == S_PAIR || s.car_state == S_ERROR) lbl = s.text[0] ? s.text : lbl;
  text_at(ctx, lbl, f18b, GRect(round ? W / 6 : 2, ly, round ? W * 2 / 3 : W - 4, 22), GTextAlignmentCenter, COL_TEXT);
  if (!round && s.car_state == S_ASLEEP && s.updated) {
    int mins = (time(NULL) - s.updated) / 60;
    if (mins < 60) snprintf(agebuf, sizeof(agebuf), "Updated %dm ago", mins);
    else if (mins < 60 * 48) snprintf(agebuf, sizeof(agebuf), "Updated %dh ago", mins / 60);
    else snprintf(agebuf, sizeof(agebuf), "Updated %dd ago", mins / 1440);
    if (ly + 40 <= H) text_at(ctx, agebuf, f14b, GRect(2, ly + 19, W - 4, 18), GTextAlignmentCenter, COL_FAINT);
  }
  draw_toast(ctx, b);
}

// ---------------- generic dark menu (More / climate options) ----------------
typedef void (*MenuPick)(int row);
typedef struct { const char *title; int rows; const char *(*row_title)(int); const char *(*row_sub)(int); MenuPick pick; } MenuSpec;
static MenuSpec s_menu_spec;
static Window *s_menu_win;
static MenuLayer *s_menu;

static uint16_t m_rows(MenuLayer *m, uint16_t sec, void *ctx) { return s_menu_spec.rows; }
static void m_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  menu_cell_basic_draw(ctx, cell, s_menu_spec.row_title(idx->row), s_menu_spec.row_sub ? s_menu_spec.row_sub(idx->row) : NULL, NULL);
}
static int16_t m_header_h(MenuLayer *m, uint16_t sec, void *c) { return PBL_IF_ROUND_ELSE(0, 22); }
static void m_header(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  GRect b = layer_get_bounds(cell);
  text_at(ctx, s_menu_spec.title, f14b, GRect(6, 1, b.size.w - 12, 18), GTextAlignmentLeft, COL_ACCENT);
}
static uint16_t m_secs(MenuLayer *m, void *c) { return 1; }
static void mn_select(MenuLayer *m, MenuIndex *idx, void *c) {
  int row = idx->row;
  window_stack_remove(s_menu_win, true);
  s_menu_spec.pick(row);
}
static void menu_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = m_secs, .get_num_rows = m_rows, .draw_row = m_draw,
    .get_header_height = m_header_h, .draw_header = m_header, .select_click = mn_select });
  menu_layer_set_normal_colors(s_menu, COL_BG, COL_TEXT);
  menu_layer_set_highlight_colors(s_menu, COL_FILL, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}
static void menu_unload(Window *w) { menu_layer_destroy(s_menu); s_menu = NULL; window_destroy(w); s_menu_win = NULL; }
static void open_menu(MenuSpec spec) {
  s_menu_spec = spec;
  s_menu_win = window_create();
  window_set_background_color(s_menu_win, COL_BG);
  window_set_window_handlers(s_menu_win, (WindowHandlers){ .load = menu_load, .unload = menu_unload });
  window_stack_push(s_menu_win, true);
}

// ---------------- confirm dialog ----------------
static Window *s_confirm_win;
static Layer *s_confirm_layer;
static char s_confirm_text[32];
static int s_confirm_cmd;
static char s_confirm_progress[24];

static void confirm_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  text_at(ctx, s_confirm_text, f24b, GRect(10, H / 2 - 40, W - 20 - PBL_IF_ROUND_ELSE(0, 14), 60), GTextAlignmentCenter, COL_TEXT);
  text_at(ctx, "Back to cancel", f14b, GRect(10, H / 2 + 20, W - 20 - PBL_IF_ROUND_ELSE(0, 14), 20), GTextAlignmentCenter, COL_FAINT);
  // confirm button hint beside the select button
  int r = 11;
  GPoint c = PBL_IF_ROUND_ELSE(GPoint(W - r - 6, H / 2), GPoint(W - r - 2, H / 2));
  graphics_context_set_fill_color(ctx, COL_FILL);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(c.x - 5, c.y), GPoint(c.x - 1, c.y + 4));
  graphics_draw_line(ctx, GPoint(c.x - 1, c.y + 4), GPoint(c.x + 5, c.y - 4));
}
static void confirm_select(ClickRecognizerRef rec, void *ctx) {
  int cmd = s_confirm_cmd;
  window_stack_remove(s_confirm_win, true);
  send_cmd(cmd, 0, s_confirm_progress);
}
static void confirm_clicks(void *ctx) { window_single_click_subscribe(BUTTON_ID_SELECT, confirm_select); }
static void confirm_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_confirm_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_confirm_layer, confirm_update);
  layer_add_child(root, s_confirm_layer);
}
static void confirm_unload(Window *w) { layer_destroy(s_confirm_layer); window_destroy(w); s_confirm_win = NULL; }
static void confirm(const char *text, int cmd, const char *progress) {
  strncpy(s_confirm_text, text, sizeof(s_confirm_text) - 1);
  strncpy(s_confirm_progress, progress, sizeof(s_confirm_progress) - 1);
  s_confirm_cmd = cmd;
  s_confirm_win = window_create();
  window_set_background_color(s_confirm_win, COL_BG);
  window_set_click_config_provider(s_confirm_win, confirm_clicks);
  window_set_window_handlers(s_confirm_win, (WindowHandlers){ .load = confirm_load, .unload = confirm_unload });
  window_stack_push(s_confirm_win, true);
}

// ---------------- climate screen ----------------
static int s_pending_temp = NO_TEMP;  // c10 being edited
static AppTimer *s_temp_timer;

static int temp_shown(void) { return s_pending_temp != NO_TEMP ? s_pending_temp : s.set_temp; }

static void climate_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  bool round = PBL_IF_ROUND_ELSE(true, false);
  bool large = W >= 200;
  int side = round ? 0 : 14;  // room for button hints on the right
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // title + power pill
  int ty = round ? H / 9 : 4;
  text_at(ctx, "Climate", f18b, GRect(round ? 0 : 6, ty, round ? W : W / 2, 22), round ? GTextAlignmentCenter : GTextAlignmentLeft, COL_TEXT);
  int pw = 40, ph = 18;
  GRect pill = round ? GRect((W - pw) / 2, ty + 24, pw, ph) : GRect(W - side - pw - 4, ty + 3, pw, ph);
  graphics_context_set_fill_color(ctx, s.climate ? COL_FILL : COL_CARD);
  graphics_fill_rect(ctx, pill, ph / 2, GCornersAll);
#ifdef PBL_BW
  if (!s.climate) { graphics_context_set_stroke_color(ctx, GColorWhite); graphics_draw_round_rect(ctx, pill, ph / 2); }
#endif
  text_at(ctx, s.climate ? "ON" : "OFF", f14b, GRect(pill.origin.x, pill.origin.y, pw, ph),
          GTextAlignmentCenter, (s.climate && PBL_IF_COLOR_ELSE(false, true)) ? GColorBlack : COL_TEXT);

  // big set temperature with chevrons
  char t[12];
  fmt_temp(t, sizeof(t), temp_shown(), true);
  int bigh = 42;
  int tcy = H * (round ? 50 : 45) / 100;
  int areaw = W - side;
  text_at(ctx, t, f_big, GRect(0, tcy - bigh / 2 - 6, areaw, bigh + 10), GTextAlignmentCenter,
          s.climate ? COL_TEXT : COL_DIM);
  graphics_context_set_stroke_color(ctx, s_pending_temp != NO_TEMP ? COL_ACCENT : COL_FAINT);
  graphics_context_set_stroke_width(ctx, 3);
  int cx = areaw / 2, up = tcy - bigh / 2 - 8, dn = tcy + bigh / 2 + 10;
  graphics_draw_line(ctx, GPoint(cx - 8, up + 5), GPoint(cx, up - 2));
  graphics_draw_line(ctx, GPoint(cx, up - 2), GPoint(cx + 8, up + 5));
  graphics_draw_line(ctx, GPoint(cx - 8, dn - 5), GPoint(cx, dn + 2));
  graphics_draw_line(ctx, GPoint(cx, dn + 2), GPoint(cx + 8, dn - 5));

  // inside / outside
  char in[10], out[10], line[40];
  fmt_temp(in, sizeof(in), s.in_temp, true);
  fmt_temp(out, sizeof(out), s.out_temp, true);
  snprintf(line, sizeof(line), "In %s   Out %s", in, out);
  int iy = dn + (large ? 12 : 6);
  text_at(ctx, line, f14b, GRect(round ? W / 8 : 2, iy, round ? W * 3 / 4 : areaw - 4, 18), GTextAlignmentCenter, COL_DIM);

  // defrost + seat chips
  int chy = iy + 20;
  if (chy + 18 < H - (round ? H / 8 : 0)) {
    char seat[16];
    static const char *lv[] = { "Off", "Low", "Med", "High" };
    snprintf(seat, sizeof(seat), "Seat %s", lv[s.seat > 3 ? 3 : s.seat]);
    int cwid = round ? W / 3 : (areaw - 12) / 2;
    GRect c1 = GRect(round ? W / 2 - cwid - 2 : 4, chy, cwid, 18), c2 = GRect(round ? W / 2 + 2 : 8 + cwid, chy, cwid, 18);
    graphics_context_set_fill_color(ctx, s.defrost ? COL_FILL : COL_CARD);
    graphics_fill_rect(ctx, c1, 9, GCornersAll);
    graphics_context_set_fill_color(ctx, s.seat ? COL_FILL : COL_CARD);
    graphics_fill_rect(ctx, c2, 9, GCornersAll);
#ifdef PBL_BW
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_round_rect(ctx, c1, 9); graphics_draw_round_rect(ctx, c2, 9);
#endif
    GColor on_txt = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
    text_at(ctx, "Defrost", f14b, GRect(c1.origin.x, c1.origin.y - 1, c1.size.w, 18), GTextAlignmentCenter, s.defrost ? on_txt : COL_TEXT);
    text_at(ctx, seat, f14b, GRect(c2.origin.x, c2.origin.y - 1, c2.size.w, 18), GTextAlignmentCenter, s.seat ? on_txt : COL_TEXT);
  }

  // button hints on the right edge (up = warmer, select = power, down = cooler)
  if (!round) {
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(W - 3, 18, 3, 26), 1, GCornersAll);
    graphics_fill_rect(ctx, GRect(W - 3, H / 2 - 13, 3, 26), 1, GCornersAll);
    graphics_fill_rect(ctx, GRect(W - 3, H - 44, 3, 26), 1, GCornersAll);
    text_at(ctx, "+", f18b, GRect(W - side - 2, 18, side, 22), GTextAlignmentCenter, COL_DIM);
    text_at(ctx, "-", f18b, GRect(W - side - 2, H - 46, side, 22), GTextAlignmentCenter, COL_DIM);
    graphics_context_set_fill_color(ctx, COL_DIM);
    graphics_fill_circle(ctx, GPoint(W - side / 2 - 2, H / 2), 3);
  }
  if (s_toast_kind == TOAST_NONE && H - (chy + 20) >= 22 && !round) {
    text_at(ctx, "Hold select: more", f14b, GRect(2, H - 20, areaw - 4, 18), GTextAlignmentCenter, COL_FAINT);
  }
  draw_toast(ctx, b);
}

static void temp_commit(void *ctx) {
  s_temp_timer = NULL;
  if (s_pending_temp == NO_TEMP) return;
  int c10 = s_pending_temp;
  s.set_temp = c10;
  s_pending_temp = NO_TEMP;
  s_busy = false;  // allow temperature even right after another command
  char what[24], t[12];
  fmt_temp(t, sizeof(t), c10, true);
  snprintf(what, sizeof(what), "Setting %s", t);
  send_cmd(C_SET_TEMP, c10, what);
}
static void temp_step(int dir) {
  int cur = temp_shown();
  if (cur == NO_TEMP) cur = 210;
  int next;
  if (is_f()) {
    int f = c10_to_disp(cur) + dir;
    if (f < 59) { f = 59; }
    if (f > 82) { f = 82; }
    next = ((f - 32) * 50 + (f >= 32 ? 4 : -4)) / 9;
  } else {
    next = ((cur + (dir > 0 ? 5 : -5)) / 5) * 5;
    if (next < 150) { next = 150; }
    if (next > 280) { next = 280; }
  }
  s_pending_temp = next;
  if (s_temp_timer) app_timer_reschedule(s_temp_timer, 1500);
  else s_temp_timer = app_timer_register(1500, temp_commit, NULL);
  redraw();
}
static void cl_up(ClickRecognizerRef r, void *c) { temp_step(+1); }
static void cl_down(ClickRecognizerRef r, void *c) { temp_step(-1); }
static void cl_select(ClickRecognizerRef r, void *c) {
  if (s.climate) send_cmd(C_CLIMATE_OFF, 0, "Climate off");
  else send_cmd(C_CLIMATE_ON, 0, "Climate on");
}

static const char *opt_title(int row) {
  static const char *t[] = { "Defrost", "Seat heat", "Climate" };
  return t[row];
}
static const char *opt_sub(int row) {
  static char buf[24];
  static const char *lv[] = { "Off", "Low", "Medium", "High" };
  if (row == 0) return s.defrost ? "On - tap to turn off" : "Off - tap for max defrost";
  if (row == 1) { snprintf(buf, sizeof(buf), "Driver: %s", lv[s.seat > 3 ? 3 : s.seat]); return buf; }
  return s.climate ? "On - tap to turn off" : "Off - tap to turn on";
}
static void opt_pick(int row) {
  if (row == 0) send_cmd(s.defrost ? C_DEFROST_OFF : C_DEFROST_ON, 0, s.defrost ? "Defrost off" : "Defrost on");
  else if (row == 1) {
    int next = (s.seat + 1) % 4;
    static const char *lv[] = { "Seat off", "Seat low", "Seat medium", "Seat high" };
    send_cmd(C_SEAT, next, lv[next]);
  } else cl_select(NULL, NULL);
}
static void cl_long(ClickRecognizerRef r, void *c) {
  open_menu((MenuSpec){ .title = "Climate", .rows = 3, .row_title = opt_title, .row_sub = opt_sub, .pick = opt_pick });
}
static void climate_clicks(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 200, cl_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 200, cl_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, cl_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, cl_long, NULL);
}
static void climate_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_climate_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_climate_layer, climate_update);
  layer_add_child(root, s_climate_layer);
}
static void climate_unload(Window *w) {
  if (s_temp_timer) { app_timer_cancel(s_temp_timer); temp_commit(NULL); }
  layer_destroy(s_climate_layer); s_climate_layer = NULL;
  window_destroy(w); s_climate_win = NULL;
}
static void open_climate(void) {
  s_climate_win = window_create();
  window_set_background_color(s_climate_win, COL_BG);
  window_set_click_config_provider(s_climate_win, climate_clicks);
  window_set_window_handlers(s_climate_win, (WindowHandlers){ .load = climate_load, .unload = climate_unload });
  window_stack_push(s_climate_win, true);
}

// ---------------- "More" menu ----------------
static const char *more_title(int row) {
  static const char *t[] = { "Flash lights", "Honk horn", "Wake & refresh", "Lock", "Unlock" };
  return t[row];
}
static const char *more_sub(int row) {
  if (row == 2) return s.car_state == S_ASLEEP ? "Car is asleep" : "Get latest status";
  return NULL;
}
static void more_pick(int row) {
  switch (row) {
    case 0: send_cmd(C_FLASH, 0, "Flashing"); break;
    case 1: confirm("Honk the horn?", C_HONK, "Honking"); break;
    case 2: send_refresh(true); break;
    case 3: send_cmd(C_LOCK, 0, "Locking"); break;
    case 4: confirm("Unlock car?", C_UNLOCK, "Unlocking"); break;
  }
}

// ---------------- main window clicks ----------------
static void m_up(ClickRecognizerRef r, void *c) { s_sel = (s_sel + B_COUNT - 1) % B_COUNT; redraw(); }
static void m_down(ClickRecognizerRef r, void *c) { s_sel = (s_sel + 1) % B_COUNT; redraw(); }
static void m_select(ClickRecognizerRef r, void *c) {
  switch (s_sel) {
    case B_LOCK:
      if (s.locked || !s.has_data) confirm("Unlock car?", C_UNLOCK, "Unlocking");
      else send_cmd(C_LOCK, 0, "Locking");
      break;
    case B_CLIMATE: open_climate(); break;
    case B_FRUNK:
      if (s.frunk) toast(TOAST_ERR, "Close by hand", 2500);
      else confirm("Open frunk?", C_FRUNK, "Opening frunk");
      break;
    case B_TRUNK: confirm(s.trunk ? "Close trunk?" : "Open trunk?", C_TRUNK, s.trunk ? "Closing trunk" : "Opening trunk"); break;
    default:
      open_menu((MenuSpec){ .title = "More", .rows = 5, .row_title = more_title, .row_sub = more_sub, .pick = more_pick });
  }
}
static void m_long(ClickRecognizerRef r, void *c) { send_refresh(true); }
static void main_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, m_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, m_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, m_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, m_long, NULL);
}
static void main_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_main_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_main_layer, main_update);
  layer_add_child(root, s_main_layer);
}
static void main_unload(Window *w) { layer_destroy(s_main_layer); s_main_layer = NULL; }

// ---------------- app ----------------
static void load_status(void) {
  memset(&s, 0, sizeof(s));
  if (persist_exists(PERSIST_STATUS) && persist_get_size(PERSIST_STATUS) == (int)sizeof(s)) {
    persist_read_data(PERSIST_STATUS, &s, sizeof(s));
    if (s.version != STATUS_VERSION) memset(&s, 0, sizeof(s));
  }
  s.version = STATUS_VERSION;
  if (!s.has_data) { s.set_temp = 210; s.in_temp = NO_TEMP; s.out_temp = NO_TEMP; s.units = 3; s.locked = true; }
  s.car_state = S_BUSY;
#ifdef DEMO
  s.has_data = true; s.car_state = S_ONLINE; s.battery = 78; s.range = 241; s.units = 3; s.locked = true;
  s.climate = true; s.set_temp = 211; s.in_temp = 185; s.out_temp = 90; s.seat = 2; s.trunk = DEMO_TRUNK;
  strcpy(s.name, "Blue Thunder");
#endif
}

static void init(void) {
  f14b = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  f18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  f24b = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  f28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  f_big = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
  load_status();
  app_message_register_inbox_received(inbox);
  app_message_open(512, 64);

  s_main_win = window_create();
  window_set_background_color(s_main_win, COL_BG);
  window_set_click_config_provider(s_main_win, main_clicks);
  window_set_window_handlers(s_main_win, (WindowHandlers){ .load = main_load, .unload = main_unload });
  window_stack_push(s_main_win, false);
#ifdef DEMO
  if (DEMO_SCREEN == 1) open_climate();
#endif
}
static void deinit(void) { save_status(); window_destroy(s_main_win); }

int main(void) { init(); app_event_loop(); deinit(); }
