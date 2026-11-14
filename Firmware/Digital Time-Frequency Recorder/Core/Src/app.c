/*
 * app.c - logika urządzenia: nastawy, wyzwalanie i tryby, podstawa czasu, oś napięcia,
 *         menu, pomiary, sterowanie z płytki i z PC.
 *
 * Wszystko zmieniają komendy tekstowe - z przycisków/enkoderów (ui.c) albo z PC (link.c,
 * tools/remote.py). Pętla: blok 4 kanałów -> wyzwolenie -> decyzja trybu -> kolumny min/max -> ramka DMA.
 *
 * Enkodery (etykiety na płytce):
 *   T (Trigger): obrót = poziom wyzwalania; wciśnięcie = menu (w menu: obrót = wybór, wciśnięcie = zmiana)
 *   X (oś czasu): obrót = T/dz albo położenie poziome; wciśnięcie przełącza
 *   Y (oś napięcia, kanał aktywny): obrót = V/dz albo offset; wciśnięcie przełącza
 *
 * Oś napięcia: V/dz w krokach 1-1,2-1,5-2-2,5-3-4-5-6-8 (50 mV ... 50 V/dz, sonda x10: x10).
 * Wzmocnienie toru (G1/G2/G5/G10) dobierane automatycznie: największe, którego zakres liniowy
 * mieści całe widoczne okno (offset +- pół pasa) -> zawsze najlepsza rozdzielczość.
 * Oś czasu: T/dz w tych samych krokach (1 us ... 50 ms/dz); fs = najszybsza konfiguracja ADC,
 * przy której blok mieści 2 ekrany. Próbek na piksel nie musi być całkowita liczba.
 *
 * Ekran: włączone kanały dzielą pole 300x200 px na równe pasy. Skala IN = wolty na wejściu BNC,
 * ADC = wolty na pinie ADC (testy na samej Nucleo, sygnał z DAC prosto na pin).
 */
#include "app.h"
#include "acq.h"
#include "afe.h"
#include "calib.h"
#include "gfx.h"
#include "lcd.h"
#include "link.h"
#include "ui.h"
#include "main.h"
#include "tim.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_TOP_H  16
#define BAR_BOT_Y  (PLOT_Y + PLOT_H)      /* 216 */
#define COL_BAR    RGB565(28, 28, 28)
#define AUTO_MS    100                    /* AUTO: obraz bez wyzwolenia po tym czasie */
#define MENU_MS    20000                  /* menu zamyka się samo po 20 s bez ruchu */
#define CODE_MID   2048                   /* VMID = VREF/2 -> 0 V na wejściu */
#define LSB_V      ((float)VREF_MV / 1000.0f / 4096.0f)
#define SPX_MAX    6.5f                   /* maks. próbek na piksel (kolumny min/max) */

/* kroki 1-1,2-1,5-2-2,5-3-4-5-6-8 */
static const float mant[10] = { 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
/* V/dz: indeks k -> mant[k%10] * 10^(k/10 - 2) [V];  7 = 50 mV, 37 = 50 V */
#define VDIV_MIN  7
#define VDIV_MAX  37
#define VDIV_IN   30                      /* domyślnie 10 V/dz (wejście BNC) */
#define VDIV_ADC  17                      /* domyślnie 0,5 V/dz (pin ADC) */
/* T/dz: indeks j -> mant[j%10] * 10^(j/10 - 6) [s];  0 = 1 us, 47 = 50 ms */
#define TB_MAX    47
#define TB_DEF    13                      /* 20 us/dz */

/* zakres liniowy na wejściu dla G1/G2/G5/G10 (wartosci_AFE_4kanaly.md) */
static const float lin_fs[AFE_NG] = { 32.2f, 16.1f, 6.49f, 3.29f };

static float decade(int e) { float p = 1.0f; while (e > 0) { p *= 10.0f; e--; } while (e < 0) { p /= 10.0f; e++; } return p; }
static float vdiv_val(int k) { return mant[k % 10] * decade(k / 10 - 2); }
static float tdiv_val(int j) { return mant[j % 10] * decade(j / 10 - 6); }

enum { MODE_AUTO = 0, MODE_NORM, MODE_SINGLE };
static const char *mode_name[] = { "AUTO", "NORM", "SINGLE" };

typedef struct {
    uint8_t on, probe10, vdiv, edge, lauto;
    int8_t  gforce;                       /* -1 = wzmocnienie automatyczne, 0..3 = wymuszone G1..G10 */
    float   off;                          /* napięcie na środku pasa [V, wejście BNC / pin ADC] */
    float   lvl;                          /* poziom wyzwalania [V] */
} chs_t;

typedef struct {
    uint8_t run, mode, trig_ch, active, tb, ovs, scale_in;
    uint8_t xmode, ymode;                 /* 0 = skala (T/dz, V/dz), 1 = położenie (poziome, offset) */
    int16_t hpos;                         /* chwila wyzwolenia względem środka ekranu [px] */
    int8_t  meas;                         /* kanał pomiarów, -1 = wył. */
    uint8_t cal1k;                        /* 1 = 1 kHz na J13 */
    chs_t   ch[NCH];
} settings_t;

static settings_t S;

static const uint16_t ch_color[NCH] = { C_YELLOW, C_CYAN, C_MAGENTA, C_LIME };

static uint8_t  tb_dirty = 1, view_dirty, status_dirty = 1, frame_pending, have_block, afe_settle;
static const char *state = "AUTO";       /* TRIG / AUTO / WAIT / STOP */
static uint32_t t_sec, t_last_frame, frames_last, fps, t_menu;
static uint32_t half_min = 0xFFFFFFFFu;
static uint16_t vmin[NCH], vmax[NCH], lvl_code;
static uint32_t f_mHz;
static float    spx = 1.0f;               /* próbek na piksel przy bieżącej T/dz */
static uint8_t  menu_open, menu_sel;

/* Bateria 2S Li-ion: V_BAT -> R36 100k / R37 33k -> PC0;  V_BAT = V_PC0 * 133 / 33 */
#define VBAT_DIV   (133.0f / 33.0f)
#define VBAT_LOW   6.6f                   /* ostrzeżenie: poniżej ~6,2 V LDO traci 5 V (dropout) */
#define VBAT_NONE  3.0f                   /* mniej = brak baterii / zasilanie tylko z USB */
static float    vbat = -1.0f;             /* [V], -1 = jeszcze nie zmierzone */
static uint8_t  blink;

/* kalibracja zera: po kolei G1, G2, G5, G10; na każdym wzmocnieniu średnia z bloku po ustaleniu */
#define CAL_SETTLE_MS 60
static int8_t   cal_g = -1;               /* -1 = nie trwa */
static uint32_t cal_t;
static uint8_t  cal_run_prev;

/* pasy kanałów */
static int16_t band_y0[NCH], band_h = PLOT_H;

static void defaults(void)
{
    memset(&S, 0, sizeof S);
    S.run = 1; S.mode = MODE_AUTO; S.tb = TB_DEF; S.ovs = 1; S.scale_in = 1; S.meas = -1; S.cal1k = 1;
    for (int c = 0; c < NCH; c++) {
        S.ch[c].on = (c < 2);             /* na płytce są CH1 i CH2; CH3/CH4 domyślnie wyłączone */
        S.ch[c].vdiv = VDIV_IN;
        S.ch[c].lauto = 1;
        S.ch[c].gforce = -1;
    }
}

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ---------------------------------------------------------------- jednostki */

static float probe(int c) { return (S.scale_in && S.ch[c].probe10) ? 10.0f : 1.0f; }

/* wolty (wejście BNC lub pin ADC, bez sondy) na jeden kod ADC kanału c */
static float v_per_code(int c) { return S.scale_in ? LSB_V / afe_gain_in2adc[afe_gain(c)] : LSB_V; }
/* kod ADC odpowiadający 0 V na wejściu: VMID + kalibracja zera (kanał, bieżące wzmocnienie) */
static float zero_code(int c) { return S.scale_in ? (float)(CODE_MID + cal_zero[c][afe_gain(c)]) : 0.0f; }
static float code_to_v(int c, float code) { return (code - zero_code(c)) * v_per_code(c); }
static int32_t v_to_code(int c, float v)
{
    int32_t k = (int32_t)(v / v_per_code(c) + zero_code(c) + 0.5f);
    return k < 0 ? 0 : k > 4095 ? 4095 : k;
}
/* wolty na piksel (bez sondy) */
static float vpx(int c) { return vdiv_val(S.ch[c].vdiv) / (float)DIV_Y; }

/* "12.3V", "-1.25V", "850mV" - bez printf dla float */
static void fmt_v(char *s, int n, float v)
{
    const char *sg = (v < 0.0f) ? "-" : "";
    if (v < 0.0f) v = -v;
    uint32_t mv = (uint32_t)(v * 1000.0f + 0.5f);
    if (mv < 1000)        snprintf(s, (size_t)n, "%s%lumV", sg, (unsigned long)mv);
    else if (mv < 10000)  snprintf(s, (size_t)n, "%s%lu.%02luV", sg, (unsigned long)(mv / 1000), (unsigned long)(mv / 10 % 100));
    else if (mv < 100000) snprintf(s, (size_t)n, "%s%lu.%luV", sg, (unsigned long)(mv / 1000), (unsigned long)(mv / 100 % 10));
    else                  snprintf(s, (size_t)n, "%s%luV", sg, (unsigned long)(mv / 1000));
}
/* V/dz: "50mV", "1.2V", "20V" */
static void fmt_vdiv(char *s, int n, float v)
{
    uint32_t mv = (uint32_t)(v * 1000.0f + 0.5f);
    if (mv < 1000)              snprintf(s, (size_t)n, "%lumV", (unsigned long)mv);
    else if (mv % 1000 == 0)    snprintf(s, (size_t)n, "%luV", (unsigned long)(mv / 1000));
    else if (mv < 10000)        snprintf(s, (size_t)n, "%lu.%luV", (unsigned long)(mv / 1000), (unsigned long)(mv / 100 % 10));
    else                        snprintf(s, (size_t)n, "%luV", (unsigned long)(mv / 1000));
}
/* T/dz: "1us", "2.5us", "120us", "1.5ms", "50ms" */
static void fmt_t(char *s, int n, float t)
{
    uint32_t ns = (uint32_t)(t * 1e9f + 0.5f);
    uint32_t v = ns, d;
    const char *u = "us";
    if (ns >= 1000000u) { v = ns / 1000u; u = "ms"; }       /* v w us -> ms jako /1000 niżej */
    d = v / 100u;                                           /* dziesiąte części jednostki */
    if (d % 10u) snprintf(s, (size_t)n, "%lu.%lu%s", (unsigned long)(d / 10u), (unsigned long)(d % 10u), u);
    else         snprintf(s, (size_t)n, "%lu%s", (unsigned long)(d / 10u), u);
}
static void fmt_f(char *s, int n, uint32_t mhz)
{
    if (mhz >= 1000000u) snprintf(s, (size_t)n, "%lu.%02lukHz", (unsigned long)(mhz / 1000000u), (unsigned long)(mhz / 10000u % 100u));
    else                 snprintf(s, (size_t)n, "%lu.%luHz", (unsigned long)(mhz / 1000u), (unsigned long)(mhz / 100u % 10u));
}

/* ---------------------------------------------------------------- pasy, wzmocnienie */

static void layout(void)
{
    int n = 0;
    for (int c = 0; c < NCH; c++) n += S.ch[c].on;
    band_h = (int16_t)(n ? PLOT_H / n : PLOT_H);
    int k = 0;
    for (int c = 0; c < NCH; c++) {
        band_y0[c] = (int16_t)(k * band_h);
        if (S.ch[c].on) k++;
    }
}

/* Największe wzmocnienie, którego zakres liniowy mieści widoczne okno [off - pół pasa, off + pół pasa] */
static void auto_gain(void)
{
    if (!S.scale_in || cal_g >= 0) return;                /* test na Nucleo / trwa kalibracja */
    for (int c = 0; c < NCH; c++) {
        float half = (float)band_h / 2.0f * vpx(c);
        float need = fabsf(S.ch[c].off) + half;
        int g = 0;
        for (int k = AFE_NG - 1; k >= 0; k--)
            if (lin_fs[k] >= need) { g = k; break; }
        if (S.ch[c].gforce >= 0) g = S.ch[c].gforce;
        if (g != afe_gain(c)) { afe_set_gain(c, g); afe_settle = 1; }
    }
}

/* y piksela dla napięcia v kanału c */
static inline float v_to_y(int c, float v)
{
    return (float)band_y0[c] + (float)band_h / 2.0f - (v - S.ch[c].off) / vpx(c);
}

/* ---------------------------------------------------------------- podstawa czasu */

static void apply_timebase(void)
{
    float t = tdiv_val(S.tb);
    int cfg = 0;
    while (cfg < ACQ_NCFG - 1 && (float)acq_cfg_fs(cfg) * t / (float)DIV_X > SPX_MAX) cfg++;
    float s = (float)acq_cfg_fs(cfg) * t / (float)DIV_X;
    int n = (cfg == 0) ? ACQ_NMAX : (int)(600.0f * s) + 16;          /* 2 ekrany + zapas */
    if (n < 1200) n = 1200;
    acq_configure(cfg, S.ovs, n);
    spx = (float)acq_fs_hz() * t / (float)DIV_X;
}

/* ---------------------------------------------------------------- menu */

enum { MI_PROBE = 0, MI_COUP = 4, MI_MEAS = 8, MI_TSRC, MI_EDGE, MI_OVS, MI_CAL1K, MI_SCALE, MI_CALZ, MI_EXIT, MI_N };

static void menu_build(void)
{
    overlay_t *o = &gfx_ov[0];
    o->on = menu_open;
    if (!menu_open) return;
    o->w = 19 * 6 + 6;
    o->x = PLOT_W - o->w - 1;
    o->y = 1;
    o->n = MI_N;
    o->hl = (int8_t)menu_sel;
    for (int i = 0; i < MI_N; i++) {
        char *t = o->txt[i];
        o->fg[i] = C_WHITE;
        if (i < MI_COUP) {
            snprintf(t, OV_CHARS + 1, "Sonda CH%d:     x%s", i + 1, S.ch[i].probe10 ? "10" : "1");
            o->fg[i] = ch_color[i];
        } else if (i < MI_MEAS) {
            int c = i - MI_COUP;
            snprintf(t, OV_CHARS + 1, "Sprzez. CH%d:   %s", c + 1, afe_dc(c) ? "DC" : "AC");
            o->fg[i] = ch_color[c];
        } else if (i == MI_MEAS) {
            if (S.meas < 0) snprintf(t, OV_CHARS + 1, "Pomiary:       wyl");
            else            snprintf(t, OV_CHARS + 1, "Pomiary:       CH%d", S.meas + 1);
        } else if (i == MI_TSRC) snprintf(t, OV_CHARS + 1, "Trig zrodlo:   CH%d", S.trig_ch + 1);
        else if (i == MI_EDGE)   snprintf(t, OV_CHARS + 1, "Trig zbocze:   %s", S.ch[S.trig_ch].edge ? "v" : "^");
        else if (i == MI_OVS)    snprintf(t, OV_CHARS + 1, "Nadprobkow.:   %s", S.ovs ? "tak" : "nie");
        else if (i == MI_CAL1K)  snprintf(t, OV_CHARS + 1, "1 kHz na J13:  %s", S.cal1k ? "tak" : "nie");
        else if (i == MI_SCALE)  snprintf(t, OV_CHARS + 1, "Skala:         %s", S.scale_in ? "BNC" : "ADC");
        else if (i == MI_CALZ)   snprintf(t, OV_CHARS + 1, "Kalibruj zero");
        else                     snprintf(t, OV_CHARS + 1, "Wyjscie");
    }
}

static void set_scale(int in);
static void cal_start(void);
static void set_cal1k(int on);

static void menu_activate(void)
{
    int i = menu_sel;
    if (i < MI_COUP) { S.ch[i].probe10 = !S.ch[i].probe10; }
    else if (i < MI_MEAS) { int c = i - MI_COUP; afe_set_dc(c, !afe_dc(c)); afe_settle = 1; }
    else if (i == MI_MEAS) { S.meas = (int8_t)(S.meas + 1); if (S.meas >= NCH) S.meas = -1; }
    else if (i == MI_TSRC) { S.trig_ch = (uint8_t)((S.trig_ch + 1) % NCH); }
    else if (i == MI_EDGE) { S.ch[S.trig_ch].edge = !S.ch[S.trig_ch].edge; }
    else if (i == MI_OVS)  { S.ovs = !S.ovs; tb_dirty = 1; }
    else if (i == MI_CAL1K) set_cal1k(!S.cal1k);
    else if (i == MI_SCALE) set_scale(!S.scale_in);
    else if (i == MI_CALZ) { menu_open = 0; cal_start(); }
    else menu_open = 0;
    view_dirty = 1;
}

/* ---------------------------------------------------------------- pomiary */

typedef struct { float vpp, vmax, vmin, vavg, vrms; uint32_t f_mhz; } meas_t;
static meas_t M;

static uint32_t measure_f_mHz(const uint16_t *d, int n, uint16_t level, uint16_t hyst, int edge)
{
    int32_t first = acq_find_trigger(d, 1, n, level, hyst, edge), last = first, k = 0;
    while (last >= 0) {
        int32_t nx = acq_find_trigger(d, last + 1, n, level, hyst, edge);
        if (nx < 0) break;
        last = nx;
        k++;
    }
    return (k > 0) ? (uint32_t)((uint64_t)acq_fs_hz() * 1000u * (uint32_t)k / (uint32_t)(last - first)) : 0;
}

static void measure(int c)
{
    const uint16_t *d = acq_ch(c);
    const int n = acq_n();
    const int32_t c0 = (int32_t)zero_code(c);
    int64_t sum = 0, sq = 0;
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)d[i] - c0;
        sum += v;
        sq += (int64_t)v * v;
    }
    float k = v_per_code(c) * probe(c);
    M.vmax = code_to_v(c, vmax[c]) * probe(c);
    M.vmin = code_to_v(c, vmin[c]) * probe(c);
    M.vpp = M.vmax - M.vmin;
    M.vavg = (float)sum / (float)n * k;
    M.vrms = sqrtf((float)sq / (float)n) * k;
    uint16_t mid = (uint16_t)((vmin[c] + vmax[c]) / 2), hy = (uint16_t)((vmax[c] - vmin[c]) / 20 + 4);
    M.f_mhz = measure_f_mHz(d, n, mid, hy, 0);
}

static void meas_build(void)
{
    overlay_t *o = &gfx_ov[1];
    o->on = (S.meas >= 0);
    if (!o->on) return;
    int c = S.meas;
    char v[12];
    o->x = 1; o->y = 1; o->w = 14 * 6 + 6; o->n = 7; o->hl = -1;
    for (int i = 0; i < 7; i++) o->fg[i] = ch_color[c];
    snprintf(o->txt[0], OV_CHARS + 1, "CH%d pomiary", c + 1);
    fmt_v(v, sizeof v, M.vpp);  snprintf(o->txt[1], OV_CHARS + 1, "Vpp  %s", v);
    fmt_v(v, sizeof v, M.vmax); snprintf(o->txt[2], OV_CHARS + 1, "Vmax %s", v);
    fmt_v(v, sizeof v, M.vmin); snprintf(o->txt[3], OV_CHARS + 1, "Vmin %s", v);
    fmt_v(v, sizeof v, M.vavg); snprintf(o->txt[4], OV_CHARS + 1, "Vsr  %s", v);
    fmt_v(v, sizeof v, M.vrms); snprintf(o->txt[5], OV_CHARS + 1, "Vrms %s", v);
    if (M.f_mhz) { fmt_f(v, sizeof v, M.f_mhz); snprintf(o->txt[6], OV_CHARS + 1, "f %s", v); }
    else snprintf(o->txt[6], OV_CHARS + 1, "f   ---");
}

/* ---------------------------------------------------------------- obraz */

static void draw_static(void)
{
    lcd_fill(C_BLACK);
    lcd_fill_rect(0, 0, LCD_W, BAR_TOP_H, COL_BAR);
    lcd_fill_rect(0, BAR_BOT_Y, LCD_W, LCD_H - BAR_BOT_Y, COL_BAR);
}

static int16_t clamp_band(int c, float y)
{
    int lo = band_y0[c], hi = band_y0[c] + band_h - 1;
    return (int16_t)(y < lo ? lo : y > hi ? hi : y);
}

/* Stopień naładowania z napięcia na ogniwo (krzywa OCV Li-ion, w przybliżeniu, pod małym obciążeniem) */
static int bat_percent(float v_pack)
{
    static const float v[]  = { 3.30f, 3.50f, 3.60f, 3.70f, 3.75f, 3.80f, 3.85f, 3.90f, 4.00f, 4.10f, 4.20f };
    static const float pc[] = { 0.0f,  5.0f, 10.0f, 30.0f, 45.0f, 55.0f, 65.0f, 72.0f, 85.0f, 95.0f, 100.0f };
    float c = v_pack / 2.0f;                              /* 2S: dwa ogniwa szeregowo */
    if (c <= v[0]) return 0;
    for (int i = 1; i < 11; i++)
        if (c <= v[i]) return (int)(pc[i - 1] + (c - v[i - 1]) * (pc[i] - pc[i - 1]) / (v[i] - v[i - 1]) + 0.5f);
    return 100;
}

/* Ikonka baterii w prawym górnym rogu paska statusu (x 294..319) */
static void draw_battery(void)
{
    const uint16_t x = 294, y = 3, w = 22, h = 10;
    lcd_fill_rect(x - 2, 0, LCD_W - x + 2, BAR_TOP_H, COL_BAR);
    if (vbat < 0.0f) return;
    if (vbat < VBAT_NONE) {                               /* brak baterii - zasilanie z USB */
        lcd_text(x, y + 1, "USB", C_GRAY, COL_BAR, 1);
        return;
    }
    int p = bat_percent(vbat);
    uint16_t col = (vbat < VBAT_LOW) ? C_RED : (p < 20) ? C_RED : (p < 40) ? C_YELLOW : C_GREEN;
    uint16_t frame = (vbat < VBAT_LOW && (blink & 1)) ? C_RED : C_WHITE;
    lcd_fill_rect(x, y, w, h, frame);                     /* obrys */
    lcd_fill_rect(x + 1, y + 1, w - 2, h - 2, COL_BAR);
    lcd_fill_rect(x + w, y + 3, 2, h - 6, frame);          /* biegun + */
    int fw = (w - 4) * p / 100;
    if (fw < 1 && p > 0) fw = 1;
    if (fw > 0) lcd_fill_rect(x + 2, y + 2, (uint16_t)fw, h - 4, col);
}

static void draw_status(void)
{
    const int tc = S.trig_ch;
    char s[96], lv[12], tb[10], fr[14], pad[50];
    fmt_v(lv, sizeof lv, S.ch[tc].lvl * probe(tc));
    fmt_t(tb, sizeof tb, tdiv_val(S.tb));
    if (f_mHz) fmt_f(fr, sizeof fr, f_mHz); else snprintf(fr, sizeof fr, "---");
    snprintf(s, sizeof s, "%-4s %-4s %s/dz%s CH%d%c%s%s %-4s f=%s",
             S.run ? "RUN" : "STOP", S.mode == MODE_SINGLE ? "SNGL" : mode_name[S.mode],
             tb, S.xmode ? "<>" : "", tc + 1, S.ch[tc].edge ? 'v' : '^', lv,
             S.ch[tc].lauto ? "A" : "", state, fr);
    snprintf(pad, sizeof pad, "%-48.48s", s);             /* stała szerokość (ikonka baterii z prawej) */
    lcd_text(2, 4, pad, S.run ? C_WHITE : C_ORANGE, COL_BAR, 1);
    draw_battery();

    /* prawa kolumna: znacznik 0 V każdego kanału i poziomu wyzwolenia */
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    for (int c = 0; c < NCH; c++) {
        if (!S.ch[c].on) continue;
        int y = clamp_band(c, v_to_y(c, 0.0f)) - 5;
        if (y < band_y0[c]) y = band_y0[c];
        if (y > band_y0[c] + band_h - 11) y = band_y0[c] + band_h - 11;
        lcd_fill_rect(PLOT_W + 3, (uint16_t)(PLOT_Y + y), 15, 11, ch_color[c]);
        char t[2] = { (char)('1' + c), 0 };
        lcd_text(PLOT_W + 8, (uint16_t)(PLOT_Y + y + 2), t, C_BLACK, ch_color[c], 1);
    }
    if (S.ch[tc].on) {
        int ty = clamp_band(tc, v_to_y(tc, S.ch[tc].lvl)) - 5;
        if (ty < 0) ty = 0;
        if (ty > PLOT_H - 11) ty = PLOT_H - 11;
        lcd_fill_rect(PLOT_W + 3, (uint16_t)(PLOT_Y + ty), 15, 11, C_ORANGE);
        lcd_text(PLOT_W + 8, (uint16_t)(PLOT_Y + ty + 2), "T", C_BLACK, C_ORANGE, 1);
    }

    /* dolny pasek: "1*DC 500mV/d" / "G10 o-12.3V"; '*' = kanał aktywny, '+' = enkoder Y zmienia offset */
    for (int c = 0; c < NCH; c++) {
        char l1[24], l2[24], v[12], p1[14], p2[14];
        uint16_t col = S.ch[c].on ? ch_color[c] : C_DGRAY, fg = S.ch[c].on ? C_BLACK : C_GRAY;
        char mark = (c == S.active) ? (S.ymode ? '+' : '*') : ' ';
        fmt_vdiv(v, sizeof v, vdiv_val(S.ch[c].vdiv) * probe(c));
        snprintf(l1, sizeof l1, "%d%c%s %s/d", c + 1, mark, afe_dc(c) ? "DC" : "AC", v);
        if (S.ch[c].on) {
            fmt_v(v, sizeof v, S.ch[c].off * probe(c));
            snprintf(l2, sizeof l2, "G%s%s o%s%s", afe_gain_name[afe_gain(c)], S.ch[c].gforce >= 0 ? "!" : "",
                     S.ch[c].off >= 0.0f ? "+" : "", v);
        } else {
            snprintf(l2, sizeof l2, "off");
        }
        snprintf(p1, sizeof p1, "%-13.13s", l1);
        snprintf(p2, sizeof p2, "%-13.13s", l2);
        lcd_fill_rect(1 + c * 80, BAR_BOT_Y + 1, 79, 22, col);
        lcd_text(2 + c * 80, BAR_BOT_Y + 3, p1, fg, col, 1);
        lcd_text(2 + c * 80, BAR_BOT_Y + 13, p2, fg, col, 1);
    }
}

/* Blok -> kolumny min/max kanałów, od pozycji pos0 (ułamek próbki), spx próbek na piksel */
static void build_view(float pos0, int triggered)
{
    const int n = acq_n();
    const int nsub = (spx >= 1.0f) ? (int)(spx + 0.5f) : 1;
    const float step = spx / (float)nsub;

    for (int c = 0; c < NCH; c++) {
        gfx_tr[c].on = S.ch[c].on;
        if (!S.ch[c].on) continue;
        const uint16_t *d = acq_ch(c);
        trace_t *tr = &gfx_tr[c];
        /* y = A * kod + B */
        const float k = v_per_code(c), p = vpx(c);
        const float A = -k / p;
        const float B = (float)band_y0[c] + (float)band_h / 2.0f
                      + (zero_code(c) * k + S.ch[c].off) / p;
        const float ylo = (float)band_y0[c], yhi = (float)(band_y0[c] + band_h - 1);
        float prev = 0.0f;
        for (int x = 0; x < PLOT_W; x++) {
            float mn = 1e9f, mx = -1e9f, v = 0.0f;
            for (int j = 0; j < nsub; j++) {
                float pos = pos0 + (float)x * spx + (float)j * step;
                int32_t i = (int32_t)pos;
                if (i < 0) i = 0;
                if (i > n - 2) i = n - 2;
                float fr = pos - (float)i;
                if (fr > 1.0f) fr = 1.0f;
                v = (float)d[i] + fr * ((float)d[i + 1] - (float)d[i]);   /* interpolacja liniowa */
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            if (x > 0) {                                     /* ciągłość z poprzednią kolumną */
                if (prev < mn) mn = prev;
                if (prev > mx) mx = prev;
            }
            prev = v;
            float ya = A * mx + B, yb = A * mn + B;          /* ya = góra, yb = dół */
            if (yb < ylo || ya > yhi) { tr->ymin[x] = 32767; tr->ymax[x] = -1; continue; }   /* poza pasem */
            tr->ymin[x] = (int16_t)(ya < ylo ? ylo : ya + 0.5f);
            tr->ymax[x] = (int16_t)(yb > yhi ? yhi : yb + 0.5f);
        }
    }
    const int tc = S.trig_ch;
    gfx_trig_y = S.ch[tc].on ? clamp_band(tc, v_to_y(tc, S.ch[tc].lvl)) : -1;
    gfx_trig_x = triggered ? (int16_t)(PLOT_W / 2 + S.hpos) : -1;
    if (S.meas >= 0) measure(S.meas);
    meas_build();
    menu_build();
    frame_pending = 1;
    t_last_frame = HAL_GetTick();
}

/* ---------------------------------------------------------------- obróbka bloku */

/* live = 1: nowy blok w trybie RUN (decyzje trybu);  live = 0: przerysowanie zatrzymanego bloku */
static void process_block(int live)
{
    const int n = acq_n();
    for (int c = 0; c < NCH; c++) {                       /* zakres każdego kanału */
        const uint16_t *d = acq_ch(c);
        uint16_t mn = 4095, mx = 0;
        for (int i = 0; i < n; i++) {
            if (d[i] < mn) mn = d[i];
            if (d[i] > mx) mx = d[i];
        }
        vmin[c] = mn;
        vmax[c] = mx;
    }

    /* poziom i histereza kanału wyzwalającego */
    const int tc = S.trig_ch;
    uint16_t hyst;
    if (S.ch[tc].lauto) {
        lvl_code = (uint16_t)((vmin[tc] + vmax[tc]) / 2);
        hyst = (uint16_t)((vmax[tc] - vmin[tc]) / 20);
        if (hyst < 8) hyst = 8;
        S.ch[tc].lvl = code_to_v(tc, lvl_code);
    } else {
        lvl_code = (uint16_t)v_to_code(tc, S.ch[tc].lvl);
        hyst = 20;
    }

    /* wyzwolenie w punkcie x = 150 + hpos: szukamy tak, żeby cały ekran mieścił się w bloku */
    const uint16_t *d = acq_ch(tc);
    const float left = (float)(PLOT_W / 2 + S.hpos) * spx, right = (float)(PLOT_W / 2 - S.hpos) * spx;
    int32_t from = (int32_t)left + 2, to = n - (int32_t)right - 2;
    int32_t t = (from < to) ? acq_find_trigger(d, from, to, lvl_code, hyst, S.ch[tc].edge) : -1;
    f_mHz = measure_f_mHz(d, n, lvl_code, hyst, S.ch[tc].edge);

    if (t >= 0) {
        float a = d[t - 1], b = d[t];
        float frac = (b != a) ? ((float)lvl_code - a) / (b - a) : 1.0f;   /* ułamek próbki */
        build_view((float)(t - 1) + frac - left, 1);
        if (live) {
            state = "TRIG";
            if (S.mode == MODE_SINGLE) { S.run = 0; state = "STOP"; }
        }
    } else if (!live) {
        build_view(0.0f, 0);
    } else if (S.mode == MODE_AUTO && (HAL_GetTick() - t_last_frame) >= AUTO_MS) {
        build_view(0.0f, 0);                               /* AUTO bez wyzwolenia */
        state = "AUTO";
    } else if (S.mode != MODE_AUTO) {
        state = "WAIT";                                    /* NORM/SINGLE: czekamy, obraz zostaje */
    }
}

/* ---------------------------------------------------------------- komendy */

static void send_status(void)
{
    uint32_t fsm = (half_min != 0xFFFFFFFFu)
                   ? (uint32_t)((uint64_t)(acq_n() / 2) * 156000000u / half_min) : 0;
    const int tc = S.trig_ch;
    char tb[10];
    fmt_t(tb, sizeof tb, tdiv_val(S.tb));
    long vd[NCH], of[NCH], vp[NCH];
    for (int c = 0; c < NCH; c++) {
        vd[c] = (long)(vdiv_val(S.ch[c].vdiv) * probe(c) * 1000.0f + 0.5f);
        of[c] = (long)(S.ch[c].off * probe(c) * 1000.0f);
        vp[c] = (long)((float)(vmax[c] - vmin[c]) * v_per_code(c) * probe(c) * 1000.0f);
    }
    printf("#S run=%d mode=%s st=%s trig=%d edge=%c lauto=%d lvl=%ld tdiv=%s fs=%lu ovs=%lu ovsen=%d n=%d"
           " f=%lu.%03lu on=%d%d%d%d g=%s,%s,%s,%s cpl=%c%c%c%c pr=%d,%d,%d,%d"
           " vdiv=%ld,%ld,%ld,%ld off=%ld,%ld,%ld,%ld vpp=%ld,%ld,%ld,%ld"
           " act=%d xm=%c ym=%c hpos=%d menu=%d meas=%d scale=%s fps=%lu fsm=%lu"
           " cal1k=%d z=%d,%d gf=%d,%d vbat=%ld bat=%d\r\n",
           S.run, mode_name[S.mode], state, tc + 1, S.ch[tc].edge ? 'F' : 'R', S.ch[tc].lauto,
           (long)(S.ch[tc].lvl * probe(tc) * 1000.0f), tb,
           (unsigned long)acq_fs_hz(), (unsigned long)acq_ovs(), S.ovs, acq_n(),
           (unsigned long)(f_mHz / 1000u), (unsigned long)(f_mHz % 1000u),
           S.ch[0].on, S.ch[1].on, S.ch[2].on, S.ch[3].on,
           afe_gain_name[afe_gain(0)], afe_gain_name[afe_gain(1)], afe_gain_name[afe_gain(2)], afe_gain_name[afe_gain(3)],
           afe_dc(0) ? 'D' : 'A', afe_dc(1) ? 'D' : 'A', afe_dc(2) ? 'D' : 'A', afe_dc(3) ? 'D' : 'A',
           S.ch[0].probe10 ? 10 : 1, S.ch[1].probe10 ? 10 : 1, S.ch[2].probe10 ? 10 : 1, S.ch[3].probe10 ? 10 : 1,
           vd[0], vd[1], vd[2], vd[3], of[0], of[1], of[2], of[3], vp[0], vp[1], vp[2], vp[3],
           S.active + 1, S.xmode ? 'P' : 'T', S.ymode ? 'P' : 'V', S.hpos, menu_open, S.meas + 1,
           S.scale_in ? "IN" : "ADC", (unsigned long)fps, (unsigned long)fsm,
           S.cal1k, cal_zero[0][afe_gain(0)], cal_zero[1][afe_gain(1)], S.ch[0].gforce, S.ch[1].gforce,
           (long)(vbat * 1000.0f), vbat > VBAT_NONE ? bat_percent(vbat) : -1);
}

static void help(void)
{
    printf("Komendy (przyciski plytki wysylaja te z gwiazdka):\r\n"
           " *RUNBTN | RUN | STOP | *SINGLE    *MODE T | MODE AUTO|NORM\r\n"
           " *CHBTN <1-4> | CH <1-4> ON|OFF    kanal: wl / aktywny / wyl\r\n"
           " *TROT +|-  *TPUSH                 enkoder T: poziom / menu\r\n"
           " *XROT +|-  *XPUSH                 enkoder X: T/dz albo pozycja / przelacz\r\n"
           " *YROT +|-  *YPUSH                 enkoder Y: V/dz albo offset kanalu aktywnego / przelacz\r\n"
           "  TRIG <1-4> [R|F]  EDGE R|F|T  LEVEL <V>|+|-|AUTO\r\n"
           "  TB +|-|<0-%d>  HPOS <px>|+|-   VDIV <1-4|A> +|-   OFFSET <1-4|A> <V>|+|-\r\n"
           "  PROBE <1-4|A> 1|10  COUP <1-4|A> AC|DC|T  MEAS <1-4>|OFF  MENU\r\n"
           "  SCALE IN|ADC  OVS ON|OFF  STAT  HELP\r\n"
           "  CALZERO (wejscia zwarte) | CALCLEAR   CAL1K ON|OFF\r\n"
           "  GAIN <1-4|A> AUTO|1|2|5|10          wymuszenie wzmocnienia (diagnostyka)\r\n", TB_MAX);
}

/* numer kanału z "1".."4" albo "A" (aktywny); -1 = błąd */
static int parse_ch(const char *a)
{
    if (!a) return -1;
    if (a[0] == 'A' && !a[1]) return S.active;
    return (a[0] >= '1' && a[0] <= '4' && !a[1]) ? a[0] - '1' : -1;
}

static void set_active_next(int from)
{
    for (int k = 1; k <= NCH; k++) {
        int j = (from + k) % NCH;
        if (S.ch[j].on) { S.active = (uint8_t)j; return; }
    }
}

static void set_scale(int in)
{
    S.scale_in = (uint8_t)in;
    for (int c = 0; c < NCH; c++) {                       /* rozsądne nastawy dla nowej skali */
        S.ch[c].vdiv = in ? VDIV_IN : VDIV_ADC;
        S.ch[c].off = in ? 0.0f : (float)VREF_MV / 2000.0f;
        S.ch[c].lauto = 1;
    }
    if (!in) for (int c = 0; c < NCH; c++) afe_set_gain(c, 0);
}

static void set_cal1k(int on)
{
    S.cal1k = (uint8_t)on;
    if (on) HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2);
    else    HAL_TIM_PWM_Stop(&htim5, TIM_CHANNEL_2);
}

/* Kalibracja zera: WEJŚCIA ZWARTE DO MASY. Dla G1, G2, G5, G10: ustawienie wzmocnienia,
 * odczekanie CAL_SETTLE_MS, średnia z całego bloku -> cal_zero[kanał][G]. */
static void cal_start(void)
{
    if (!S.scale_in) { printf("CAL: tylko w skali BNC (z plytka)\r\n"); return; }
    cal_run_prev = S.run;
    S.run = 1;
    cal_g = 0;
    for (int c = 0; c < NCH; c++) afe_set_gain(c, 0);
    afe_settle = 1;
    cal_t = HAL_GetTick() + CAL_SETTLE_MS;
    state = "CAL";
    printf("CAL: start (wejscia zwarte do masy)\r\n");
}

/* wołane z nowym blokiem w trakcie kalibracji */
static void cal_block(void)
{
    if ((int32_t)(HAL_GetTick() - cal_t) < 0) return;    /* jeszcze się ustala */
    const int n = acq_n();
    for (int c = 0; c < NCH; c++) {
        const uint16_t *d = acq_ch(c);
        int64_t s = 0;
        for (int i = 0; i < n; i++) s += d[i];
        int32_t mean = (int32_t)((s + n / 2) / n);
        cal_zero[c][cal_g] = (int16_t)(mean - CODE_MID);
    }
    printf("CAL G%s: %d %d %d %d kodow\r\n", afe_gain_name[cal_g],
           cal_zero[0][cal_g], cal_zero[1][cal_g], cal_zero[2][cal_g], cal_zero[3][cal_g]);
    if (++cal_g < AFE_NG) {
        for (int c = 0; c < NCH; c++) afe_set_gain(c, cal_g);
        afe_settle = 1;
        cal_t = HAL_GetTick() + CAL_SETTLE_MS;
        return;
    }
    cal_g = -1;
    S.run = cal_run_prev;
    state = S.run ? "AUTO" : "STOP";
    printf(calib_save() ? "CAL: BLAD zapisu flash\r\n" : "CAL: zapisane we flash\r\n");
    auto_gain();
    view_dirty = 1;
    status_dirty = 1;
}

static void level_step(int dir)
{
    const int tc = S.trig_ch;
    S.ch[tc].lvl += (float)dir * 2.0f * vpx(tc);          /* 2 piksele na ząbek */
    S.ch[tc].lauto = 0;
}

static void vdiv_step(int c, int dir)
{
    int k = S.ch[c].vdiv + dir;
    S.ch[c].vdiv = (uint8_t)(k < VDIV_MIN ? VDIV_MIN : k > VDIV_MAX ? VDIV_MAX : k);
}

static void offset_step(int c, int dir)
{
    S.ch[c].off -= (float)dir * 2.0f * vpx(c);            /* obrót w prawo = przebieg w górę */
    float lim = S.scale_in ? 40.0f : (float)VREF_MV / 1000.0f;
    if (S.ch[c].off > lim) S.ch[c].off = lim;
    if (S.ch[c].off < -lim) S.ch[c].off = -lim;
}

static void hpos_step(int dir)
{
    int h = S.hpos + dir * 5;
    S.hpos = (int16_t)(h < -140 ? -140 : h > 140 ? 140 : h);
}

/* quiet = 1: komenda z przycisków (bez odpowiedzi na UART - nie blokuje enkoderów) */
static void command(char *ln, int quiet)
{
    for (char *p = ln; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *cmd = strtok(ln, " \t");
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    int ok = 1, ch;
    int dir1 = (a1 && a1[0] == '+') ? 1 : (a1 && a1[0] == '-' && !a1[1]) ? -1 : 0;
    int dir2 = (a2 && a2[0] == '+') ? 1 : (a2 && a2[0] == '-' && !a2[1]) ? -1 : 0;
    if (!cmd) return;

    /* --- panel: enkodery i przyciski --- */
    if (!strcmp(cmd, "TROT") && dir1) {
        if (menu_open) { menu_sel = (uint8_t)((menu_sel + MI_N + dir1) % MI_N); t_menu = HAL_GetTick(); }
        else level_step(dir1);
    }
    else if (!strcmp(cmd, "TPUSH") || !strcmp(cmd, "MENU")) {
        if (!menu_open) { menu_open = 1; menu_sel = 0; }
        else if (!strcmp(cmd, "MENU")) menu_open = 0;
        else menu_activate();
        t_menu = HAL_GetTick();
    }
    else if (!strcmp(cmd, "XROT") && dir1) {
        if (S.xmode) hpos_step(dir1);
        else { int j = S.tb + dir1; if (j >= 0 && j <= TB_MAX) { S.tb = (uint8_t)j; tb_dirty = 1; } }
    }
    else if (!strcmp(cmd, "XPUSH")) S.xmode = !S.xmode;
    else if (!strcmp(cmd, "YROT") && dir1) {
        if (S.ymode) offset_step(S.active, dir1); else vdiv_step(S.active, dir1);
    }
    else if (!strcmp(cmd, "YPUSH")) S.ymode = !S.ymode;
    else if (!strcmp(cmd, "RUN"))    { S.run = 1; if (S.mode == MODE_SINGLE) S.mode = MODE_AUTO; }
    else if (!strcmp(cmd, "STOP"))   { S.run = 0; state = "STOP"; }
    else if (!strcmp(cmd, "RUNBTN")) {
        S.run = !S.run;
        if (!S.run) state = "STOP";
        else if (S.mode == MODE_SINGLE) state = "WAIT";
    }
    else if (!strcmp(cmd, "SINGLE")) { S.mode = MODE_SINGLE; S.run = 1; state = "WAIT"; }
    else if (!strcmp(cmd, "MODE") && a1) {
        if (!strcmp(a1, "AUTO")) S.mode = MODE_AUTO;
        else if (!strncmp(a1, "NORM", 4)) S.mode = MODE_NORM;
        else if (!strcmp(a1, "T")) S.mode = (S.mode == MODE_AUTO) ? MODE_NORM : MODE_AUTO;
        else ok = 0;
    }
    else if (!strcmp(cmd, "CHBTN") && (ch = parse_ch(a1)) >= 0) {
        if (!S.ch[ch].on)          { S.ch[ch].on = 1; S.active = (uint8_t)ch; }   /* wyłączony -> włącz */
        else if (S.active != ch)   { S.active = (uint8_t)ch; }                    /* włączony -> aktywny */
        else                       { S.ch[ch].on = 0; set_active_next(ch); }      /* aktywny -> wyłącz */
    }
    else if (!strcmp(cmd, "CH") && (ch = parse_ch(a1)) >= 0 && a2) {
        S.ch[ch].on = !strcmp(a2, "ON");
        if (S.ch[ch].on) S.active = (uint8_t)ch;
        else if (S.active == ch) set_active_next(ch);
    }
    /* --- bezpośrednie nastawy (pilot, terminal) --- */
    else if (!strcmp(cmd, "TRIG") && (ch = parse_ch(a1)) >= 0) {
        S.trig_ch = (uint8_t)ch;                           /* zbocze i poziom wracają z pamięci kanału */
        if (a2) S.ch[ch].edge = (a2[0] == 'F');
    }
    else if (!strcmp(cmd, "EDGE") && a1) {
        uint8_t *e = &S.ch[S.trig_ch].edge;
        *e = (a1[0] == 'T') ? !*e : (a1[0] == 'F');
    }
    else if (!strcmp(cmd, "LEVEL") && a1) {
        if (!strcmp(a1, "AUTO")) S.ch[S.trig_ch].lauto = 1;
        else if (dir1) level_step(dir1);
        else { S.ch[S.trig_ch].lvl = strtof(a1, NULL) / probe(S.trig_ch); S.ch[S.trig_ch].lauto = 0; }
    }
    else if (!strcmp(cmd, "TB") && a1) {
        int j = dir1 ? S.tb + dir1 : atoi(a1);
        if (j < 0 || j > TB_MAX) ok = 0;
        else if (j != S.tb) { S.tb = (uint8_t)j; tb_dirty = 1; }
    }
    else if (!strcmp(cmd, "HPOS") && a1) {
        if (dir1) hpos_step(dir1);
        else { int h = atoi(a1); S.hpos = (int16_t)(h < -140 ? -140 : h > 140 ? 140 : h); }
    }
    else if (!strcmp(cmd, "VDIV") && (ch = parse_ch(a1)) >= 0 && a2) {
        if (dir2) vdiv_step(ch, dir2); else ok = 0;
    }
    else if (!strcmp(cmd, "OFFSET") && (ch = parse_ch(a1)) >= 0 && a2) {
        if (dir2) offset_step(ch, dir2); else S.ch[ch].off = strtof(a2, NULL) / probe(ch);
    }
    else if (!strcmp(cmd, "PROBE") && (ch = parse_ch(a1)) >= 0 && a2) S.ch[ch].probe10 = (atoi(a2) == 10);
    else if (!strcmp(cmd, "COUP") && (ch = parse_ch(a1)) >= 0 && a2) {
        if (!strcmp(a2, "T")) afe_set_dc(ch, !afe_dc(ch));
        else if (!strcmp(a2, "DC") || !strcmp(a2, "AC")) afe_set_dc(ch, a2[0] == 'D');
        else ok = 0;
        afe_settle = 1;
    }
    else if (!strcmp(cmd, "MEAS") && a1) S.meas = (int8_t)(!strcmp(a1, "OFF") ? -1 : parse_ch(a1));
    else if (!strcmp(cmd, "SCALE") && a1) set_scale(!strcmp(a1, "IN"));
    else if (!strcmp(cmd, "GAIN") && (ch = parse_ch(a1)) >= 0 && a2) {
        int g = -2;
        if (!strcmp(a2, "AUTO")) g = -1;
        else for (int i = 0; i < AFE_NG; i++) if (!strcmp(a2, afe_gain_name[i])) g = i;
        if (g == -2) ok = 0; else S.ch[ch].gforce = (int8_t)g;
    }
    else if (!strcmp(cmd, "CALZERO")) cal_start();
    else if (!strcmp(cmd, "CALCLEAR")) { memset(cal_zero, 0, sizeof cal_zero); calib_save(); }
    else if (!strcmp(cmd, "CAL1K") && a1) set_cal1k(!strcmp(a1, "ON"));
    else if (!strcmp(cmd, "OVS") && a1) { S.ovs = !strcmp(a1, "ON"); tb_dirty = 1; }
    else if (!strcmp(cmd, "HELP")) help();
    else if (strcmp(cmd, "STAT")) ok = 0;

    layout();
    auto_gain();
    view_dirty = 1;
    status_dirty = 1;
    if (acq_busy() && (uint64_t)acq_n() * 1000u > 50u * (uint64_t)acq_fs_hz())
        acq_abort();                                      /* blok > 50 ms: przerwij, żeby od razu odświeżyć ekran */
    if (!quiet) {
        printf(ok ? "OK\r\n" : "ERR nieznana komenda (HELP)\r\n");
        send_status();
    }
}

/* ---------------------------------------------------------------- start i pętla */

void app_init(void)
{
    dwt_init();
    defaults();
    printf("\r\n=== Rejestrator 4ch, SYSCLK = %lu Hz ===\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("Wpisz HELP. Pilot z PC: tools/remote.py\r\n");
    afe_init();                         /* najpierw: znane wzmocnienie G1 na wyjściach 74HC595 */
    calib_load();
    /* podświetlenie: LED wyświetlacza (J8 pin 8) = PC7 = TIM8_CH2, PWM 20 kHz, CCR2 7799 = pełna jasność */
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    /* sygnał kalibracyjny 1 kHz, 50 % na J13 (PC12 = TIM5_CH2) - kompensacja dzielnika, testy */
    HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2);
    lcd_init();
    gfx_init();
    for (int c = 0; c < NCH; c++) gfx_tr[c].color = ch_color[c];
    layout();
    auto_gain();
    draw_static();

    /* DAC1 (PA4) nie jest używany: generator testowy usunięty - nakładał się na mierzony sygnał */
    acq_init();
    link_init();
    ui_init();
    t_sec = HAL_GetTick() + 1000;
}

void app_poll(void)
{
    char ln[96];
    gfx_poll();
    acq_poll();

    while (ui_poll(ln, sizeof ln)) command(ln, 1);       /* przyciski i enkodery */
    while (link_getline(ln, sizeof ln)) command(ln, 0);  /* PC */

    if (menu_open && HAL_GetTick() - t_menu > MENU_MS) { menu_open = 0; view_dirty = 1; status_dirty = 1; }

    if (tb_dirty) {                                       /* zmiana podstawy czasu */
        acq_abort();
        apply_timebase();
        tb_dirty = 0;
        have_block = 0;
        half_min = 0xFFFFFFFFu;
        status_dirty = 1;
    }
    if (afe_settle) {                                     /* wzmocnienie / AC/DC: blok w trakcie jest nieważny */
        acq_abort();
        afe_settle = 0;
    }
    if (!S.run && acq_busy()) acq_abort();                /* STOP przerywa zbieranie */

    if (acq_take()) {                                     /* nowy blok */
        have_block = 1;
        if (acq_half_cycles() < half_min) half_min = acq_half_cycles();
        if (cal_g >= 0) cal_block(); else process_block(1);
    } else if (view_dirty && have_block) {
        process_block(0);                                 /* przerysowanie bez nowego bloku */
    }
    view_dirty = 0;

    /* następny blok zbiera się w tle (także w czasie wysyłania ramki), ale dopiero po wysłaniu
     * nowego wzmocnienia do 74HC595 */
    /* Blok zbiera się TYLKO gdy SPI wyświetlacza stoi - zbocza SPI (39 MHz) zakłócały tor analogowy.
     * Kolejność: blok -> obróbka -> ramka na ekran -> następny blok. */
    if (S.run && !acq_busy() && !afe_pending() && !gfx_busy() && !frame_pending && !status_dirty) acq_arm();

    if (gfx_busy() || acq_busy()) return;                 /* SPI tylko między blokami */

    if (afe_pending()) afe_flush();                       /* 74HC595 tylko przy wolnym SPI */

    if ((int32_t)(HAL_GetTick() - t_sec) >= 0) {          /* raz na sekundę */
        t_sec += 1000;
        fps = gfx_frames() - frames_last;
        frames_last = gfx_frames();
        int code = acq_read_vbat();                        /* ADC wolny (acq_busy = 0 w tym miejscu) */
        if (code >= 0) {
            float v = (float)code * LSB_V * VBAT_DIV;
            vbat = (vbat < 0.0f || fabsf(v - vbat) > 0.5f) ? v : vbat + 0.3f * (v - vbat);   /* wygładzanie */
        }
        blink++;
        status_dirty = 1;
        send_status();
        half_min = 0xFFFFFFFFu;
    }
    if (status_dirty) { draw_status(); status_dirty = 0; }
    if (frame_pending) { frame_pending = 0; gfx_start_frame(); }
}
