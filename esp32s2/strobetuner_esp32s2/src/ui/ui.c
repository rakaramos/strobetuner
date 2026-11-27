/*
  Strobe tuner UI (240x240 round GC9A01, LVGL 9).

    TUNER     turn = string          press = tuning list    hold = strobe on/off
    LIST      turn = scroll          press = use tuning     hold on Open = set strings,
                                     (Settings opens menu)  hold elsewhere = back
    OPEN      turn = string/Reset/Done, press = edit string (then turn = semitone,
              press = done with it), hold = back to tuner
    MENU      turn = choose setting  press = edit           hold = back to tuner
    EDIT      turn = change value    press or hold = back to menu

  Everything visual lives here. To experiment:
    - colours:  THEMES[]
    - layout:   build_*() functions (positions are px from the top of the circle)
    - settings: SETTINGS[]
  Note: LVGL's built-in Montserrat has ASCII plus LV_SYMBOL_* icons only.
*/
#include "ui.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* ========================================================================== */
/*  Themes                                                                    */
/* ========================================================================== */

typedef struct {
  const char *name;
  uint32_t bg, fg, muted, accent, warn, ring, on_accent;
} ui_theme_t;

static const ui_theme_t THEMES[] = {
  /* name        bg        fg        muted     accent    warn      ring      on_accent */
  {"Midnight", 0x000000, 0xFFFFFF, 0x6B7280, 0x22C55E, 0xEF4444, 0x1F2937, 0x000000},
  {"Amber",    0x0B0704, 0xFFD9A0, 0x8A6A44, 0xFFB020, 0xFF5A1F, 0x2A1C0E, 0x0B0704},
  {"Neon",     0x05010F, 0xE0F2FE, 0x64748B, 0x22D3EE, 0xF472B6, 0x1E1B4B, 0x05010F},
  {"Paper",    0xF4F1EA, 0x1A1A1A, 0x8C8577, 0x0F766E, 0xB91C1C, 0xDCD6C8, 0xFFFFFF},
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))
int ui_theme_count(void) { return THEME_COUNT; }

static tuner_settings_t *S;
static const ui_theme_t *T;
#define C(field) lv_color_hex(T->field)

/* ========================================================================== */
/*  Settings menu                                                             */
/* ========================================================================== */

typedef struct {
  const char *title;
  size_t offset;          /* offsetof(tuner_settings_t, field) */
  int min, max, step, fast_step;
  bool wrap;
  void (*format)(int v, char *val, size_t vn, char *sub, size_t sn);
} setting_def_t;

static void fmt_a4(int v, char *val, size_t vn, char *sub, size_t sn) {
  snprintf(val, vn, "%d.%d", v / 10, v % 10); snprintf(sub, sn, "Hz");
}
static void fmt_flash(int v, char *val, size_t vn, char *sub, size_t sn) {
  snprintf(val, vn, "%d", v); snprintf(sub, sn, "us per flash");
}
static void fmt_trim(int v, char *val, size_t vn, char *sub, size_t sn) {
  int a = v < 0 ? -v : v;
  snprintf(val, vn, "%s%d.%d", v < 0 ? "-" : v > 0 ? "+" : "", a / 10, a % 10);
  snprintf(sub, sn, "ppm");
}
static void fmt_percent(int v, char *val, size_t vn, char *sub, size_t sn) {
  snprintf(val, vn, "%d", v); snprintf(sub, sn, "%%");
}
static void fmt_theme(int v, char *val, size_t vn, char *sub, size_t sn) {
  snprintf(val, vn, "%s", THEMES[v].name); snprintf(sub, sn, "colour scheme");
}
static void fmt_onoff(int v, char *val, size_t vn, char *sub, size_t sn) {
  snprintf(val, vn, "%s", v ? "On" : "Off"); snprintf(sub, sn, "check with a phone tuner");
}

static setting_def_t SETTINGS[] = {
  {"A4 reference", offsetof(tuner_settings_t, a4_tenths),       4300, 4500, 5, 10, false, fmt_a4},
  {"Flash length", offsetof(tuner_settings_t, flash_us),        50, 1000, 10, 50, false, fmt_flash},
  {"Clock trim",   offsetof(tuner_settings_t, trim_tenths_ppm), -1000, 1000, 5, 50, false, fmt_trim},
  {"Brightness",   offsetof(tuner_settings_t, brightness),      10, 100, 5, 10, false, fmt_percent},
  {"Theme",        offsetof(tuner_settings_t, theme),           0, 0, 1, 1, true, fmt_theme},
  {"Ref. tone",    offsetof(tuner_settings_t, ref_tone),        0, 1, 1, 1, true, fmt_onoff},
};
#define SETTING_COUNT ((int)(sizeof(SETTINGS) / sizeof(SETTINGS[0])))
#define MENU_BACK SETTING_COUNT
static int *setting_ptr(const setting_def_t *d) { return (int *)((char *)S + d->offset); }

/* ========================================================================== */
/*  Helpers                                                                   */
/* ========================================================================== */

static lv_obj_t *make_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_scrollable(scr, false);
  lv_obj_set_style_bg_color(scr, C(bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  return scr;
}

/* Centred label whose top edge is `top` px below the top of the circle. */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int top) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "");
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, top);
  return l;
}

static lv_obj_t *make_row(lv_obj_t *parent, int top) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, top);
  return row;
}

static lv_obj_t *row_label(lv_obj_t *row, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(row);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, "");
  return l;
}

static void make_ring(lv_obj_t *parent) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 236, 236);
  lv_obj_center(arc);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_clickable(arc, false);
  lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, C(ring), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
}

/* Bigger steps when the knob is spun fast. */
static int accel_step(const setting_def_t *d) {
  static uint32_t last;
  uint32_t now = lv_tick_get();
  bool fast = (now - last) < 60;
  last = now;
  return fast ? d->fast_step : d->step;
}

/* ---- String "pills" along the bottom arc (shared by tuner and Open editor) ---- */
typedef struct { lv_obj_t *pill[TUNER_STRINGS], *lbl[TUNER_STRINGS]; } pills_t;

static void build_pills(lv_obj_t *parent, pills_t *p) {
  for (int i = 0; i < TUNER_STRINGS; i++) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 28, 28);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, C(accent), 0);
    lv_obj_set_style_border_color(o, C(accent), 0);
    float rad = (140 - i * 20) * 3.14159265f / 180;   /* low string on the left */
    lv_obj_align(o, LV_ALIGN_CENTER, (int)lroundf(90 * cosf(rad)), (int)lroundf(90 * sinf(rad)));
    lv_obj_t *l = lv_label_create(o);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    p->pill[i] = o;
    p->lbl[i] = l;
  }
}

/* sel = highlighted string (-1 none); outline = editing it; vs_standard = colour
   strings that differ from Standard (used for Open). */
static void update_pills(pills_t *p, const int notes[TUNER_STRINGS], int sel, bool outline,
                         bool vs_standard) {
  for (int i = 0; i < TUNER_STRINGS; i++) {
    bool is_sel = i == sel;
    bool changed = vs_standard && notes[i] != tuner_standard_midi(i);
    bool filled = is_sel && !outline;
    lv_obj_set_style_bg_opa(p->pill[i], filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->pill[i], is_sel && outline ? 2 : 0, 0);
    lv_color_t c = filled ? C(on_accent) : (is_sel && outline) ? C(fg) : changed ? C(accent) : C(muted);
    lv_obj_set_style_text_color(p->lbl[i], c, 0);
    lv_label_set_text(p->lbl[i], tuner_note_letter(notes[i]));
  }
}

/* ========================================================================== */
/*  Screens                                                                   */
/* ========================================================================== */

typedef enum { SCR_TUNE, SCR_LIST, SCR_OPEN, SCR_MENU, SCR_EDIT } screen_id_t;
static screen_id_t current = SCR_TUNE;
static int list_sel;          /* 0..TUNING_COUNT-1 tunings, TUNING_COUNT = Settings */
static int open_sel;          /* 0..5 strings, 6 Reset, 7 Done */
static bool open_editing;
static int menu_sel, edit_idx;
#define LIST_COUNT (TUNING_COUNT + 1)
#define OPEN_RESET TUNER_STRINGS
#define OPEN_DONE (TUNER_STRINGS + 1)

/* ---- Tuner ---- */
static lv_obj_t *tune_scr, *t_name, *t_mark, *t_string, *t_note, *t_oct, *t_hz;
static pills_t t_pills;

static void build_tune(void) {
  tune_scr = make_screen();
  make_ring(tune_scr);
  lv_obj_t *row = make_row(tune_scr, 30);
  t_name = row_label(row, &lv_font_montserrat_14, T->muted);
  t_mark = row_label(row, &lv_font_montserrat_14, T->accent);
  lv_label_set_text(t_mark, LV_SYMBOL_EDIT);
  t_string = make_label(tune_scr, &lv_font_montserrat_14, T->muted, 50);
  lv_obj_set_style_text_letter_space(t_string, 2, 0);
  row = make_row(tune_scr, 66);
  t_note = row_label(row, &lv_font_montserrat_48, T->fg);
  t_oct = row_label(row, &lv_font_montserrat_20, T->muted);
  lv_obj_set_style_pad_bottom(t_oct, 6, 0);
  t_hz = make_label(tune_scr, &lv_font_montserrat_20, T->muted, 128);
  build_pills(tune_scr, &t_pills);
}

static void refresh_tune(void) {
  int notes[TUNER_STRINGS], m = tuner_target_midi(S);
  char buf[32];
  tuner_tuning_notes(S, S->tuning, notes);
  lv_label_set_text(t_name, tuner_tuning_name(S->tuning));
  lv_obj_set_hidden(t_mark, !(S->tuning == TUNING_OPEN && tuner_open_is_set(S)));
  snprintf(buf, sizeof buf, "STRING %d", TUNER_STRINGS - S->string_idx);
  lv_label_set_text(t_string, buf);
  lv_label_set_text(t_note, tuner_note_letter(m));
  snprintf(buf, sizeof buf, "%d", tuner_note_octave(m));
  lv_label_set_text(t_oct, buf);
  lv_obj_set_style_text_color(t_note, S->strobe_on ? C(fg) : C(muted), 0);
  if (S->strobe_on) {
    snprintf(buf, sizeof buf, "%.2f Hz", tuner_target_hz(S));
    lv_label_set_text(t_hz, buf);
    lv_obj_set_style_text_color(t_hz, C(muted), 0);
  } else {
    lv_label_set_text(t_hz, LV_SYMBOL_PAUSE "  strobe off");
    lv_obj_set_style_text_color(t_hz, C(warn), 0);
  }
  update_pills(&t_pills, notes, S->string_idx, false, S->tuning == TUNING_OPEN);
}

/* ---- Tuning list ---- */
#define LIST_ROWS 5
static lv_obj_t *list_scr, *l_header, *l_row[LIST_ROWS], *l_preview, *l_hint;
static const int ROW_TOP[LIST_ROWS] = {50, 72, 96, 128, 150};

static void build_list(void) {
  list_scr = make_screen();
  make_ring(list_scr);
  l_header = make_label(list_scr, &lv_font_montserrat_14, T->muted, 26);
  lv_obj_set_style_text_letter_space(l_header, 1, 0);
  for (int i = 0; i < LIST_ROWS; i++) {
    bool mid = i == LIST_ROWS / 2;
    l_row[i] = make_label(list_scr, mid ? &lv_font_montserrat_20 : &lv_font_montserrat_14,
                          mid ? T->fg : T->muted, ROW_TOP[i]);
    if (i == 0 || i == LIST_ROWS - 1) lv_obj_set_style_text_opa(l_row[i], LV_OPA_50, 0);
  }
  l_preview = make_label(list_scr, &lv_font_montserrat_14, T->accent, 176);
  lv_obj_set_style_text_letter_space(l_preview, 2, 0);
  l_hint = make_label(list_scr, &lv_font_montserrat_14, T->muted, 196);
}

static const char *list_item(int i) {
  return i < TUNING_COUNT ? tuner_tuning_name(i) : LV_SYMBOL_SETTINGS " Settings";
}

static void refresh_list(void) {
  char buf[48];
  for (int r = 0; r < LIST_ROWS; r++) {
    int i = list_sel + r - LIST_ROWS / 2;
    lv_label_set_text(l_row[r], i >= 0 && i < LIST_COUNT ? list_item(i) : "");
  }
  if (list_sel < TUNING_COUNT) {
    snprintf(buf, sizeof buf, "TUNING %d OF %d", list_sel + 1, TUNING_COUNT);
    lv_label_set_text(l_header, buf);
    int n[TUNER_STRINGS];
    size_t o = 0;
    tuner_tuning_notes(S, list_sel, n);
    for (int i = 0; i < TUNER_STRINGS; i++)
      o += snprintf(buf + o, sizeof buf - o, i ? " %s" : "%s", tuner_note_letter(n[i]));
    lv_label_set_text(l_preview, buf);
    if (list_sel == TUNING_OPEN)
      lv_label_set_text(l_hint, tuner_open_is_set(S) ? "hold to change strings" : "hold to set strings");
    else
      lv_label_set_text(l_hint, "press to use");
  } else {
    lv_label_set_text(l_header, "");
    lv_label_set_text(l_preview, "");
    lv_label_set_text(l_hint, "press to open");
  }
}

/* ---- Open string editor ---- */
static lv_obj_t *open_scr, *o_label, *o_big, *o_left, *o_right, *o_sub, *o_chip[2], *o_chip_lbl[2];
static pills_t o_pills;

static void build_open(void) {
  open_scr = make_screen();
  make_ring(open_scr);
  lv_obj_t *t = make_label(open_scr, &lv_font_montserrat_14, T->muted, 28);
  lv_label_set_text(t, "Open - set strings");
  o_label = make_label(open_scr, &lv_font_montserrat_14, T->muted, 50);
  lv_obj_set_style_text_letter_space(o_label, 2, 0);
  lv_obj_t *row = make_row(open_scr, 66);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);
  o_left = row_label(row, &lv_font_montserrat_20, T->accent);
  lv_label_set_text(o_left, LV_SYMBOL_LEFT);
  o_big = row_label(row, &lv_font_montserrat_48, T->fg);
  o_right = row_label(row, &lv_font_montserrat_20, T->accent);
  lv_label_set_text(o_right, LV_SYMBOL_RIGHT);
  o_sub = make_label(open_scr, &lv_font_montserrat_14, T->muted, 146);
  const char *names[2] = {"Reset", "Done"};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *c = lv_obj_create(open_scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 52, 26);
    lv_obj_set_style_radius(c, 13, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_bg_color(c, C(accent), 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, i ? 175 : 13, 114);
    lv_obj_t *l = lv_label_create(c);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_label_set_text(l, names[i]);
    lv_obj_center(l);
    o_chip[i] = c;
    o_chip_lbl[i] = l;
  }
  build_pills(open_scr, &o_pills);
}

static void refresh_open(void) {
  char buf[40];
  if (open_sel < TUNER_STRINGS) {
    int cur = S->open_midi[open_sel], base = tuner_standard_midi(open_sel), diff = cur - base;
    snprintf(buf, sizeof buf, "STRING %d", TUNER_STRINGS - open_sel);
    lv_label_set_text(o_label, buf);
    snprintf(buf, sizeof buf, "%s%d", tuner_note_letter(cur), tuner_note_octave(cur));
    lv_label_set_text(o_big, buf);
    if (open_editing) {
      if (diff) snprintf(buf, sizeof buf, "%+d from %s", diff, tuner_note_letter(base));
      else snprintf(buf, sizeof buf, "same as Standard");
    } else {
      if (diff) snprintf(buf, sizeof buf, "was %s", tuner_note_letter(base));
      else snprintf(buf, sizeof buf, "press to change");
    }
    lv_label_set_text(o_sub, buf);
  } else {
    lv_label_set_text(o_label, "");
    lv_label_set_text(o_big, open_sel == OPEN_RESET ? "Reset" : "Done");
    lv_label_set_text(o_sub, open_sel == OPEN_RESET ? "back to Standard" : "save and tune");
  }
  lv_obj_set_style_text_font(o_big, open_sel < TUNER_STRINGS ? &lv_font_montserrat_48
                                                              : &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_opa(o_left, open_editing ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_opa(o_right, open_editing ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  for (int i = 0; i < 2; i++) {
    bool on = open_sel == TUNER_STRINGS + i;
    lv_obj_set_style_bg_opa(o_chip[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o_chip[i], on ? C(accent) : C(ring), 0);
    lv_obj_set_style_text_color(o_chip_lbl[i], on ? C(on_accent) : C(muted), 0);
  }
  update_pills(&o_pills, S->open_midi, open_sel < TUNER_STRINGS ? open_sel : -1, open_editing, true);
}

/* ---- Settings menu ---- */
static lv_obj_t *menu_scr, *m_roller, *m_value;

static void build_menu(void) {
  menu_scr = make_screen();
  make_ring(menu_scr);
  lv_obj_t *title = make_label(menu_scr, &lv_font_montserrat_14, T->muted, 28);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
  static char opts[256];
  size_t o = 0;
  for (int i = 0; i < SETTING_COUNT; i++)
    o += snprintf(opts + o, sizeof opts - o, "%s\n", SETTINGS[i].title);
  snprintf(opts + o, sizeof opts - o, LV_SYMBOL_LEFT " Back");
  m_roller = lv_roller_create(menu_scr);
  lv_roller_set_options(m_roller, opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(m_roller, 3);
  lv_obj_set_width(m_roller, 200);
  lv_obj_align(m_roller, LV_ALIGN_CENTER, 0, -4);
  lv_obj_set_clickable(m_roller, false);
  lv_obj_set_style_bg_opa(m_roller, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m_roller, 0, 0);
  lv_obj_set_style_text_font(m_roller, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(m_roller, C(muted), 0);
  lv_obj_set_style_text_line_space(m_roller, 14, 0);
  lv_obj_set_style_text_align(m_roller, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_opa(m_roller, LV_OPA_TRANSP, LV_PART_SELECTED);
  lv_obj_set_style_text_color(m_roller, C(accent), LV_PART_SELECTED);
  lv_obj_set_style_text_font(m_roller, &lv_font_montserrat_20, LV_PART_SELECTED);
  m_value = make_label(menu_scr, &lv_font_montserrat_14, T->fg, 190);
}

static void refresh_menu(void) {
  lv_roller_set_selected(m_roller, menu_sel, LV_ANIM_ON);
  if (menu_sel < SETTING_COUNT) {
    char val[40], sub[48];
    const setting_def_t *d = &SETTINGS[menu_sel];
    d->format(*setting_ptr(d), val, sizeof val, sub, sizeof sub);
    if (d->format == fmt_theme || d->format == fmt_onoff) lv_label_set_text(m_value, val);
    else lv_label_set_text_fmt(m_value, "%s %s", val, sub);
  } else {
    lv_label_set_text(m_value, "to tuner");
  }
}

/* ---- Setting editor ---- */
static lv_obj_t *edit_scr, *e_arc, *e_title, *e_value, *e_sub;

static void build_edit(void) {
  edit_scr = make_screen();
  e_arc = lv_arc_create(edit_scr);
  lv_obj_set_size(e_arc, 228, 228);
  lv_obj_center(e_arc);
  lv_arc_set_bg_angles(e_arc, 135, 45);
  lv_arc_set_range(e_arc, 0, 1000);
  lv_obj_remove_style(e_arc, NULL, LV_PART_KNOB);
  lv_obj_set_clickable(e_arc, false);
  lv_obj_set_style_arc_width(e_arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(e_arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(e_arc, C(ring), LV_PART_MAIN);
  lv_obj_set_style_arc_color(e_arc, C(accent), LV_PART_INDICATOR);
  e_title = make_label(edit_scr, &lv_font_montserrat_14, T->muted, 50);
  e_value = make_label(edit_scr, &lv_font_montserrat_28, T->fg, 92);
  e_sub = make_label(edit_scr, &lv_font_montserrat_14, T->muted, 132);
  lv_obj_t *hint = make_label(edit_scr, &lv_font_montserrat_14, T->muted, 196);
  lv_label_set_text(hint, "press to save");
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
}

static void refresh_edit(void) {
  const setting_def_t *d = &SETTINGS[edit_idx];
  int v = *setting_ptr(d);
  char val[40], sub[48];
  d->format(v, val, sizeof val, sub, sizeof sub);
  lv_label_set_text(e_title, d->title);
  lv_label_set_text(e_value, val);
  lv_label_set_text(e_sub, sub);
  int range = d->max - d->min;
  lv_arc_set_value(e_arc, range ? (int)((long)(v - d->min) * 1000 / range) : 1000);
}

/* ========================================================================== */
/*  Navigation                                                                */
/* ========================================================================== */

static lv_obj_t *screen_obj(screen_id_t id) {
  switch (id) {
    case SCR_LIST: return list_scr;
    case SCR_OPEN: return open_scr;
    case SCR_MENU: return menu_scr;
    case SCR_EDIT: return edit_scr;
    default: return tune_scr;
  }
}

static void refresh_current(void) {
  switch (current) {
    case SCR_TUNE: refresh_tune(); break;
    case SCR_LIST: refresh_list(); break;
    case SCR_OPEN: refresh_open(); break;
    case SCR_MENU: refresh_menu(); break;
    case SCR_EDIT: refresh_edit(); break;
  }
}

static void show(screen_id_t id) {
  current = id;
  refresh_current();
  lv_screen_load_anim(screen_obj(id), LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, false);
}

static void build_all(void) {
  /* Free the old screens BEFORE building new ones, so a theme change never needs
     two full sets of screens in LVGL's memory pool at once. */
  lv_obj_t *blank = lv_obj_create(NULL);
  lv_screen_load(blank);
  lv_obj_t **all[5] = {&tune_scr, &list_scr, &open_scr, &menu_scr, &edit_scr};
  for (int i = 0; i < 5; i++) if (*all[i]) { lv_obj_delete(*all[i]); *all[i] = NULL; }
  T = &THEMES[S->theme];
  build_tune(); build_list(); build_open(); build_menu(); build_edit();
  refresh_current();
  lv_screen_load(screen_obj(current));
  lv_obj_delete(blank);
}

static void changed(void) { platform_settings_changed(S); }

void ui_init(tuner_settings_t *settings) {
  S = settings;
  current = SCR_TUNE;
  menu_sel = 0;
  SETTINGS[4].max = THEME_COUNT - 1;    /* Theme */
  if (S->theme >= THEME_COUNT) S->theme = 0;
  build_all();
}

void ui_refresh(void) {
  if (S->theme >= THEME_COUNT) S->theme = 0;
  if (T != &THEMES[S->theme]) build_all();
  else refresh_current();
}

static void use_tuning(int t) {
  S->tuning = t;
  S->string_idx = 0;
  S->strobe_on = true;
  changed();
  show(SCR_TUNE);
}

void ui_key(ui_key_t key) {
  int dir = key == UI_KEY_CW ? 1 : key == UI_KEY_CCW ? -1 : 0;
  bool press = key == UI_KEY_PRESS, hold = key == UI_KEY_LONG_PRESS;

  switch (current) {
    case SCR_TUNE:
      if (dir) { tuner_step(S, dir); changed(); refresh_tune(); }
      else if (press) { list_sel = S->tuning; show(SCR_LIST); }
      else { S->strobe_on = !S->strobe_on; changed(); refresh_tune(); }
      break;

    case SCR_LIST:
      if (dir) {
        int n = list_sel + dir;
        if (n >= 0 && n < LIST_COUNT) { list_sel = n; refresh_list(); }
      } else if (press) {
        if (list_sel == TUNING_COUNT) { menu_sel = 0; show(SCR_MENU); }
        else use_tuning(list_sel);
      } else if (hold && list_sel == TUNING_OPEN) {
        S->tuning = TUNING_OPEN;          /* tune along while setting strings */
        S->string_idx = 0;
        open_sel = 0; open_editing = false;
        changed();
        show(SCR_OPEN);
      } else show(SCR_TUNE);
      break;

    case SCR_OPEN:
      if (open_editing) {
        if (dir) { tuner_open_adjust(S, open_sel, dir); changed(); }
        else open_editing = false;
      } else if (dir) {
        open_sel = (open_sel + dir + OPEN_DONE + 1) % (OPEN_DONE + 1);
        if (open_sel < TUNER_STRINGS) { S->string_idx = open_sel; changed(); }
      } else if (press && open_sel < TUNER_STRINGS) {
        open_editing = true;
      } else if (press && open_sel == OPEN_RESET) {
        tuner_open_reset(S); changed();
      } else {
        S->string_idx = 0; changed();
        show(SCR_TUNE);
        break;
      }
      refresh_open();
      break;

    case SCR_MENU:
      if (dir) { menu_sel = (menu_sel + dir + MENU_BACK + 1) % (MENU_BACK + 1); refresh_menu(); }
      else if (press && menu_sel < SETTING_COUNT) { edit_idx = menu_sel; show(SCR_EDIT); }
      else show(SCR_TUNE);
      break;

    case SCR_EDIT: {
      const setting_def_t *d = &SETTINGS[edit_idx];
      if (!dir) { show(SCR_MENU); break; }
      int *p = setting_ptr(d);
      int v = *p + dir * accel_step(d);
      if (d->wrap) { int n = d->max - d->min + 1; v = d->min + ((v - d->min) % n + n) % n; }
      else v = v < d->min ? d->min : v > d->max ? d->max : v;
      if (v == *p) break;
      *p = v;
      changed();
      if (d->offset == offsetof(tuner_settings_t, theme)) build_all();
      else refresh_edit();
      break;
    }
  }
}
