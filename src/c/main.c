// Tesla Remote - watch side. Tesla-style dark UI with blue accents.
#include <pebble.h>
#include "art_geo.h"

// Touch screens: Pebble Time 2 (emery) and Round 2 (gabbro). Other watches are button-only.
#if !defined(PBL_PLATFORM_BASALT) && !defined(PBL_PLATFORM_CHALK) && !defined(PBL_PLATFORM_DIORITE) && !defined(PBL_PLATFORM_APLITE)
#define HAS_TOUCH 1
#endif
#ifdef HAS_TOUCH
static void attach_touch(Window *w, RecognizerEventCb tap, bool back);
static void climate_tap(const Recognizer *rec, RecognizerEvent e);
static void seat_tap(const Recognizer *rec, RecognizerEvent e);
static void confirm_tap(const Recognizer *rec, RecognizerEvent e);
#endif

// ---------------- protocol (matches src/pkjs/index.js) ----------------
enum { T_STATUS = 1, T_CMD = 2, T_RESULT = 3, T_REFRESH = 4, T_PROGRESS = 5, T_SETTING = 6,
       T_MENU_REQ = 7, T_MENU = 8, T_ACTION = 9, T_MENU_CLOSE = 10 };
enum { C_LOCK = 1, C_UNLOCK, C_FRUNK, C_TRUNK, C_CLIMATE_ON, C_CLIMATE_OFF, C_SET_TEMP,
       C_DEFROST_ON, C_DEFROST_OFF, C_SEAT, C_FLASH, C_HONK, C_COOL, C_WHEEL_ON, C_WHEEL_OFF,
       C_VENT, C_CLOSE_WIN, C_SET_TEMPS, C_TRUNK_CLOSE };
enum { S_UNKNOWN = 0, S_ONLINE, S_ASLEEP, S_OFFLINE, S_ERROR, S_SETUP, S_PAIR, S_BUSY };
#define NO_TEMP (-1000)
#define PERSIST_STATUS 1
#define STATUS_VERSION 6

typedef struct {
  uint8_t version;
  int8_t car_state;
  bool has_data, locked, frunk, trunk, climate, defrost, rear_defrost, wheel, windows, split, can_cool, has_rear, has_rear_c, has_3rd;
  uint8_t battery, charging, charge_limit, units, model, shift, layout;
  int8_t tonneau;    // Cybertruck: -1 unknown/none, 0 closed, 1 open, 2 ajar, 5 opening, 6 closing
  int16_t speed;
  uint8_t seat[7];   // heat 0-3: driver, passenger, rear left, rear centre, rear right, third left, third right
  uint8_t cool[2];   // cooling 0-3: driver, passenger
  int16_t range, min_to_full, in_temp, out_temp, set_temp, pass_temp, bearing;
  int32_t dist;      // metres to car, -1 unknown
  int32_t updated;
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
#define COL_ON_FILL PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
#define COL_CARD PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define COL_CAR PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COL_GLASS PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define COL_GOOD PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite)
#define COL_BAD PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define COL_HEAT PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define COL_COOL PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite)

static GFont f14b, f18, f18b, f24b, f28b, f_big;

static void text_at(GContext *ctx, const char *t, GFont f, GRect r, GTextAlignment a, GColor col) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, t, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}
static int text_w(const char *t, GFont f) {
  return graphics_text_layout_get_content_size(t, f, GRect(0, 0, 300, 60), GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// ---------------- toast (progress / result banner) ----------------
enum { TOAST_NONE, TOAST_PROGRESS, TOAST_OK, TOAST_ERR };
static int s_toast_kind = TOAST_NONE;
static char s_toast[41];
static AppTimer *s_toast_timer;
static bool s_busy;

static Layer *s_main_layer, *s_climate_layer, *s_seat_layer;

static void rm_redraw(void);
static void rm_receive(const char *page, const char *title, const char *rows);
static void redraw(void) {
  rm_redraw();
  if (s_main_layer) layer_mark_dirty(s_main_layer);
  if (s_climate_layer) layer_mark_dirty(s_climate_layer);
  if (s_seat_layer) layer_mark_dirty(s_seat_layer);
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
static void draw_toast(GContext *ctx, GRect b) {
  if (s_toast_kind == TOAST_NONE) return;
  int w = b.size.w - PBL_IF_ROUND_ELSE(b.size.w / 3, 12), h = 26;
  int y = b.size.h - h - PBL_IF_ROUND_ELSE(b.size.h / 9, 4);
  GRect r = GRect((b.size.w - w) / 2, y, w, h);
  GColor bg = s_toast_kind == TOAST_OK ? COL_FILL : (s_toast_kind == TOAST_ERR ? COL_BAD : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, r, h / 2, GCornersAll);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s%s", s_toast, s_toast_kind == TOAST_PROGRESS ? "..." : "");
  text_at(ctx, buf, f18b, GRect(r.origin.x + 4, r.origin.y + 1, r.size.w - 8, h), GTextAlignmentCenter, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
}

// ---------------- messaging ----------------
static void send3(int type, int cmd, int32_t value) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) { toast(TOAST_ERR, "Phone busy", 2000); return; }
  dict_write_int32(it, MESSAGE_KEY_Type, type);
  dict_write_int32(it, MESSAGE_KEY_Cmd, cmd);
  dict_write_int32(it, MESSAGE_KEY_Value, value);
  app_message_outbox_send();
}
static void send_cmd(int cmd, int32_t value, const char *what) {
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

// ---------------- charging pulse ----------------
static AppTimer *s_pulse_timer;
static bool s_pulse;
static int s_phase;
static void pulse_tick(void *ctx) {
  s_phase = (s_phase + 1) % 8;
  s_pulse = s_phase >= 4;
  if (s_main_layer) layer_mark_dirty(s_main_layer);
  s_pulse_timer = (s.charging == 2 && s.has_data) ? app_timer_register(140, pulse_tick, NULL) : NULL;
}
static void anim_kick(void);
static void pulse_update(void) {
  if (s.charging == 2 && s.has_data && !s_pulse_timer) s_pulse_timer = app_timer_register(140, pulse_tick, NULL);
}

static void inbox(DictionaryIterator *it, void *ctx) {
  int type = tint(it, MESSAGE_KEY_Type, 0);
  Tuple *text = dict_find(it, MESSAGE_KEY_Text);
  if (type == T_STATUS) {
    s.car_state = tint(it, MESSAGE_KEY_CarState, s.car_state);
    if (tint(it, MESSAGE_KEY_HasData, 0)) {
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
      s.rear_defrost = tint(it, MESSAGE_KEY_RearDefrost, s.rear_defrost);
      s.wheel = tint(it, MESSAGE_KEY_Wheel, s.wheel);
      s.windows = tint(it, MESSAGE_KEY_Windows, s.windows);
      s.seat[0] = tint(it, MESSAGE_KEY_SeatHeat, s.seat[0]);
      s.seat[1] = tint(it, MESSAGE_KEY_SeatR, s.seat[1]);
      s.seat[2] = tint(it, MESSAGE_KEY_SeatRL, s.seat[2]);
      s.seat[3] = tint(it, MESSAGE_KEY_SeatRC, s.seat[3]);
      s.seat[4] = tint(it, MESSAGE_KEY_SeatRR, s.seat[4]);
      s.seat[5] = tint(it, MESSAGE_KEY_Seat3L, s.seat[5]);
      s.seat[6] = tint(it, MESSAGE_KEY_Seat3R, s.seat[6]);
      s.has_3rd = tint(it, MESSAGE_KEY_Has3rd, s.has_3rd);
      s.layout = tint(it, MESSAGE_KEY_Layout, s.layout);
      s.tonneau = tint(it, MESSAGE_KEY_Tonneau, s.tonneau);
      s.has_rear = tint(it, MESSAGE_KEY_HasRear, s.has_rear);
      s.has_rear_c = tint(it, MESSAGE_KEY_HasRearC, s.has_rear_c);
      s.cool[0] = tint(it, MESSAGE_KEY_CoolL, s.cool[0]);
      s.cool[1] = tint(it, MESSAGE_KEY_CoolR, s.cool[1]);
      s.can_cool = tint(it, MESSAGE_KEY_CanCool, s.can_cool);
      s.in_temp = tint(it, MESSAGE_KEY_InTemp, s.in_temp);
      s.out_temp = tint(it, MESSAGE_KEY_OutTemp, s.out_temp);
      s.set_temp = tint(it, MESSAGE_KEY_SetTemp, s.set_temp);
      s.pass_temp = tint(it, MESSAGE_KEY_PassTemp, s.pass_temp);
      s.units = tint(it, MESSAGE_KEY_Units, s.units);
      s.model = tint(it, MESSAGE_KEY_Model, s.model);
      s.dist = tint(it, MESSAGE_KEY_Dist, s.dist);
      s.shift = tint(it, MESSAGE_KEY_Shift, 0);
      s.speed = tint(it, MESSAGE_KEY_Speed, 0);
      s.bearing = tint(it, MESSAGE_KEY_Bearing, s.bearing);
      s.updated = time(NULL) - tint(it, MESSAGE_KEY_Age, 0);
    }
    s.split = tint(it, MESSAGE_KEY_Split, s.split);
    Tuple *name = dict_find(it, MESSAGE_KEY_CarName);
    if (name && name->value->cstring[0]) { strncpy(s.name, name->value->cstring, sizeof(s.name) - 1); }
    strncpy(s.text, text ? text->value->cstring : "", sizeof(s.text) - 1);
    if (s_toast_kind == TOAST_PROGRESS && !s_busy && s.car_state != S_BUSY) toast_hide(NULL);
    save_status();
    pulse_update();
    anim_kick();
  } else if (type == T_MENU) {
    Tuple *pg = dict_find(it, MESSAGE_KEY_MenuPage), *ti = dict_find(it, MESSAGE_KEY_MenuTitle), *rw = dict_find(it, MESSAGE_KEY_MenuRows);
    if (pg && rw) rm_receive(pg->value->cstring, ti ? ti->value->cstring : "", rw->value->cstring);
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

// ---------------- temperature & distance helpers ----------------
static bool is_f(void) { return s.units & 1; }
// +1 heating (set point warmer than the cabin), -1 cooling, 0 unknown
static int hvac_dir(int set_c10) {
  if (s.in_temp == NO_TEMP || set_c10 == NO_TEMP) return 0;
  return set_c10 > s.in_temp ? 1 : -1;
}
static GColor hvac_color(int dir, GColor fallback) { return dir > 0 ? COL_HEAT : dir < 0 ? COL_COOL : fallback; }
static bool is_mi(void) { return s.units & 2; }
static int c10_to_disp(int c10) {  // F: whole degrees. C: tenths
  if (is_f()) return (c10 * 9 + (c10 >= 0 ? 25 : -25)) / 50 + 32;
  return c10;
}
static void fmt_temp(char *buf, size_t n, int c10) {
  if (c10 == NO_TEMP) { snprintf(buf, n, "--"); return; }
  int v = c10_to_disp(c10);
  if (is_f()) snprintf(buf, n, "%d°", v);
  else if (v % 10 == 0) snprintf(buf, n, "%d°", v / 10);
  else snprintf(buf, n, "%d.%d°", v / 10, abs(v % 10));
}
static void fmt_dist(char *buf, size_t n) {
  static const char *dirs[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  buf[0] = 0;
  if (s.dist < 0 || !s.has_data) return;
  const char *dir = dirs[((s.bearing + 22) / 45) % 8];
  if (s.dist < 25) { snprintf(buf, n, "At car"); return; }
  if (is_mi()) {
    int ft = s.dist * 3281 / 1000;
    if (ft < 1000) snprintf(buf, n, "%d ft %s", (ft + 5) / 10 * 10, dir);
    else {
      int mi10 = (s.dist * 10 + 804) / 1609;
      if (mi10 < 100) snprintf(buf, n, "%d.%d mi %s", mi10 / 10, mi10 % 10, dir);
      else snprintf(buf, n, "%d mi %s", mi10 / 10, dir);
    }
  } else {
    if (s.dist < 1000) snprintf(buf, n, "%d m %s", (int)((s.dist + 5) / 10 * 10), dir);
    else {
      int km10 = (s.dist + 50) / 100;
      if (km10 < 100) snprintf(buf, n, "%d.%d km %s", km10 / 10, km10 % 10, dir);
      else snprintf(buf, n, "%d km %s", km10 / 10, dir);
    }
  }
}

// ---------------- car art (generated by tools/make_assets.py, see art_geo.h) ----------------
static GBitmap *s_bmp[3];
static uint32_t s_bmp_id[3];
static GBitmap *bmp_get(int slot, uint32_t id) {
  if (s_bmp[slot] && s_bmp_id[slot] == id) return s_bmp[slot];
  if (s_bmp[slot]) gbitmap_destroy(s_bmp[slot]);
  s_bmp[slot] = gbitmap_create_with_resource(id);
  s_bmp_id[slot] = id;
  return s_bmp[slot];
}
static bool is_ct(void) { return s.model == 5; }
// Status model code (1 S, 2 3, 3 X, 4 Y, 5 Cybertruck) -> art index (0 3, 1 S, 2 X, 3 Y, 4 Cybertruck)
static int art_model(void) { static const int8_t m[6] = { 0, 1, 0, 2, 3, 4 }; return m[s.model < 6 ? s.model : 0]; }
static int tonneau_target(void) { return (s.tonneau == 1 || s.tonneau == 2 || s.tonneau == 5) ? 7 : 0; }
static const uint32_t CHARGE_RES[8] = { RESOURCE_ID_CHARGE_0, RESOURCE_ID_CHARGE_1, RESOURCE_ID_CHARGE_2, RESOURCE_ID_CHARGE_3,
  RESOURCE_ID_CHARGE_4, RESOURCE_ID_CHARGE_5, RESOURCE_ID_CHARGE_6, RESOURCE_ID_CHARGE_7 };
// Draw the charger cord with its plug tip at `port`, clipped so it never runs past `max_y`.
static void draw_charger(GContext *ctx, GPoint port, int max_y, int phase) {
  GBitmap *cb = bmp_get(2, CHARGE_RES[phase % 8]);
  GSize cs = gbitmap_get_bounds(cb).size;
  int tx = 25 * cs.w / 40, ty = 1 * cs.h / 56;     // plug tip inside the 40x56 frame
  GRect r = GRect(port.x - tx, port.y - ty, cs.w, cs.h);
  if (r.origin.y + r.size.h > max_y) r.size.h = max_y - r.origin.y;
  if (r.size.h <= 0) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, cb, r);
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// Control icons (28x28 glyphs supplied as art). Colour watches recolour the glyph.
static GBitmap *s_icon[5];
static void draw_icon(GContext *ctx, int i, GPoint c, GColor col, bool on_light) {
  GBitmap *b = s_icon[i];
  if (!b) return;
  GSize sz = gbitmap_get_bounds(b).size;
  GRect r = GRect(c.x - sz.w / 2, c.y - sz.h / 2, sz.w, sz.h);
#ifdef PBL_COLOR
  GBitmapFormat f = gbitmap_get_format(b);
  int n = f == GBitmapFormat1BitPalette ? 2 : f == GBitmapFormat2BitPalette ? 4 : f == GBitmapFormat4BitPalette ? 16 : 0;
  GColor *pal = gbitmap_get_palette(b);
  for (int k = 0; k < n && pal; k++) if (pal[k].a) pal[k] = (GColor){ .argb = (uint8_t)(col.argb | 0xC0) };
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
#else
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
#endif
  graphics_draw_bitmap_in_rect(ctx, b, r);
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}


// Lid animation: frame 0 = closed, 7 = fully open.
static int s_fr_frame, s_tr_frame, s_tn_frame;  // what is on screen now
static AppTimer *s_anim_timer;
static void anim_tick(void *ctx) {
  s_anim_timer = NULL;
  int ft = (s.frunk && s.has_data) ? 7 : 0, tt = (s.trunk && s.has_data) ? 7 : 0;
  int delay = 0;
  // roughly real Tesla speed: frunk lid ~3 s, power trunk ~4.5 s
  int nt = (HAS_TONNEAU_ART && is_ct() && s.has_data) ? tonneau_target() : 0;
  if (s_fr_frame != ft) { s_fr_frame += ft > s_fr_frame ? 1 : -1; delay = 420; }
  else if (s_tr_frame != tt) { s_tr_frame += tt > s_tr_frame ? 1 : -1; delay = 640; }
  else if (s_tn_frame != nt) { s_tn_frame += nt > s_tn_frame ? 1 : -1; delay = 700; }
  if (s_main_layer) layer_mark_dirty(s_main_layer);
  if (delay) s_anim_timer = app_timer_register(delay, anim_tick, NULL);
}
static void anim_kick(void) { if (!s_anim_timer) s_anim_timer = app_timer_register(30, anim_tick, NULL); }

// ---------------- line-art icons (2px) ----------------
static void line2(GContext *ctx, GPoint a, GPoint b) { graphics_draw_line(ctx, a, b); }

// Heat waves: n wavy lines (2px), like Tesla's seat-heat symbol.
static void icon_heat(GContext *ctx, GPoint c, int s, int n, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 2);
  int gap = s * 4 / 10 > 4 ? s * 4 / 10 : 4, h = s * 7 / 10;
  if (h < 8) h = 8;
  static const int8_t wave[5] = { 0, 2, 0, -2, 0 };
  for (int i = 0; i < n; i++) {
    int x = c.x + i * gap - (n - 1) * gap / 2;
    int y0 = c.y - h / 2;
    for (int k = 0; k < 4; k++)
      line2(ctx, GPoint(x + wave[k], y0 + k * h / 4), GPoint(x + wave[k + 1], y0 + (k + 1) * h / 4));
  }
}
// Cooling: n small snow stars
static void icon_cool(GContext *ctx, GPoint c, int s, int n, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 1);
  int gap = s / 3, r = s / 7 + 1;
  for (int i = 0; i < n; i++) {
    GPoint p = GPoint(c.x + (i - (n - 1) / 2) * gap - ((n % 2) ? 0 : gap / 2), c.y);
    graphics_draw_line(ctx, GPoint(p.x - r, p.y), GPoint(p.x + r, p.y));
    graphics_draw_line(ctx, GPoint(p.x, p.y - r), GPoint(p.x, p.y + r));
    graphics_draw_line(ctx, GPoint(p.x - r + 1, p.y - r + 1), GPoint(p.x + r - 1, p.y + r - 1));
    graphics_draw_line(ctx, GPoint(p.x - r + 1, p.y + r - 1), GPoint(p.x + r - 1, p.y - r + 1));
  }
}

static void icon_bolt(GContext *ctx, GPoint c, int s, GColor col) {
  GPoint pts[6] = { GPoint(c.x + s / 5, c.y - s / 2), GPoint(c.x - s / 3, c.y + s / 10), GPoint(c.x, c.y + s / 10),
                    GPoint(c.x - s / 5, c.y + s / 2), GPoint(c.x + s / 3, c.y - s / 10), GPoint(c.x, c.y - s / 10) };
  GPathInfo info = { .num_points = 6, .points = pts };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, col);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void draw_battery(GContext *ctx, GRect r, int pct, bool charging, bool pulse) {
  graphics_context_set_stroke_color(ctx, charging ? COL_GOOD : COL_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, r, 2);
  graphics_context_set_fill_color(ctx, charging ? COL_GOOD : COL_DIM);
  graphics_fill_rect(ctx, GRect(r.origin.x + r.size.w, r.origin.y + r.size.h / 3, 2, r.size.h / 3), 0, GCornerNone);
  int fw = (r.size.w - 4) * pct / 100;
  if (fw < 1 && pct > 0) fw = 1;
  GColor fill = pct <= 20 ? COL_BAD : COL_TEXT;
  if (charging) fill = PBL_IF_COLOR_ELSE(pulse ? GColorGreen : GColorIslamicGreen, GColorWhite);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 2, charging ? r.size.w - 4 : fw, r.size.h - 4), 1, GCornersAll);
  if (charging) {
    GPoint c = grect_center_point(&r);
#ifdef PBL_BW
    if (!pulse) { graphics_context_set_fill_color(ctx, GColorBlack); graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 2, r.size.w - 4, r.size.h - 4), 1, GCornersAll); }
    icon_bolt(ctx, c, r.size.h + 2, pulse ? GColorBlack : GColorWhite);
#else
    icon_bolt(ctx, c, r.size.h + 2, GColorBlack);
#endif
  }
}

// ---------------- generic dark menu ----------------
typedef void (*MenuPick)(int row);
typedef struct { const char *title; int rows; const char *(*row_title)(int); const char *(*row_sub)(int); MenuPick pick; } MenuSpec;
static MenuSpec s_menu_spec;
static Window *s_menu_win;
static MenuLayer *s_menu;

static uint16_t mn_rows(MenuLayer *m, uint16_t sec, void *ctx) { return s_menu_spec.rows; }
static void mn_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  menu_cell_basic_draw(ctx, cell, s_menu_spec.row_title(idx->row), s_menu_spec.row_sub ? s_menu_spec.row_sub(idx->row) : NULL, NULL);
}
static int16_t mn_header_h(MenuLayer *m, uint16_t sec, void *c) { return PBL_IF_ROUND_ELSE(0, 22); }
static void mn_header(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  GRect b = layer_get_bounds(cell);
  text_at(ctx, s_menu_spec.title, f14b, GRect(6, 1, b.size.w - 12, 18), GTextAlignmentLeft, COL_ACCENT);
}
static uint16_t mn_secs(MenuLayer *m, void *c) { return 1; }
static void mn_select(MenuLayer *m, MenuIndex *idx, void *c) {
  int row = idx->row;
  MenuPick pick = s_menu_spec.pick;
  window_stack_remove(s_menu_win, true);
  pick(row);
}
static void menu_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = mn_secs, .get_num_rows = mn_rows, .draw_row = mn_draw,
    .get_header_height = mn_header_h, .draw_header = mn_header, .select_click = mn_select });
  menu_layer_set_normal_colors(s_menu, COL_BG, COL_TEXT);
  menu_layer_set_highlight_colors(s_menu, COL_FILL, COL_ON_FILL);
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
static char s_confirm_text[32], s_confirm_progress[24], s_confirm_action[40];
static int s_confirm_cmd;

static void confirm_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  int side = PBL_IF_ROUND_ELSE(0, 16);
  text_at(ctx, s_confirm_text, f24b, GRect(10, H / 2 - 40, W - 20 - side, 60), GTextAlignmentCenter, COL_TEXT);
  text_at(ctx, "Back to cancel", f14b, GRect(10, H / 2 + 20, W - 20 - side, 20), GTextAlignmentCenter, COL_FAINT);
  int r = 11;
  GPoint c = GPoint(W - r - PBL_IF_ROUND_ELSE(6, 2), H / 2);
  graphics_context_set_fill_color(ctx, COL_FILL);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_stroke_color(ctx, COL_ON_FILL);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(c.x - 5, c.y), GPoint(c.x - 1, c.y + 4));
  graphics_draw_line(ctx, GPoint(c.x - 1, c.y + 4), GPoint(c.x + 5, c.y - 4));
}
static void send_action(const char *action, const char *what);
static void confirm_select(ClickRecognizerRef rec, void *ctx) {
  int cmd = s_confirm_cmd;
  window_stack_remove(s_confirm_win, true);
  if (s_confirm_action[0]) send_action(s_confirm_action, s_confirm_progress);
  else send_cmd(cmd, 0, s_confirm_progress);
}
static void confirm_clicks(void *ctx) { window_single_click_subscribe(BUTTON_ID_SELECT, confirm_select); }
static void confirm_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_confirm_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_confirm_layer, confirm_update);
  layer_add_child(root, s_confirm_layer);
#ifdef HAS_TOUCH
  attach_touch(w, confirm_tap, true);
#endif
}
static void confirm_unload(Window *w) { layer_destroy(s_confirm_layer); window_destroy(w); s_confirm_win = NULL; }
static void confirm(const char *text, int cmd, const char *progress) {
  strncpy(s_confirm_text, text, sizeof(s_confirm_text) - 1);
  strncpy(s_confirm_progress, progress, sizeof(s_confirm_progress) - 1);
  s_confirm_cmd = cmd;
  s_confirm_action[0] = 0;
  s_confirm_win = window_create();
  window_set_background_color(s_confirm_win, COL_BG);
  window_set_click_config_provider(s_confirm_win, confirm_clicks);
  window_set_window_handlers(s_confirm_win, (WindowHandlers){ .load = confirm_load, .unload = confirm_unload });
  window_stack_push(s_confirm_win, true);
}

// ---------------- shared quick actions ----------------
static void set_split(bool on) {
  s.split = on;
  save_status();
  send3(T_SETTING, 0, on ? 1 : 0);
  toast(TOAST_OK, on ? "Split temperature on" : "Split temperature off", 1500);
}


// ---------------- menus built by the phone ("More") ----------------
// The phone sends a page as rows of "title\tsubtitle\taction" separated by newlines.
// Actions: "p:page" open page, "c:cmd:arg" run, "k:Question?:cmd:arg" confirm first,
// "w:split" / "w:refresh" handled here, "" = information only.
#define RM_MAX 4
#define RM_ROWS 20
typedef struct {
  Window *win; MenuLayer *ml;
  char page[16]; char title[24];
  char *buf; int n;
  char *t[RM_ROWS], *sub[RM_ROWS], *act[RM_ROWS];
} RMenu;
static RMenu s_rm[RM_MAX];
static int s_rm_depth;

static void send_text(int type, const char *text) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_int32(it, MESSAGE_KEY_Type, type);
  dict_write_cstring(it, MESSAGE_KEY_Text, text);
  app_message_outbox_send();
}
static void send_action(const char *action, const char *what) {
  if (s_busy) { vibes_short_pulse(); return; }
  s_busy = true;
  toast(TOAST_PROGRESS, what, 90000);
  send_text(T_ACTION, action);
}

static uint16_t rm_rows(MenuLayer *ml, uint16_t sec, void *ctx) { RMenu *m = ctx; return m->n ? m->n : 1; }
static void rm_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  RMenu *m = c;
  if (!m->n) { menu_cell_basic_draw(ctx, cell, "Loading...", NULL, NULL); return; }
  int i = idx->row;
  bool info = !m->act[i][0];
  if (info && !menu_cell_layer_is_highlighted(cell)) graphics_context_set_text_color(ctx, COL_DIM);
  menu_cell_basic_draw(ctx, cell, m->t[i], m->sub[i][0] ? m->sub[i] : NULL, NULL);
}
static int16_t rm_header_h(MenuLayer *ml, uint16_t sec, void *ctx) { return PBL_IF_ROUND_ELSE(0, 22); }
static void rm_header(GContext *ctx, const Layer *cell, uint16_t sec, void *c) {
  RMenu *m = c;
  GRect b = layer_get_bounds(cell);
  const char *t = s_toast_kind != TOAST_NONE ? s_toast : m->title;
  GColor col = s_toast_kind == TOAST_ERR ? COL_BAD : s_toast_kind == TOAST_OK ? COL_GOOD : COL_ACCENT;
  text_at(ctx, t, f14b, GRect(6, 1, b.size.w - 12, 18), GTextAlignmentLeft, col);
}
static uint16_t rm_secs(MenuLayer *ml, void *c) { return 1; }
static void open_remote(const char *page);
static void rm_select(MenuLayer *ml, MenuIndex *idx, void *c) {
  RMenu *m = c;
  if (!m->n) return;
  const char *a = m->act[idx->row];
  if (!a[0]) return;
  if (a[0] == 'p' && a[1] == ':') open_remote(a + 2);
  else if (a[0] == 'c' && a[1] == ':') send_action(a, m->t[idx->row]);
  else if (a[0] == 'k' && a[1] == ':') {
    // k:Question?:cmd:arg  ->  confirm, then run "c:cmd:arg"
    const char *q = a + 2, *rest = strchr(q, ':');
    if (!rest) return;
    char question[32];
    int ql = rest - q < (int)sizeof(question) - 1 ? rest - q : (int)sizeof(question) - 1;
    memcpy(question, q, ql); question[ql] = 0;
    confirm(question, 0, m->t[idx->row]);
    snprintf(s_confirm_action, sizeof(s_confirm_action), "c:%s", rest + 1);
  } else if (!strcmp(a, "w:split")) { set_split(!s.split); send_text(T_MENU_REQ, m->page); }
  else if (!strcmp(a, "w:refresh")) send_refresh(true);
}
static void rm_parse(RMenu *m, const char *rows) {
  if (m->buf) free(m->buf);
  m->n = 0;
  size_t len = strlen(rows);
  m->buf = malloc(len + 1);
  if (!m->buf) return;
  memcpy(m->buf, rows, len + 1);
  char *p = m->buf;
  while (*p && m->n < RM_ROWS) {
    char *line = p, *nl = strchr(p, '\n');
    if (nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
    char *f1 = strchr(line, '\t'), *f2 = f1 ? strchr(f1 + 1, '\t') : NULL;
    if (f1) *f1 = 0;
    if (f2) *f2 = 0;
    m->t[m->n] = line; m->sub[m->n] = f1 ? f1 + 1 : ""; m->act[m->n] = f2 ? f2 + 1 : "";
    m->n++;
  }
}
static void rm_load(Window *w) {
  RMenu *m = window_get_user_data(w);
  Layer *root = window_get_root_layer(w);
  m->ml = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(m->ml, m, (MenuLayerCallbacks){
    .get_num_sections = rm_secs, .get_num_rows = rm_rows, .draw_row = rm_draw,
    .get_header_height = rm_header_h, .draw_header = rm_header, .select_click = rm_select });
  menu_layer_set_normal_colors(m->ml, COL_BG, COL_TEXT);
  menu_layer_set_highlight_colors(m->ml, COL_FILL, COL_ON_FILL);
  menu_layer_set_click_config_onto_window(m->ml, w);
  layer_add_child(root, menu_layer_get_layer(m->ml));
}
static void rm_unload(Window *w) {
  RMenu *m = window_get_user_data(w);
  menu_layer_destroy(m->ml); m->ml = NULL;
  if (m->buf) { free(m->buf); m->buf = NULL; }
  m->n = 0;
  window_destroy(w); m->win = NULL;
  s_rm_depth--;
  // tell the phone which page (if any) is still showing, so it keeps that one up to date
  send_text(T_MENU_CLOSE, s_rm_depth > 0 ? s_rm[s_rm_depth - 1].page : "");
}
static void open_remote(const char *page) {
  if (s_rm_depth >= RM_MAX) return;
  RMenu *m = &s_rm[s_rm_depth++];
  memset(m, 0, sizeof(*m));
  strncpy(m->page, page, sizeof(m->page) - 1);
  strncpy(m->title, "Loading", sizeof(m->title) - 1);
  m->win = window_create();
  window_set_user_data(m->win, m);
  window_set_background_color(m->win, COL_BG);
  window_set_window_handlers(m->win, (WindowHandlers){ .load = rm_load, .unload = rm_unload });
  window_stack_push(m->win, true);
  send_text(T_MENU_REQ, page);
}
static void rm_receive(const char *page, const char *title, const char *rows) {
  for (int i = s_rm_depth - 1; i >= 0; i--) {
    RMenu *m = &s_rm[i];
    if (strcmp(m->page, page)) continue;
    strncpy(m->title, title, sizeof(m->title) - 1);
    rm_parse(m, rows);
    if (m->ml) menu_layer_reload_data(m->ml);
    return;
  }
}
static void rm_redraw(void) {
  for (int i = 0; i < s_rm_depth; i++) if (s_rm[i].ml) layer_mark_dirty(menu_layer_get_layer(s_rm[i].ml));
}

// ---------------- climate screen ----------------
static Window *s_climate_win;
static int s_pend[2] = { NO_TEMP, NO_TEMP };  // pending driver / passenger temps (c10)
static int s_side;                             // 0 driver, 1 passenger (split mode)
#ifdef HAS_TOUCH
static GRect s_cl_pill, s_cl_chip[2], s_cl_hint;
static int s_cl_tcy, s_cl_areaw;
#endif

static int shown_temp(int side) {
  if (s_pend[side] != NO_TEMP) return s_pend[side];
  return side ? (s.pass_temp ? s.pass_temp : s.set_temp) : s.set_temp;
}
static bool pending(void) { return s_pend[0] != NO_TEMP || s_pend[1] != NO_TEMP; }

static void chevron(GContext *ctx, GPoint c, bool up, int w, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  int h = w / 2;
  if (up) { graphics_draw_line(ctx, GPoint(c.x - w, c.y + h / 2), GPoint(c.x, c.y - h / 2)); graphics_draw_line(ctx, GPoint(c.x, c.y - h / 2), GPoint(c.x + w, c.y + h / 2)); }
  else { graphics_draw_line(ctx, GPoint(c.x - w, c.y - h / 2), GPoint(c.x, c.y + h / 2)); graphics_draw_line(ctx, GPoint(c.x, c.y + h / 2), GPoint(c.x + w, c.y - h / 2)); }
}

static void chip(GContext *ctx, GRect r, const char *label, bool on) {
  graphics_context_set_fill_color(ctx, on ? COL_FILL : COL_CARD);
  graphics_fill_rect(ctx, r, r.size.h / 2, GCornersAll);
#ifdef PBL_BW
  if (!on) { graphics_context_set_stroke_color(ctx, GColorWhite); graphics_context_set_stroke_width(ctx, 1); graphics_draw_round_rect(ctx, r, r.size.h / 2); }
#endif
  text_at(ctx, label, f14b, GRect(r.origin.x, r.origin.y + (r.size.h - 18) / 2 - 1, r.size.w, 18), GTextAlignmentCenter, on ? COL_ON_FILL : COL_TEXT);
}

static void climate_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  bool round = PBL_IF_ROUND_ELSE(true, false);
  bool large = W >= 200;
  int side = round ? 0 : 16;
  int areaw = W - side;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // title + power pill
  int ty = round ? H / 10 : 6;
  text_at(ctx, "Climate", f18b, GRect(round ? 0 : 8, ty - 2, round ? W : W / 2, 22), round ? GTextAlignmentCenter : GTextAlignmentLeft, COL_TEXT);
  int pw = 42, ph = 18;
  GRect pill = round ? GRect((W - pw) / 2, ty + 20, pw, ph) : GRect(areaw - pw - 6, ty + 1, pw, ph);
  chip(ctx, pill, s.climate ? "ON" : "OFF", s.climate);
#ifdef HAS_TOUCH
  s_cl_pill = pill; s_cl_areaw = areaw;
  s_cl_chip[0] = s_cl_chip[1] = GRect(0, 0, 0, 0);
#endif

  int tcy = round ? H * 47 / 100 : H * 44 / 100;
#ifdef HAS_TOUCH
  s_cl_tcy = tcy;
  s_cl_hint = GRect(0, tcy + 44, W, 26);
#endif
  char t[12];
  bool pend = pending();
  if (!s.split) {
    fmt_temp(t, sizeof(t), shown_temp(0));
    GColor tc = (s.climate || pend) ? hvac_color(hvac_dir(shown_temp(0)), COL_TEXT) : COL_DIM;
    text_at(ctx, t, f_big, GRect(0, tcy - 27, areaw, 50), GTextAlignmentCenter, tc);
    chevron(ctx, GPoint(areaw / 2, tcy - 38), true, large ? 20 : 17, pend ? COL_ACCENT : COL_DIM);
    chevron(ctx, GPoint(areaw / 2, tcy + 38), false, large ? 20 : 17, pend ? COL_ACCENT : COL_DIM);
  } else {
    for (int sd = 0; sd < 2; sd++) {
      int cx = areaw * (sd ? 74 : 26) / 100;
      bool act = sd == s_side;
      text_at(ctx, sd ? "Passenger" : "Driver", f14b, GRect(cx - 40, tcy - 48, 80, 18), GTextAlignmentCenter, act ? COL_ACCENT : COL_FAINT);
      fmt_temp(t, sizeof(t), shown_temp(sd));
      text_at(ctx, t, f28b, GRect(cx - 40, tcy - 18, 80, 34), GTextAlignmentCenter,
              (s.climate || s_pend[sd] != NO_TEMP) ? hvac_color(hvac_dir(shown_temp(sd)), COL_TEXT) : COL_DIM);
      if (act) {
        chevron(ctx, GPoint(cx, tcy - 26), true, 13, s_pend[sd] != NO_TEMP ? COL_ACCENT : COL_DIM);
        chevron(ctx, GPoint(cx, tcy + 28), false, 13, s_pend[sd] != NO_TEMP ? COL_ACCENT : COL_DIM);
      }
    }
  }

  // inside/outside or "press select" hint
  int iy = tcy + 48;
  if (pend) {
    text_at(ctx, "Select to set", f18b, GRect(round ? W / 8 : 2, iy - 3, round ? W * 3 / 4 : areaw - 4, 22), GTextAlignmentCenter, COL_ACCENT);
  } else {
    char in[10], out[10], line[40];
    fmt_temp(in, sizeof(in), s.in_temp);
    fmt_temp(out, sizeof(out), s.out_temp);
    snprintf(line, sizeof(line), "In %s   Out %s", in, out);
    text_at(ctx, line, f14b, GRect(round ? W / 8 : 2, iy, round ? W * 3 / 4 : areaw - 4, 18), GTextAlignmentCenter, COL_DIM);
  }
  // chips
  int chy = iy + 22;
  if (chy + 18 <= H - (round ? H / 7 : 2)) {
    int cw = round ? W / 3 : (areaw - 14) / 2;
    GRect c1 = GRect(round ? W / 2 - cw - 2 : 5, chy, cw, 18), c2 = GRect(round ? W / 2 + 2 : 9 + cw, chy, cw, 18);
    chip(ctx, c1, "Defrost", s.defrost);
    chip(ctx, c2, "Vent", s.windows);
#ifdef HAS_TOUCH
    s_cl_chip[0] = c1; s_cl_chip[1] = c2;
#endif
  }
  // button hints on the right edge
  if (!round) {
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(W - 3, 18, 3, 26), 1, GCornersAll);
    graphics_fill_rect(ctx, GRect(W - 3, H / 2 - 13, 3, 26), 1, GCornersAll);
    graphics_fill_rect(ctx, GRect(W - 3, H - 44, 3, 26), 1, GCornersAll);
    text_at(ctx, "+", f18b, GRect(W - side - 2, 18, side, 22), GTextAlignmentCenter, COL_DIM);
    text_at(ctx, "-", f18b, GRect(W - side - 2, H - 46, side, 22), GTextAlignmentCenter, COL_DIM);
    if (pend) {
      graphics_context_set_stroke_color(ctx, COL_ACCENT);
      graphics_context_set_stroke_width(ctx, 2);
      int x = W - side / 2 - 2, y = H / 2;
      graphics_draw_line(ctx, GPoint(x - 4, y), GPoint(x - 1, y + 3));
      graphics_draw_line(ctx, GPoint(x - 1, y + 3), GPoint(x + 4, y - 3));
    } else {
      graphics_context_set_fill_color(ctx, COL_DIM);
      graphics_fill_circle(ctx, GPoint(W - side / 2 - 2, H / 2), 3);
    }
  }
  draw_toast(ctx, b);
}

static void temp_step(int dir) {
  int sd = s.split ? s_side : 0;
  int cur = shown_temp(sd);
  if (cur == NO_TEMP || cur == 0) cur = 210;
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
  s_pend[sd] = next;
  redraw();
}
static void apply_temps(void) {
  char what[28], t1[10], t2[10];
  if (!s.split) {
    int c10 = s_pend[0];
    s.set_temp = s.pass_temp = c10;
    fmt_temp(t1, sizeof(t1), c10);
    snprintf(what, sizeof(what), "Setting %s", t1);
    send_cmd(C_SET_TEMP, c10, what);
  } else {
    int d = shown_temp(0), p = shown_temp(1);
    s.set_temp = d; s.pass_temp = p;
    fmt_temp(t1, sizeof(t1), d); fmt_temp(t2, sizeof(t2), p);
    snprintf(what, sizeof(what), "Setting %s / %s", t1, t2);
    send_cmd(C_SET_TEMPS, ((int32_t)d << 16) | (p & 0xffff), what);
  }
  s_pend[0] = s_pend[1] = NO_TEMP;
}
static void cl_up(ClickRecognizerRef r, void *c) { temp_step(+1); }
static void cl_down(ClickRecognizerRef r, void *c) { temp_step(-1); }
static void cl_select(ClickRecognizerRef r, void *c) {
  if (pending()) { apply_temps(); return; }
  if (s.split) { s_side = !s_side; redraw(); return; }
  if (s.climate) send_cmd(C_CLIMATE_OFF, 0, "Climate off");
  else send_cmd(C_CLIMATE_ON, 0, "Climate on");
}

static const char *copt_title(int row) {
  static const char *t[] = { "Climate", "Defrost car", "Windows", "Split temperature" };
  return t[row];
}
static const char *copt_sub(int row) {
  switch (row) {
    case 0: return s.climate ? "On - select to turn off" : "Off - select to turn on";
    case 1: return s.defrost ? "On - select to turn off" : "Off - select for max defrost";
    case 2: return s.windows ? "Vented - select to close" : "Closed - select to vent";
    default: return s.split ? "On - driver & passenger" : "Off - one temperature";
  }
}
static void copt_pick(int row) {
  switch (row) {
    case 0: if (s.climate) send_cmd(C_CLIMATE_OFF, 0, "Climate off"); else send_cmd(C_CLIMATE_ON, 0, "Climate on"); break;
    case 1: send_cmd(s.defrost ? C_DEFROST_OFF : C_DEFROST_ON, 0, s.defrost ? "Defrost off" : "Defrost on"); break;
    case 2: send_cmd(s.windows ? C_CLOSE_WIN : C_VENT, 0, s.windows ? "Closing windows" : "Venting windows"); break;
    default: set_split(!s.split); s_side = 0; break;
  }
}
static void cl_long(ClickRecognizerRef r, void *c) {
  open_menu((MenuSpec){ .title = "Climate", .rows = 4, .row_title = copt_title, .row_sub = copt_sub, .pick = copt_pick });
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
#ifdef HAS_TOUCH
  attach_touch(w, climate_tap, true);
#endif
}
static void climate_unload(Window *w) {
  s_pend[0] = s_pend[1] = NO_TEMP;  // leaving without pressing Select sends nothing
  layer_destroy(s_climate_layer); s_climate_layer = NULL;
  window_destroy(w); s_climate_win = NULL;
}
static void open_climate(void) {
  s_pend[0] = s_pend[1] = NO_TEMP;
  s_side = 0;
  s_climate_win = window_create();
  window_set_background_color(s_climate_win, COL_BG);
  window_set_click_config_provider(s_climate_win, climate_clicks);
  window_set_window_handlers(s_climate_win, (WindowHandlers){ .load = climate_load, .unload = climate_unload });
  window_stack_push(s_climate_win, true);
}

// ---------------- seats & heat screen (top-down car) ----------------
enum { I_WHEEL, I_SEAT_D, I_SEAT_P, I_SEAT_RL, I_SEAT_RC, I_SEAT_RR, I_SEAT_3L, I_SEAT_3R, I_WINDSHIELD, I_REAR_WIN, I_COUNT };
// Which seat map: 0 Model 3, 1 S, 2 Y, 3 Y 7-seat, 4 X 6-seat, 5 X 7-seat, 6 Cybertruck
static int seat_layout(void) {
  switch (s.model) {
    case 1: return 1;
    case 3: return s.layout == 2 ? 4 : 5;
    case 4: return s.layout == 1 ? 3 : 2;
    case 5: return 6;
    default: return 0;
  }
}
static Window *s_seat_win;
static int s_item = I_SEAT_D;
static int s_menu_seat;  // seat index for the level menu

static bool item_visible(int i) {
  if (SEAT_BOXES[seat_layout()][i][2] == 0) return false;
  if (i >= I_SEAT_RL && i <= I_SEAT_RR && !s.has_rear && s.has_data) return false;
  if (i == I_SEAT_RC && !s.has_rear_c) return false;
  if ((i == I_SEAT_3L || i == I_SEAT_3R) && !s.has_3rd && s.has_data) return false;
  return true;
}

static const char *item_name(int i) {
  static const char *n[] = { "Steering wheel", "Driver seat", "Passenger seat", "Rear left", "Rear middle", "Rear right",
                             "Third row left", "Third row right", "Windshield", "Rear window" };
  if (i == I_REAR_WIN && is_ct()) return "Tonneau";
  if (s.model == 3 && s.layout == 2 && (i == I_SEAT_RL || i == I_SEAT_RR)) return i == I_SEAT_RL ? "Middle left" : "Middle right";
  return n[i];
}
static void item_state(int i, char *buf, size_t n) {
  static const char *lv[] = { "Off", "Low", "Medium", "High" };
  if (i == I_REAR_WIN && is_ct()) {
    snprintf(buf, n, "%s", s.tonneau == 0 ? "Closed" : s.tonneau == 1 ? "Open" : s.tonneau == 2 ? "Partly open"
             : s.tonneau == 5 ? "Opening" : s.tonneau == 6 ? "Closing" : "Select for options");
  } else if (i == I_WHEEL) snprintf(buf, n, "%s", s.wheel ? "Heat on" : "Off");
  else if (i == I_WINDSHIELD || i == I_REAR_WIN) snprintf(buf, n, "%s", (i == I_WINDSHIELD ? s.defrost : (s.rear_defrost || s.defrost)) ? "Defrosting" : "Off");
  else {
    int k = i - I_SEAT_D;
    if (k < 2 && s.cool[k]) snprintf(buf, n, "Cool %s", lv[s.cool[k] > 3 ? 3 : s.cool[k]]);
    else if (s.seat[k]) snprintf(buf, n, "Heat %s", lv[s.seat[k] > 3 ? 3 : s.seat[k]]);
    else snprintf(buf, n, "Off");
  }
}

static GBitmap *s_seat_bmp;
#ifdef HAS_TOUCH
static GRect s_seat_hit[I_COUNT];
#endif

static void seat_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  bool round = PBL_IF_ROUND_ELSE(true, false);
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  static const uint32_t maps[7] = { RESOURCE_ID_SEATS0, RESOURCE_ID_SEATS1, RESOURCE_ID_SEATS2, RESOURCE_ID_SEATS3,
                                    RESOURCE_ID_SEATS4, RESOURCE_ID_SEATS5, RESOURCE_ID_SEATS6 };
  int lay = seat_layout();
  if (!s_seat_bmp) s_seat_bmp = gbitmap_create_with_resource(maps[lay]);
  const int16_t (*SEAT_BOX)[4] = SEAT_BOXES[lay];
  GSize ms = gbitmap_get_bounds(s_seat_bmp).size;
  int mx = (W - ms.w) / 2, my = round ? (H - ms.h) / 2 - H / 14 : 2;
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  graphics_draw_bitmap_in_rect(ctx, s_seat_bmp, GRect(mx, my, ms.w, ms.h));
  int k = ms.w;  // scale = k / 200

  for (int i = 0; i < I_COUNT; i++) {
    if (!item_visible(i)) continue;
    GRect r = GRect(mx + SEAT_BOX[i][0] * k / 200, my + SEAT_BOX[i][1] * k / 200, SEAT_BOX[i][2] * k / 200, SEAT_BOX[i][3] * k / 200);
    GPoint c = grect_center_point(&r);
    int sz = r.size.w < r.size.h ? r.size.w : r.size.h;
    if (i == I_WHEEL) {
      if (s.wheel) { graphics_context_set_stroke_color(ctx, COL_HEAT); graphics_context_set_stroke_width(ctx, 2); graphics_draw_circle(ctx, c, r.size.w / 2); }
    } else if (i == I_REAR_WIN && is_ct()) {
      if (s.tonneau == 1 || s.tonneau == 2 || s.tonneau == 5) {  // open: show the bed in accent colour
        graphics_context_set_stroke_color(ctx, COL_ACCENT);
        graphics_context_set_stroke_width(ctx, 1);
        for (int y = r.origin.y + 2; y < r.origin.y + r.size.h - 1; y += 4)
          graphics_draw_line(ctx, GPoint(r.origin.x + 3, y), GPoint(r.origin.x + r.size.w - 4, y));
      }
    } else if (i == I_WINDSHIELD || i == I_REAR_WIN) {
      bool on = i == I_WINDSHIELD ? s.defrost : (s.rear_defrost || s.defrost);
      if (on) {
        graphics_context_set_stroke_color(ctx, COL_HEAT);
        graphics_context_set_stroke_width(ctx, 1);
        for (int y = r.origin.y + 1; y < r.origin.y + r.size.h; y += 3)
          graphics_draw_line(ctx, GPoint(r.origin.x + 3, y), GPoint(r.origin.x + r.size.w - 4, y));
      }
    } else {
      int seat = i - I_SEAT_D;
      int lvl;
      if (seat < 2 && s.cool[seat]) { lvl = s.cool[seat] > 3 ? 3 : s.cool[seat]; icon_cool(ctx, c, sz + 4, lvl, COL_COOL); }
      else if (s.seat[seat]) { lvl = s.seat[seat] > 3 ? 3 : s.seat[seat]; icon_heat(ctx, c, sz, lvl, COL_HEAT); }
    }
    if (i == s_item) {
      graphics_context_set_stroke_color(ctx, COL_ACCENT);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_round_rect(ctx, grect_inset(r, GEdgeInsets(-2)), 4);
    }
#ifdef HAS_TOUCH
    s_seat_hit[i] = r;
#endif
  }

  // label
  char st[24];
  item_state(s_item, st, sizeof(st));
  int ly = my + ms.h + (round ? 0 : 2);
  if (ly + 36 > H) ly = H - 36;
  text_at(ctx, item_name(s_item), f18b, GRect(round ? W / 6 : 2, ly - 3, round ? W * 2 / 3 : W - 4, 22), GTextAlignmentCenter, COL_TEXT);
  text_at(ctx, st, f14b, GRect(round ? W / 6 : 2, ly + 16, round ? W * 2 / 3 : W - 4, 18), GTextAlignmentCenter, COL_DIM);
  draw_toast(ctx, b);
}

static const char *lvl_title(int row) {
  static const char *t[] = { "Off", "Heat low", "Heat medium", "Heat high", "Cool low", "Cool medium", "Cool high" };
  return t[row];
}
static void lvl_pick(int row) {
  int k = s_menu_seat;
  static const char *what[] = { "Seat off", "Heat low", "Heat medium", "Heat high", "Cool low", "Cool medium", "Cool high" };
  if (row == 0) {
    if (k < 2 && s.cool[k]) send_cmd(C_COOL, (k << 4) | 0, what[0]);
    else send_cmd(C_SEAT, (k << 4) | 0, what[0]);
  } else if (row <= 3) send_cmd(C_SEAT, (k << 4) | row, what[row]);
  else send_cmd(C_COOL, (k << 4) | (row - 3), what[row]);
}
static void seat_select(ClickRecognizerRef r, void *c) {
  switch (s_item) {
    case I_WHEEL: send_cmd(s.wheel ? C_WHEEL_OFF : C_WHEEL_ON, 0, s.wheel ? "Wheel heat off" : "Wheel heat on"); break;
    case I_REAR_WIN:
      if (is_ct()) { open_remote("cargo"); break; }
      // fall through
    case I_WINDSHIELD:
      // Tesla only offers one defrost switch for both windshields.
      send_cmd(s.defrost ? C_DEFROST_OFF : C_DEFROST_ON, 0, s.defrost ? "Defrost off" : "Defrosting car");
      break;
    default: {
      s_menu_seat = s_item - I_SEAT_D;
      int rows = (s_menu_seat < 2 && s.can_cool) ? 7 : 4;
      open_menu((MenuSpec){ .title = item_name(s_item), .rows = rows, .row_title = lvl_title, .row_sub = NULL, .pick = lvl_pick });
    }
  }
}
static void seat_move(int dir) {
  do { s_item = (s_item + I_COUNT + dir) % I_COUNT; } while (!item_visible(s_item));
  redraw();
}
static void seat_up(ClickRecognizerRef r, void *c) { seat_move(-1); }
static void seat_down(ClickRecognizerRef r, void *c) { seat_move(+1); }
static void seat_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, seat_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, seat_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, seat_select);
}
static void seat_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_seat_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_seat_layer, seat_update);
  layer_add_child(root, s_seat_layer);
#ifdef HAS_TOUCH
  attach_touch(w, seat_tap, true);
#endif
}
static void seat_unload(Window *w) {
  layer_destroy(s_seat_layer); s_seat_layer = NULL;
  if (s_seat_bmp) { gbitmap_destroy(s_seat_bmp); s_seat_bmp = NULL; }
  window_destroy(w); s_seat_win = NULL;
}
static void open_seats(void) {
  s_item = I_SEAT_D;
  s_seat_win = window_create();
  window_set_background_color(s_seat_win, COL_BG);
  window_set_click_config_provider(s_seat_win, seat_clicks);
  window_set_window_handlers(s_seat_win, (WindowHandlers){ .load = seat_load, .unload = seat_unload });
  window_stack_push(s_seat_win, true);
}

// ---------------- main screen ----------------
enum { B_CLIMATE, B_SEAT, B_FRUNK, B_TRUNK, B_LOCK, B_COUNT, B_MORE = B_COUNT };
static Window *s_main_win;
static int s_sel = B_CLIMATE;

static const char *state_label(void) {
  switch (s.car_state) {
    case S_ONLINE: {
      static char drive[24];
      if (s.shift == 1 || s.shift == 3) { snprintf(drive, sizeof(drive), "Driving %d %s", s.speed, is_mi() ? "mph" : "km/h"); return drive; }
      if (s.shift == 2) return "Reversing";
      if (s.shift == 4) return s.charging == 2 ? "Charging" : "Awake";
      return s.charging == 2 ? "Charging" : "Parked";
    }
    case S_ASLEEP: return "Asleep";
    case S_OFFLINE: return "Offline";
    case S_SETUP: return "Set up on phone";
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
    default: return COL_DIM;
  }
}
static int seat_mode(void) {
  bool heat = s.wheel, cool = s.cool[0] || s.cool[1];
  for (int i = 0; i < 7; i++) if (s.seat[i]) heat = true;
  return heat && cool ? 2 : heat ? 1 : cool ? -1 : 0;
}
static bool any_seat_on(void) {
  for (int i = 0; i < 7; i++) if (s.seat[i]) return true;
  return s.cool[0] || s.cool[1] || s.wheel;
}
static bool button_active(int b) {
  if (!s.has_data) return false;
  switch (b) {
    case B_CLIMATE: return s.climate;
    case B_SEAT: return any_seat_on();
    case B_FRUNK: return s.frunk;
    case B_TRUNK: return s.trunk || (is_ct() && (s.tonneau == 1 || s.tonneau == 2 || s.tonneau == 5));
    default: return !s.locked;
  }
}
static void button_label(int b, char *buf, size_t n) {
  char t[10];
  switch (b) {
    case B_CLIMATE:
      if (s.climate) { fmt_temp(t, sizeof(t), s.set_temp); snprintf(buf, n, "%s %s", s.defrost ? "Defrosting" : hvac_dir(s.set_temp) > 0 ? "Heating to" : "Cooling to", t); }
      else snprintf(buf, n, "Climate off");
      break;
    case B_SEAT: { int sm = seat_mode(); snprintf(buf, n, "%s", sm == 2 ? "Seats heating & cooling" : sm == 1 ? "Seat heat on" : sm == -1 ? "Seat cooling on" : "Seats"); break; }
    case B_FRUNK: snprintf(buf, n, "%s", s.frunk ? "Frunk open" : "Open frunk"); break;
    case B_TRUNK:
      if (is_ct()) {
        bool tn = s.tonneau == 1 || s.tonneau == 2 || s.tonneau == 5;
        snprintf(buf, n, "%s", s.trunk && tn ? "Tailgate & tonneau open" : s.trunk ? "Tailgate open" : tn ? "Tonneau open" : "Cargo");
      } else snprintf(buf, n, "%s", s.trunk ? "Trunk open" : "Open trunk");
      break;
    case B_LOCK: snprintf(buf, n, "%s", (s.locked || !s.has_data) ? "Locked" : "Unlocked"); break;
    default: snprintf(buf, n, "More"); break;
  }
}

// Draw the state dot under a button: red (heat), blue (cool/active), or half and half.
static void state_dot(GContext *ctx, GPoint p, GColor left, GColor right) {
  graphics_context_set_fill_color(ctx, left);
  graphics_fill_rect(ctx, GRect(p.x - 2, p.y - 2, 2, 5), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, right);
  graphics_fill_rect(ctx, GRect(p.x, p.y - 2, 2, 5), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, left);
  graphics_fill_rect(ctx, GRect(p.x - 1, p.y - 3, 1, 7), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, right);
  graphics_fill_rect(ctx, GRect(p.x, p.y - 3, 1, 7), 0, GCornerNone);
}
static void draw_icon_split(GContext *ctx, int i, GPoint c, GColor left, GColor right) {
  // draw the left half in one colour and the right half in another
  GBitmap *b = s_icon[i];
  if (!b) return;
  GSize sz = gbitmap_get_bounds(b).size;
  GBitmap *lh = gbitmap_create_as_sub_bitmap(b, GRect(0, 0, sz.w / 2, sz.h));
  GBitmap *rh = gbitmap_create_as_sub_bitmap(b, GRect(sz.w / 2, 0, sz.w - sz.w / 2, sz.h));
  GBitmap *parts[2] = { lh, rh };
  GColor cols[2] = { left, right };
  for (int k = 0; k < 2; k++) {
#ifdef PBL_COLOR
    GBitmapFormat f = gbitmap_get_format(b);
    int n = f == GBitmapFormat1BitPalette ? 2 : f == GBitmapFormat2BitPalette ? 4 : f == GBitmapFormat4BitPalette ? 16 : 0;
    GColor *pal = gbitmap_get_palette(b);
    for (int j = 0; j < n && pal; j++) if (pal[j].a) pal[j] = (GColor){ .argb = (uint8_t)(cols[k].argb | 0xC0) };
#endif
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, parts[k], GRect(c.x - sz.w / 2 + (k ? sz.w / 2 : 0), c.y - sz.h / 2, k ? sz.w - sz.w / 2 : sz.w / 2, sz.h));
  }
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  gbitmap_destroy(lh); gbitmap_destroy(rh);
}
static void draw_button(GContext *ctx, GPoint c, int r, int b, bool sel, bool plates) {
  bool active = button_active(b);
  GColor bg = sel ? COL_FILL : COL_CARD;
  // what colour says about this control: heat red, cool blue, otherwise the normal accent
  GColor lc = COL_ACCENT, rc = COL_ACCENT;
  if (b == B_CLIMATE && active) lc = rc = hvac_color(hvac_dir(s.set_temp), COL_ACCENT);
  if (b == B_SEAT && active) {
    int sm = seat_mode();
    lc = sm == -1 ? COL_COOL : COL_HEAT;
    rc = sm == 1 ? COL_HEAT : sm == 2 ? COL_COOL : COL_COOL;
    if (sm == 1) rc = COL_HEAT;
  }
#ifdef PBL_COLOR
  if (sel || plates) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_circle(ctx, c, r);
  }
  if (sel) draw_icon(ctx, b, c, COL_ON_FILL, false);
  else if (!active) draw_icon(ctx, b, c, COL_TEXT, false);
  else if (b == B_SEAT && seat_mode() == 2) draw_icon_split(ctx, b, c, lc, rc);
  else draw_icon(ctx, b, c, lc, false);
#else
  (void)lc; (void)rc; (void)bg;
  draw_icon(ctx, b, c, GColorWhite, false);
  // black & white: ring drawn after the icon so the icon's black box can't cover it
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  if (sel || plates) graphics_draw_circle(ctx, c, r - 1);
  if (sel) { graphics_draw_circle(ctx, c, r); graphics_draw_circle(ctx, c, r - 2); }
#endif
  if (active) state_dot(ctx, GPoint(c.x, c.y + r + 5), PBL_IF_COLOR_ELSE(lc, GColorWhite), PBL_IF_COLOR_ELSE(rc, GColorWhite));
}

#ifdef HAS_TOUCH
static GPoint s_btn_c[5];
static GRect s_more_rect, s_car_rect;
static int s_btn_r;
static int s_top_bottom;  // y below which the status rows end
#endif

static void main_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;
  bool round = PBL_IF_ROUND_ELSE(true, false);
  bool large = W >= 200;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  GFont top_f = large ? f24b : f18b;
  int top_h = large ? 26 : 20;

  // --- top row: battery, percentage and range on the left; "More" on the right ---
  char pct[8], rng[16], tin[10];
  if (s.has_data) snprintf(pct, sizeof(pct), "%d%%", s.battery); else snprintf(pct, sizeof(pct), "--%%");
  if (s.has_data) snprintf(rng, sizeof(rng), "%d %s", s.range, is_mi() ? "mi" : "km"); else snprintf(rng, sizeof(rng), "--");
  fmt_temp(tin, sizeof(tin), s.has_data ? s.in_temp : NO_TEMP);
  bool chg = s.charging == 2 && s.has_data;
  int bw = large ? 26 : 19, bh = large ? 13 : 10;
  int gap = large ? 10 : 5;
  GFont rng_f = large ? top_f : f18;
  int cluster_w = bw + 5 + text_w(pct, top_f) + gap + text_w(rng, rng_f);
  int row1 = round ? H * 14 / 100 : 4;
  int lx = round ? (W - cluster_w) / 2 : 4;
  int tcy = row1 + top_h / 2 + 2;  // text visual centre
  draw_battery(ctx, GRect(lx, tcy - bh / 2, bw, bh), s.has_data ? s.battery : 0, chg, s_pulse);
  int px = lx + bw + 4;
  text_at(ctx, pct, top_f, GRect(px, row1 - (large ? 3 : 2), 70, top_h + 4), GTextAlignmentLeft, COL_TEXT);
  int rx = px + text_w(pct, top_f) + gap;
  text_at(ctx, rng, rng_f, GRect(rx, row1 - (large ? 3 : 2), 90, top_h + 4), GTextAlignmentLeft, COL_DIM);

  // "More" button
  int mw = large ? 50 : 35, mh = large ? 22 : 18;
  GRect more = round ? GRect((W - mw) / 2, H * 3 / 100, mw, mh) : GRect(W - mw - 2, 4, mw, mh);
  bool msel = s_sel == B_MORE;
  graphics_context_set_fill_color(ctx, msel ? COL_FILL : COL_CARD);
  graphics_fill_rect(ctx, more, mh / 2, GCornersAll);
#ifdef PBL_BW
  graphics_context_set_stroke_color(ctx, GColorWhite); graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, more, mh / 2);
  if (msel) graphics_draw_round_rect(ctx, grect_inset(more, GEdgeInsets(1)), mh / 2 - 1);
#endif
  text_at(ctx, "More", f14b, GRect(more.origin.x, more.origin.y + (mh - 18) / 2 - 1, mw, 18), GTextAlignmentCenter,
          PBL_IF_COLOR_ELSE(msel ? COL_ON_FILL : COL_TEXT, COL_TEXT));
#ifdef HAS_TOUCH
  s_more_rect = more;
#endif

  // --- second row: inside temperature under the battery; status + distance on the right ---
  char line[48], dist[20];
  fmt_dist(dist, sizeof(dist));
  if (s.car_state == S_ONLINE && s.shift >= 1 && s.shift <= 3) dist[0] = 0;
  const char *st = state_label();
  if (s.car_state == S_ASLEEP && s.updated) {
    int mins = (time(NULL) - s.updated) / 60;
    char ago[12];
    if (mins < 60) snprintf(ago, sizeof(ago), "%dm ago", mins);
    else if (mins < 60 * 48) snprintf(ago, sizeof(ago), "%dh ago", mins / 60);
    else snprintf(ago, sizeof(ago), "%dd ago", mins / 1440);
    snprintf(line, sizeof(line), "%s %s%s%s", st, ago, dist[0] ? " · " : "", dist);
  } else snprintf(line, sizeof(line), "%s%s%s", st, (dist[0] && st[0]) ? " · " : "", dist);
  int row2 = row1 + top_h + (large ? 2 : 0);
  if (round) {
    char both[64];
    snprintf(both, sizeof(both), "%s%s%s", s.has_data ? tin : "", (s.has_data && line[0]) ? " · " : "", line);
    if (text_w(both, f14b) > W * 3 / 4) snprintf(both, sizeof(both), "%s%s%s", s.has_data ? tin : "", (s.has_data && dist[0]) ? " · " : "", dist[0] ? dist : st);
    text_at(ctx, both, f14b, GRect(W / 8, row2, W * 3 / 4, 18), GTextAlignmentCenter, state_color());
  } else {
    GFont tf = large ? f18b : f14b;
    int tw = s.has_data ? text_w(tin, tf) : 0;
    if (s.has_data) text_at(ctx, tin, tf, GRect(lx, row2 - (large ? 2 : 0), tw + 4, 22), GTextAlignmentLeft, COL_DIM);
    int avail = W - 4 - (lx + tw + 8);
    if (text_w(line, f14b) > avail) snprintf(line, sizeof(line), "%s", dist[0] ? dist : st);
    text_at(ctx, line, f14b, GRect(W - 4 - avail, row2 + (large ? 2 : 0), avail, 18), GTextAlignmentRight, state_color());
  }

  // --- control row ---
  bool plates = large;  // gray circles behind every icon only where there is room
  int r = large ? 18 : 15;
  int rowy = round ? H * 73 / 100 : H - (large ? 58 : 44);
  int span = round ? W * 74 / 100 : W - 2 * (r + 1);
  for (int i = 0; i < B_COUNT; i++) {
    int cx = (W - span) / 2 + span * i / (B_COUNT - 1);
    draw_button(ctx, GPoint(cx, rowy), r, i, i == s_sel, plates);
#ifdef HAS_TOUCH
    s_btn_c[i] = GPoint(cx, rowy); s_btn_r = r;
#endif
  }
#ifdef HAS_TOUCH
  s_top_bottom = row2 + 18;
#endif
  char lbl[32];
  button_label(s_sel, lbl, sizeof(lbl));
  if (s.car_state == S_SETUP || s.car_state == S_PAIR || s.car_state == S_ERROR) { if (s.text[0]) snprintf(lbl, sizeof(lbl), "%s", s.text); }
  int ly = rowy + r + (large ? 8 : 5);
  text_at(ctx, lbl, f18b, GRect(round ? W / 6 : 2, ly, round ? W * 2 / 3 : W - 4, 22), GTextAlignmentCenter, COL_TEXT);

  // --- car, centred in the space between the text and the buttons ---
  int am = art_model();
  const CarGeo *g = &CAR_GEO[am];
  int area_top = row2 + 18 + (large ? 6 : 2);
  int area_bot = rowy - r - (large ? 12 : 9);
  int avail_h = area_bot - area_top;
  int bx = (W - g->w) / 2 - (chg ? g->w / 30 : 0);
  int by = area_top + (avail_h - g->h) / 2;  // centred whether or not it is charging
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  graphics_draw_bitmap_in_rect(ctx, bmp_get(0, CAR_RES[am][0][0]), GRect(bx, by, g->w, g->h));
  // moving parts drawn over the closed car: tonneau, then frunk, then trunk/tailgate
  int frames[3] = { s_tr_frame, s_fr_frame, s_tn_frame };
  static const int order[3] = { 2, 1, 0 };
  for (int k = 0; k < 3; k++) {
    int row = order[k], f = frames[row];
    if (!f || !g->patch[row][2] || !CAR_RES[am][row][f]) continue;
    GBitmap *pb = bmp_get(1, CAR_RES[am][row][f]);
    graphics_draw_bitmap_in_rect(ctx, pb, GRect(bx + g->patch[row][0], by + g->patch[row][1], g->patch[row][2], g->patch[row][3]));
  }
#ifdef HAS_TOUCH
  s_car_rect = GRect(bx, by, g->w, g->h);
#endif
  if (chg) draw_charger(ctx, GPoint(bx + g->port[0], by + g->port[1]), rowy - r - 3, s_phase);

  draw_toast(ctx, b);
}

static void m_up(ClickRecognizerRef r, void *c) { s_sel = (s_sel + B_COUNT) % (B_COUNT + 1); redraw(); }
static void m_down(ClickRecognizerRef r, void *c) { s_sel = (s_sel + 1) % (B_COUNT + 1); redraw(); }
static void m_select(ClickRecognizerRef r, void *c) {
  switch (s_sel) {
    case B_CLIMATE: open_climate(); break;
    case B_SEAT: open_seats(); break;
    case B_FRUNK:
      if (s.frunk) toast(TOAST_ERR, "Close by hand", 2500);
      else confirm("Open frunk?", C_FRUNK, "Opening frunk");
      break;
    case B_TRUNK:
      if (is_ct()) open_remote("cargo");  // tailgate + powered tonneau
      else if (s.trunk) confirm("Close trunk?", C_TRUNK_CLOSE, "Closing trunk");
      else confirm("Open trunk?", C_TRUNK, "Opening trunk");
      break;
    case B_MORE: open_remote("root"); break;
    default:
      if (s.locked || !s.has_data) confirm("Unlock car?", C_UNLOCK, "Unlocking");
      else send_cmd(C_LOCK, 0, "Locking");
  }
}
static void m_long(ClickRecognizerRef r, void *c) { open_remote("root"); }

// ---------------- touch (Pebble Time 2 / Round 2) ----------------
#ifdef HAS_TOUCH
static bool in_rect(GPoint p, GRect r, int pad) {
  return p.x >= r.origin.x - pad && p.x < r.origin.x + r.size.w + pad && p.y >= r.origin.y - pad && p.y < r.origin.y + r.size.h + pad;
}
static void back_swipe(const Recognizer *rec, RecognizerEvent e) {
  if (e == RecognizerEvent_Completed && swipe_recognizer_get_direction(rec) == SwipeDirection_Right)
    window_stack_pop(true);
}
static void attach_touch(Window *w, RecognizerEventCb tap, bool back) {
  window_set_touch_bridge_disabled(w, true);
  window_attach_recognizer(w, tap_recognizer_create(tap, NULL));
  if (back) window_attach_recognizer(w, swipe_recognizer_create(back_swipe, NULL, SwipeDirection_Right));
}

static void main_tap(const Recognizer *rec, RecognizerEvent e) {
  if (e != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(rec);
  for (int i = 0; i < B_COUNT; i++) {
    int dx = p.x - s_btn_c[i].x, dy = p.y - s_btn_c[i].y, rr = s_btn_r + 6;
    if (dx * dx + dy * dy <= rr * rr) { s_sel = i; redraw(); m_select(NULL, NULL); return; }
  }
  if (in_rect(p, s_more_rect, 6)) { open_remote("root"); return; }
  if (in_rect(p, s_car_rect, 4)) {
    int x = p.x - s_car_rect.origin.x;
    const CarGeo *g = &CAR_GEO[art_model()];
    if (x < g->patch[1][0] + g->patch[1][2]) { s_sel = B_FRUNK; redraw(); m_select(NULL, NULL); return; }
    if (x > g->patch[0][0]) { s_sel = B_TRUNK; redraw(); m_select(NULL, NULL); return; }
  }
  if (p.y < s_top_bottom) send_refresh(false);
}
static void main_swipe(const Recognizer *rec, RecognizerEvent e) {
  if (e == RecognizerEvent_Completed && swipe_recognizer_get_direction(rec) == SwipeDirection_Up) m_long(NULL, NULL);
}

static void climate_tap(const Recognizer *rec, RecognizerEvent e) {
  if (e != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(rec);
  if (in_rect(p, s_cl_pill, 6)) {
    if (s.climate) send_cmd(C_CLIMATE_OFF, 0, "Climate off"); else send_cmd(C_CLIMATE_ON, 0, "Climate on");
    return;
  }
  if (in_rect(p, s_cl_chip[0], 4)) { copt_pick(1); return; }
  if (in_rect(p, s_cl_chip[1], 4)) { copt_pick(2); return; }
  if (pending() && in_rect(p, s_cl_hint, 0)) { apply_temps(); return; }
  if (p.y < s_cl_tcy - 60 || p.y > s_cl_tcy + 50) return;
  if (s.split) { int sd = p.x < s_cl_areaw / 2 ? 0 : 1; if (sd != s_side) { s_side = sd; redraw(); } }
  if (p.y < s_cl_tcy - 12) temp_step(+1);
  else if (p.y > s_cl_tcy + 16) temp_step(-1);
  else if (pending()) apply_temps();
}

static void seat_tap(const Recognizer *rec, RecognizerEvent e) {
  if (e != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(rec);
  for (int i = 0; i < I_COUNT; i++) {
    if (item_visible(i) && in_rect(p, s_seat_hit[i], 3)) { s_item = i; redraw(); seat_select(NULL, NULL); return; }
  }
}

static void confirm_tap(const Recognizer *rec, RecognizerEvent e) {
  if (e != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(rec);
  GRect b = layer_get_bounds(window_get_root_layer(s_confirm_win));
  if (p.x > b.size.w * 2 / 3 || (p.y > b.size.h / 2 - 50 && p.y < b.size.h / 2 + 20)) confirm_select(NULL, NULL);
}
#endif

static void main_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, m_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, m_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, m_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, m_long, NULL);
}
static void main_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_main_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_main_layer, main_update);
  layer_add_child(root, s_main_layer);
#ifdef HAS_TOUCH
  attach_touch(w, main_tap, false);
  window_attach_recognizer(w, swipe_recognizer_create(main_swipe, NULL, SwipeDirection_Up));
#endif
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
  if (!s.has_data) { s.set_temp = 210; s.pass_temp = 210; s.in_temp = NO_TEMP; s.out_temp = NO_TEMP; s.units = 3; s.locked = true; s.dist = -1; s.model = 2; s.tonneau = -1; }
  s.car_state = S_BUSY;
#ifdef DEMO
  s.has_data = true; s.car_state = S_ONLINE; s.battery = 78; s.range = 241; s.units = 3; s.locked = true;
  s.climate = true; s.set_temp = 211; s.pass_temp = 200; s.in_temp = 185; s.out_temp = 90; s.model = DEMO_MODEL;
  s.seat[0] = 2; s.seat[1] = 0; s.seat[2] = 1; s.cool[1] = DEMO_SPLIT ? 0 : 2; s.can_cool = true; s.has_rear = true; s.wheel = true;
  s.trunk = DEMO_TRUNK; s.frunk = DEMO_TRUNK; s.charging = DEMO_CHARGING ? 2 : 0; s.dist = 480; s.bearing = 40; s.split = DEMO_SPLIT;
  s.layout = DEMO_LAYOUT; s.has_3rd = DEMO_LAYOUT != 0 || DEMO_MODEL == 3; s.has_rear_c = DEMO_MODEL != 2 && !(DEMO_MODEL == 3 && DEMO_LAYOUT == 2);
  s.seat[5] = 2; s.seat[3] = 1; s.tonneau = DEMO_MODEL == 5 ? 1 : -1;
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
  static const uint32_t icons[5] = { RESOURCE_ID_ICON_CLIMATE, RESOURCE_ID_ICON_SEAT_HEAT, RESOURCE_ID_ICON_FRUNK, RESOURCE_ID_ICON_TRUNK, RESOURCE_ID_ICON_LOCK };
  for (int i = 0; i < 5; i++) s_icon[i] = gbitmap_create_with_resource(icons[i]);
#ifdef HAS_TOUCH
  app_touch_navigation_enable(true);  // lets the built-in menus scroll and select by touch
#endif
  app_message_register_inbox_received(inbox);
  app_message_open(1024, 64);

  s_main_win = window_create();
  window_set_background_color(s_main_win, COL_BG);
  window_set_click_config_provider(s_main_win, main_clicks);
  window_set_window_handlers(s_main_win, (WindowHandlers){ .load = main_load, .unload = main_unload });
  window_stack_push(s_main_win, false);
  s_fr_frame = (s.frunk && s.has_data) ? 7 : 0;
  s_tr_frame = (s.trunk && s.has_data) ? 7 : 0;
  pulse_update();
#ifdef DEMO
  if (DEMO_SCREEN == 1) open_climate();
  if (DEMO_SCREEN == 2) open_seats();
  if (DEMO_SCREEN == 3) {
    open_remote("root");
    rm_receive("root", "More", "Charging\tCharging · 1h 25m left\tp:charging\nClimate\tOn · 70°\tp:climate\nSecurity\tSentry Off · Locked\tp:security\nControls\tLights, horn, windows, lids\tp:controls\nCar info\tTires, odometer, software\tp:info");
  }
#endif
}
static void deinit(void) { save_status(); window_destroy(s_main_win); }

int main(void) { init(); app_event_loop(); deinit(); }
