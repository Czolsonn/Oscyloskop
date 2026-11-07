/*
 * app.c - logika urządzenia: nastawy, wyzwalanie i tryby (A6), podstawa czasu (A7),
 *         obsługa z płytki (C1), wzmocnienie i AC/DC (C2), przeliczenie na wolty wejściowe.
 *
 * Wszystkie nastawy zmieniają komendy tekstowe - z przycisków/enkoderów (ui.c) albo z PC (link.c,
 * tools/remote.py). Pętla: blok 4 kanałów -> wyzwolenie -> decyzja trybu -> kolumny min/max -> ramka DMA.
 *
 * Ekran: włączone kanały dzielą pole 300x200 px na równe pasy (1 kanał = cały ekran, 2 = po połowie...).
 * Pas pokazuje pełny zakres ADC kanału; środek pasa = VMID = 0 V na wejściu BNC.
 * Skala: IN  = wolty na wejściu BNC (przez wzmocnienie toru - płytka),
 *        ADC = wolty na pinie ADC (testy na samej Nucleo, sygnał z DAC prosto na pin).
 *
 * Tryby:  AUTO   - obraz z wyzwoleniem, a gdy go brak >100 ms, obraz bez wyzwolenia
 *         NORM   - obraz tylko z wyzwoleniem (inaczej zostaje poprzedni, status WAIT)
 *         SINGLE - pierwszy obraz z wyzwoleniem, potem STOP
 * Nastawy wyzwalania (zbocze, poziom / AUTO) są pamiętane osobno dla każdego kanału.
 */
#include "app.h"
#include "acq.h"
#include "afe.h"
#include "gen.h"
#include "gfx.h"
#include "lcd.h"
#include "link.h"
#include "ui.h"
#include "main.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_TOP_H  16
#define BAR_BOT_Y  (PLOT_Y + PLOT_H)      /* 216 */
#define COL_BAR    RGB565(28, 28, 28)
#define AUTO_MS    100                    /* AUTO: obraz bez wyzwolenia po tym czasie */
#define CODE_MID   2048                   /* VMID = VREF/2 -> 0 V na wejściu */

enum { MODE_AUTO = 0, MODE_NORM, MODE_SINGLE };
static const char *mode_name[] = { "AUTO", "NORM", "SINGLE" };

typedef struct {
    uint8_t  run;            /* 1 = RUN, 0 = STOP */
    uint8_t  mode;           /* MODE_* */
    uint8_t  trig_ch;        /* źródło wyzwalania 0..3 */
    uint8_t  active;         /* kanał aktywny (enkoder Y, przyciski CH) */
    uint8_t  tb;             /* indeks podstawy czasu */
    uint8_t  ovs;            /* nadpróbkowanie ADC przy wolnych T/dz */
    uint8_t  scale_in;       /* 1 = wolty na wejściu BNC, 0 = wolty na pinie ADC */
    uint8_t  on[NCH];
    uint8_t  edge[NCH];      /* 0 = narastające, 1 = opadające - osobno dla każdego kanału */
    uint8_t  lvl_auto[NCH];  /* 1 = poziom = środek przebiegu */
    uint16_t lvl[NCH];       /* poziom ręczny [kod ADC] */
} settings_t;

static settings_t S = {
    .run = 1, .mode = MODE_AUTO, .trig_ch = 0, .active = 0, .tb = 4, .ovs = 1, .scale_in = 1,
    .on = { 1, 1, 1, 1 }, .edge = { 0, 0, 0, 0 }, .lvl_auto = { 1, 1, 1, 1 },
    .lvl = { CODE_MID, CODE_MID, CODE_MID, CODE_MID },
};

static const uint16_t ch_color[NCH] = { C_YELLOW, C_CYAN, C_MAGENTA, C_LIME };

static uint8_t  tb_dirty = 1, view_dirty, status_dirty = 1, frame_pending, have_block, afe_settle;
static const char *state = "AUTO";       /* TRIG / AUTO / WAIT / STOP */
static uint32_t t_sec, t_last_frame, frames_last, fps;
static uint32_t half_min = 0xFFFFFFFFu;
static uint16_t vmin[NCH], vmax[NCH], lvl_code;
static uint32_t f_mHz;

/* pasy kanałów */
static int16_t band_y0[NCH], band_h = PLOT_H;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ---------------------------------------------------------------- jednostki */

/* wolty (w aktualnej skali) na jeden kod ADC kanału ch */
static float v_per_code(int ch)
{
    float v = (float)VREF_MV / 1000.0f / 4096.0f;
    return S.scale_in ? v / afe_gain_in2adc[afe_gain(ch)] : v;
}
static float code_to_v(int ch, float code)
{
    return S.scale_in ? (code - CODE_MID) * v_per_code(ch) : code * v_per_code(ch);
}
static int32_t v_to_code(int ch, float v)
{
    int32_t c = (int32_t)(S.scale_in ? CODE_MID + v / v_per_code(ch) : v / v_per_code(ch));
    return c < 0 ? 0 : c > 4095 ? 4095 : c;
}

/* "12.3V", "-1.25V", "850mV" - bez printf dla float */
static void fmt_v(char *s, int n, float v)
{
    const char *sg = (v < 0.0f) ? "-" : "";
    if (v < 0.0f) v = -v;
    uint32_t mv = (uint32_t)(v * 1000.0f + 0.5f);
    if (mv < 1000)       snprintf(s, (size_t)n, "%s%lumV", sg, (unsigned long)mv);
    else if (mv < 10000) snprintf(s, (size_t)n, "%s%lu.%02luV", sg, (unsigned long)(mv / 1000), (unsigned long)(mv / 10 % 100));
    else                 snprintf(s, (size_t)n, "%s%lu.%luV", sg, (unsigned long)(mv / 1000), (unsigned long)(mv / 100 % 10));
}

/* ---------------------------------------------------------------- pasy kanałów */

static void layout(void)
{
    int n = 0;
    for (int c = 0; c < NCH; c++) n += S.on[c];
    band_h = (int16_t)(n ? PLOT_H / n : PLOT_H);
    int k = 0;
    for (int c = 0; c < NCH; c++) {
        band_y0[c] = (int16_t)(k * band_h);
        if (S.on[c]) k++;
    }
}

/* Kod ADC -> piksel w pasie kanału ch (pełny zakres 0..4095 = wysokość pasa) */
static inline int16_t code_to_y(int ch, int32_t code)
{
    return (int16_t)(band_y0[ch] + (band_h - 1) - code * band_h / 4096);
}

/* ---------------------------------------------------------------- obraz */

static void draw_static(void)
{
    lcd_fill(C_BLACK);
    lcd_fill_rect(0, 0, LCD_W, BAR_TOP_H, COL_BAR);
    lcd_fill_rect(0, BAR_BOT_Y, LCD_W, LCD_H - BAR_BOT_Y, COL_BAR);
}

static void draw_status(void)
{
    char s[64], lv[12], pad[54];
    fmt_v(lv, sizeof lv, code_to_v(S.trig_ch, lvl_code));
    snprintf(s, sizeof s, "%-4s %-4s %s/dz CH%d%c%s%s %-4s f=%lu.%02lukHz",
             S.run ? "RUN" : "STOP", S.mode == MODE_SINGLE ? "SNGL" : mode_name[S.mode],
             acq_tb(S.tb)->label, S.trig_ch + 1, S.edge[S.trig_ch] ? 'v' : '^', lv,
             S.lvl_auto[S.trig_ch] ? "A" : "", state,
             (unsigned long)(f_mHz / 1000000u), (unsigned long)(f_mHz / 10000u % 100u));
    snprintf(pad, sizeof pad, "%-53.53s", s);             /* stała szerokość - zamazuje stary tekst */
    lcd_text(2, 4, pad, S.run ? C_WHITE : C_ORANGE, COL_BAR, 1);

    /* prawa kolumna: znacznik 0 V (środek pasa) każdego kanału i poziomu wyzwolenia */
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    for (int c = 0; c < NCH; c++) {
        if (!S.on[c]) continue;
        uint16_t y = PLOT_Y + code_to_y(c, CODE_MID) - 5;
        lcd_fill_rect(PLOT_W + 3, y, 15, 11, ch_color[c]);
        char t[2] = { (char)('1' + c), 0 };
        lcd_text(PLOT_W + 8, y + 2, t, C_BLACK, ch_color[c], 1);
    }
    if (S.on[S.trig_ch]) {
        int ty = PLOT_Y + code_to_y(S.trig_ch, lvl_code) - 5;
        if (ty < PLOT_Y) ty = PLOT_Y;
        if (ty > PLOT_Y + PLOT_H - 11) ty = PLOT_Y + PLOT_H - 11;
        lcd_fill_rect(PLOT_W + 3, (uint16_t)ty, 15, 11, C_ORANGE);
        lcd_text(PLOT_W + 8, (uint16_t)ty + 2, "T", C_BLACK, C_ORANGE, 1);
    }

    /* dolny pasek: dwie linie na kanał - "1*DC G1 34V" (zakres +-) i Vpp */
    for (int c = 0; c < NCH; c++) {
        char l1[24], l2[16], v[12], p1[14], p2[14];
        uint16_t col = S.on[c] ? ch_color[c] : C_DGRAY, fg = S.on[c] ? C_BLACK : C_GRAY;
        /* zakres pasa: +-pełna skala na wejściu (IN) albo 0..VREF na pinie (ADC), krótko: 34V, 6.9V */
        uint32_t fs_dv = (uint32_t)((S.scale_in ? 2048.0f : 4096.0f) * v_per_code(c) * 10.0f + 0.5f);
        if (fs_dv >= 100) snprintf(v, sizeof v, "%luV", (unsigned long)(fs_dv / 10));
        else              snprintf(v, sizeof v, "%lu.%luV", (unsigned long)(fs_dv / 10), (unsigned long)(fs_dv % 10));
        snprintf(l1, sizeof l1, "%d%c%s x%s %s", c + 1, (c == S.active) ? '*' : ' ',
                 afe_dc(c) ? "DC" : "AC", afe_gain_name[afe_gain(c)], v);
        if (S.on[c]) {
            fmt_v(v, sizeof v, (float)(vmax[c] - vmin[c]) * v_per_code(c));
            snprintf(l2, sizeof l2, "%spp", v);
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
static void build_view(float pos0)
{
    const float spx = acq_spx();
    const int n = acq_n();
    const int nsub = (spx >= 1.0f) ? (int)(spx + 0.5f) : 1;
    const float step = spx / (float)nsub;

    for (int c = 0; c < NCH; c++) {
        gfx_tr[c].on = S.on[c];
        if (!S.on[c]) continue;
        const uint16_t *d = acq_ch(c);
        trace_t *tr = &gfx_tr[c];
        float prev = 0.0f;
        for (int x = 0; x < PLOT_W; x++) {
            float mn = 1e9f, mx = -1e9f, v = 0.0f;
            for (int k = 0; k < nsub; k++) {
                float pos = pos0 + (float)x * spx + (float)k * step;
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
            tr->ymin[x] = code_to_y(c, (int32_t)(mx + 0.5f));   /* większy kod = wyżej */
            tr->ymax[x] = code_to_y(c, (int32_t)(mn + 0.5f));
        }
    }
    gfx_trig_y = S.on[S.trig_ch] ? code_to_y(S.trig_ch, lvl_code) : -1;
    frame_pending = 1;
    t_last_frame = HAL_GetTick();
}

/* ---------------------------------------------------------------- obróbka bloku */

/* Częstotliwość: średni odstęp między kolejnymi wyzwoleniami w całym bloku */
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
    if (S.lvl_auto[tc]) {
        lvl_code = (uint16_t)((vmin[tc] + vmax[tc]) / 2);
        hyst = (uint16_t)((vmax[tc] - vmin[tc]) / 20);
        if (hyst < 8) hyst = 8;
    } else {
        lvl_code = S.lvl[tc];
        hyst = 20;
    }

    /* wyzwolenie na środku ekranu: szukamy w [half, n - half] */
    const uint16_t *d = acq_ch(tc);
    const float half = (float)(PLOT_W / 2) * acq_spx();
    int32_t from = (int32_t)half + 2, to = n - (int32_t)half - 2;
    int32_t t = acq_find_trigger(d, from, to, lvl_code, hyst, S.edge[tc]);
    f_mHz = measure_f_mHz(d, n, lvl_code, hyst, S.edge[tc]);

    if (t >= 0) {
        float a = d[t - 1], b = d[t];
        float frac = (b != a) ? ((float)lvl_code - a) / (b - a) : 1.0f;   /* ułamek próbki */
        build_view((float)(t - 1) + frac - half);
        if (live) {
            state = "TRIG";
            if (S.mode == MODE_SINGLE) { S.run = 0; state = "STOP"; }
        }
    } else if (!live) {
        build_view(0.0f);
    } else if (S.mode == MODE_AUTO && (HAL_GetTick() - t_last_frame) >= AUTO_MS) {
        build_view(0.0f);                                  /* AUTO bez wyzwolenia */
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
    int32_t lvl_mv = (int32_t)(code_to_v(tc, lvl_code) * 1000.0f);
    int32_t vpp[NCH];
    for (int c = 0; c < NCH; c++) vpp[c] = (int32_t)((float)(vmax[c] - vmin[c]) * v_per_code(c) * 1000.0f);
    printf("#S run=%d mode=%s trig=%d edge=%c lauto=%d lvl=%ld act=%d scale=%s tb=%d tdiv=%s fs=%lu ovs=%lu"
           " ovsen=%d n=%d st=%s f=%lu.%03lu vpp=%ld,%ld,%ld,%ld on=%d%d%d%d g=%s,%s,%s,%s cpl=%c%c%c%c"
           " fps=%lu fsm=%lu gen=%s,%lu\r\n",
           S.run, mode_name[S.mode], tc + 1, S.edge[tc] ? 'F' : 'R', S.lvl_auto[tc], (long)lvl_mv,
           S.active + 1, S.scale_in ? "IN" : "ADC", S.tb, acq_tb(S.tb)->label,
           (unsigned long)acq_fs_hz(), (unsigned long)acq_ovs(), S.ovs, acq_n(), state,
           (unsigned long)(f_mHz / 1000u), (unsigned long)(f_mHz % 1000u),
           (long)vpp[0], (long)vpp[1], (long)vpp[2], (long)vpp[3],
           S.on[0], S.on[1], S.on[2], S.on[3],
           afe_gain_name[afe_gain(0)], afe_gain_name[afe_gain(1)], afe_gain_name[afe_gain(2)], afe_gain_name[afe_gain(3)],
           afe_dc(0) ? 'D' : 'A', afe_dc(1) ? 'D' : 'A', afe_dc(2) ? 'D' : 'A', afe_dc(3) ? 'D' : 'A',
           (unsigned long)fps, (unsigned long)fsm, gen_name(gen_shape()), (unsigned long)(gen_f_mhz() / 1000u));
}

static void help(void)
{
    printf("Komendy (te same wysylaja przyciski plytki):\r\n"
           "  RUN | STOP | SINGLE | RUNBTN        praca / zatrzymanie / jeden obraz / przelacz\r\n"
           "  MODE AUTO|NORM|T                    tryb wyzwalania (T = przelacz)\r\n"
           "  TRIG <1-4> [R|F]                    zrodlo wyzwalania (pamieta zbocze i poziom kanalu)\r\n"
           "  EDGE R|F|T                          zbocze kanalu wyzwalajacego\r\n"
           "  LEVEL <V>|+|-|AUTO                  poziom w woltach biezacej skali, AUTO = srodek przebiegu\r\n"
           "  TB <0-%d>|+|-|<np. 100us>           podstawa czasu, TBLIST = lista\r\n"
           "  CH <1-4> ON|OFF | CHBTN <1-4>       kanal wl/wyl | przycisk kanalu (wl / aktywny / wyl)\r\n"
           "  GAIN <1-4|A> 1|2|5|10|+|-           wzmocnienie (A = kanal aktywny)\r\n"
           "  COUP <1-4|A> AC|DC|T                sprzezenie\r\n"
           "  SCALE IN|ADC                        wolty na wejsciu BNC / na pinie ADC (test na Nucleo)\r\n"
           "  OVS ON|OFF                          nadprobkowanie ADC przy wolnych T/dz\r\n"
           "  GEN SIN|SQR|TRI <Hz> | GEN OFF      generator na PA4 (A2), np. GEN SIN 10k\r\n"
           "  STAT | HELP\r\n", TB_COUNT - 1);
}

static uint32_t parse_hz(const char *s)
{
    char *e;
    float v = strtof(s, &e);
    if (*e == 'K') v *= 1000.0f;
    return (v > 0.0f) ? (uint32_t)(v + 0.5f) : 0;
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
        if (S.on[j]) { S.active = (uint8_t)j; return; }
    }
}

/* quiet = 1: komenda z przycisków (bez odpowiedzi na UART - nie blokuje enkoderów) */
static void command(char *ln, int quiet)
{
    for (char *p = ln; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *cmd = strtok(ln, " \t");
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    int ok = 1, ch;
    if (!cmd) return;

    if (!strcmp(cmd, "RUN"))         { S.run = 1; if (S.mode == MODE_SINGLE) S.mode = MODE_AUTO; }
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
    else if (!strcmp(cmd, "TRIG") && (ch = parse_ch(a1)) >= 0) {
        S.trig_ch = (uint8_t)ch;                            /* zbocze i poziom wracają z pamięci kanału */
        if (a2) S.edge[ch] = (a2[0] == 'F');
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "EDGE") && a1) {
        uint8_t *e = &S.edge[S.trig_ch];
        *e = (a1[0] == 'T') ? !*e : (a1[0] == 'F');
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "LEVEL") && a1) {
        int tc = S.trig_ch;
        if (!strcmp(a1, "AUTO")) S.lvl_auto[tc] = 1;
        else if (!strcmp(a1, "+") || !strcmp(a1, "-")) {
            int32_t step = 2 * 4096 / band_h;                  /* 2 piksele pasa na ząbek */
            int32_t c = S.lvl_auto[tc] ? lvl_code : S.lvl[tc];
            c += (a1[0] == '+') ? step : -step;
            S.lvl[tc] = (uint16_t)(c < 0 ? 0 : c > 4095 ? 4095 : c);
            S.lvl_auto[tc] = 0;
        } else {
            S.lvl[tc] = (uint16_t)v_to_code(tc, strtof(a1, NULL));
            S.lvl_auto[tc] = 0;
        }
        if (!S.lvl_auto[tc]) lvl_code = S.lvl[tc];
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "TB") && a1) {
        int idx = S.tb;
        if (!strcmp(a1, "+")) idx++;
        else if (!strcmp(a1, "-")) idx--;
        else if (isdigit((unsigned char)a1[0]) && !strpbrk(a1, "UM")) idx = atoi(a1);
        else {
            idx = -1;
            for (int i = 0; i < TB_COUNT; i++) {
                char lab[8];
                snprintf(lab, sizeof lab, "%s", acq_tb(i)->label);
                for (char *p = lab; *p; p++) *p = (char)toupper((unsigned char)*p);
                if (!strcmp(lab, a1)) idx = i;
            }
        }
        if (quiet && idx < 0) idx = 0;                        /* enkoder na krańcu: zostaje */
        if (quiet && idx >= TB_COUNT) idx = TB_COUNT - 1;
        if (idx < 0 || idx >= TB_COUNT) ok = 0;
        else if (idx != S.tb) { S.tb = (uint8_t)idx; tb_dirty = 1; }
    }
    else if (!strcmp(cmd, "TBLIST")) {
        for (int i = 0; i < TB_COUNT; i++) printf("#TB %d %s\r\n", i, acq_tb(i)->label);
    }
    else if (!strcmp(cmd, "CH") && (ch = parse_ch(a1)) >= 0 && a2) {
        S.on[ch] = !strcmp(a2, "ON");
        if (S.on[ch]) S.active = (uint8_t)ch;
        else if (S.active == ch) set_active_next(ch);
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "CHBTN") && (ch = parse_ch(a1)) >= 0) {
        if (!S.on[ch])            { S.on[ch] = 1; S.active = (uint8_t)ch; }   /* wyłączony -> włącz */
        else if (S.active != ch)  { S.active = (uint8_t)ch; }                 /* włączony -> aktywny */
        else                      { S.on[ch] = 0; set_active_next(ch); }      /* aktywny -> wyłącz */
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "GAIN") && (ch = parse_ch(a1)) >= 0 && a2) {
        int g = -1;
        if (!strcmp(a2, "+")) g = afe_gain(ch) + 1;
        else if (!strcmp(a2, "-")) g = afe_gain(ch) - 1;
        else for (int i = 0; i < AFE_NG; i++) if (!strcmp(a2, afe_gain_name[i])) g = i;
        if (g < -1 || (g == -1 && strcmp(a2, "-"))) ok = 0;
        else { afe_set_gain(ch, g); afe_settle = 1; view_dirty = 1; }   /* afe_set_gain przycina 0..3 */
    }
    else if (!strcmp(cmd, "COUP") && (ch = parse_ch(a1)) >= 0 && a2) {
        if (!strcmp(a2, "T")) afe_set_dc(ch, !afe_dc(ch));
        else if (!strcmp(a2, "DC") || !strcmp(a2, "AC")) afe_set_dc(ch, a2[0] == 'D');
        else ok = 0;
        afe_settle = 1;
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "SCALE") && a1) { S.scale_in = !strcmp(a1, "IN"); view_dirty = 1; }
    else if (!strcmp(cmd, "OVS") && a1) { S.ovs = !strcmp(a1, "ON"); tb_dirty = 1; }
    else if (!strcmp(cmd, "GEN") && a1) {
        int sh = !strcmp(a1, "SIN") ? GEN_SIN : !strcmp(a1, "SQR") ? GEN_SQR :
                 !strcmp(a1, "TRI") ? GEN_TRI : !strcmp(a1, "OFF") ? GEN_OFF : -1;
        uint32_t f = a2 ? parse_hz(a2) : (gen_f_mhz() / 1000u ? gen_f_mhz() / 1000u : 10000u);
        if (sh < 0) ok = 0; else gen_set(sh, f);
    }
    else if (!strcmp(cmd, "HELP")) help();
    else if (strcmp(cmd, "STAT")) ok = 0;

    if (view_dirty) layout();
    status_dirty = 1;
    if (!quiet) {
        printf(ok ? "OK\r\n" : "ERR nieznana komenda (HELP)\r\n");
        send_status();
    }
}

/* ---------------------------------------------------------------- start i pętla */

void app_init(void)
{
    dwt_init();
    printf("\r\n=== Rejestrator 4ch (A6, A7, C1, C2), SYSCLK = %lu Hz ===\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("Wpisz HELP. Sterowanie z PC: tools/remote.py\r\n");
    afe_init();                         /* najpierw: znane wzmocnienie G1 na wyjściach 74HC595 */
    lcd_init();
    gfx_init();
    for (int c = 0; c < NCH; c++) gfx_tr[c].color = ch_color[c];
    layout();
    draw_static();

    gen_set(GEN_SIN, 10000);
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

    if (tb_dirty) {                                       /* zmiana podstawy czasu */
        acq_abort();
        acq_set_timebase(S.tb, S.ovs);
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
        process_block(1);
    } else if (view_dirty && have_block && (!S.run || S.mode != MODE_AUTO)) {
        process_block(0);                                 /* przerysowanie bez nowego bloku */
    }
    view_dirty = 0;

    /* następny blok zbiera się w tle (także w czasie wysyłania ramki), ale dopiero po wysłaniu
     * nowego wzmocnienia do 74HC595 */
    if (S.run && !acq_busy() && !afe_pending()) acq_arm();

    if (gfx_busy()) return;                               /* SPI zajęte - reszta między ramkami */

    if (afe_pending()) afe_flush();                       /* 74HC595 tylko przy wolnym SPI */

    if ((int32_t)(HAL_GetTick() - t_sec) >= 0) {          /* raz na sekundę */
        t_sec += 1000;
        fps = gfx_frames() - frames_last;
        frames_last = gfx_frames();
        status_dirty = 1;
        send_status();
        half_min = 0xFFFFFFFFu;
    }
    if (status_dirty) { draw_status(); status_dirty = 0; }
    if (frame_pending) { frame_pending = 0; gfx_start_frame(); }
}
