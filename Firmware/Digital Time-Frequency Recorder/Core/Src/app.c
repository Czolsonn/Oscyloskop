/*
 * app.c - kroki A6 + A7: wyzwalanie i tryby, podstawa czasu, sterowanie komendami z PC.
 *
 * Nastawy (settings_t S) zmieniają: komendy z UART (link.c, program tools/remote.py) i przycisk B1.
 * Pętla: blok 4 kanałów -> wyzwolenie (kanał, zbocze, poziom) -> decyzja trybu -> kolumny min/max
 *        -> ramka przez DMA. Ekran: każdy kanał we własnym pasie 50 px (0..3,3 V), 30 px na działkę czasu.
 *
 * Tryby:  AUTO   - obraz z wyzwoleniem, a gdy go brak >100 ms, obraz bez wyzwolenia
 *         NORM   - obraz tylko z wyzwoleniem (inaczej zostaje poprzedni, status WAIT)
 *         SINGLE - pierwszy obraz z wyzwoleniem, potem STOP
 */
#include "app.h"
#include "acq.h"
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
#define BAND_H     (PLOT_H / NCH)         /* 50 px na kanał */
#define VREF_MV    3300                   /* Nucleo: VREF+ = VDDA = 3,3 V (na płytce 3,25 V) */
#define AUTO_MS    100                    /* AUTO: obraz bez wyzwolenia po tym czasie */

enum { MODE_AUTO = 0, MODE_NORM, MODE_SINGLE };
static const char *mode_name[] = { "AUTO", "NORM", "SINGLE" };

typedef struct {
    uint8_t run;          /* 1 = RUN, 0 = STOP */
    uint8_t mode;         /* MODE_* */
    uint8_t trig_ch;      /* 0..3 */
    uint8_t edge;         /* 0 = narastające, 1 = opadające */
    int16_t level_mv;     /* poziom wyzwolenia [mV], -1 = automatycznie środek przebiegu */
    uint8_t tb;           /* indeks podstawy czasu (acq_tb) */
    uint8_t ovs;          /* 1 = nadpróbkowanie ADC przy wolnych T/dz */
    uint8_t on[NCH];      /* kanały widoczne */
} settings_t;

static settings_t S = { 1, MODE_AUTO, 0, 0, -1, 4, 1, { 1, 1, 1, 1 } };

static const uint16_t ch_color[NCH] = { C_YELLOW, C_CYAN, C_MAGENTA, C_LIME };

static uint8_t  tb_dirty = 1, view_dirty, status_dirty = 1, frame_pending, have_block;
static const char *state = "AUTO";       /* TRIG / AUTO / WAIT / STOP */
static uint32_t t_sec, t_last_frame, frames_last, fps;
static uint32_t half_min = 0xFFFFFFFFu;
static uint16_t vmin[NCH], vmax[NCH], lvl_code;
static uint32_t f_mHz;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Kod ADC -> piksel w pasie kanału ch (pełny zakres 0..4095 = 50 px) */
static inline int16_t code_to_y(int ch, int32_t code)
{
    return (int16_t)(ch * BAND_H + (BAND_H - 1) - code * BAND_H / 4096);
}

/* ---------------------------------------------------------------- obraz */

static void draw_static(void)
{
    lcd_fill(C_BLACK);
    lcd_fill_rect(0, 0, LCD_W, BAR_TOP_H, COL_BAR);
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    lcd_fill_rect(0, BAR_BOT_Y, LCD_W, LCD_H - BAR_BOT_Y, COL_BAR);
}

static void draw_status(void)
{
    char s[64], lv[12];
    uint16_t lmv = (uint16_t)((uint32_t)lvl_code * VREF_MV / 4095);
    snprintf(lv, sizeof lv, "%u.%02uV%s", lmv / 1000, lmv / 10 % 100, S.level_mv < 0 ? "A" : "");
    snprintf(s, sizeof s, "%-4s %-4s %s/dz CH%d%c%s %-4s f=%lu.%02lukHz",
             S.run ? "RUN" : "STOP", S.mode == MODE_SINGLE ? "SNGL" : mode_name[S.mode],
             acq_tb(S.tb)->label, S.trig_ch + 1, S.edge ? 'v' : '^', lv, state,
             (unsigned long)(f_mHz / 1000000u), (unsigned long)(f_mHz / 10000u % 100u));
    char pad[54];
    snprintf(pad, sizeof pad, "%-53.53s", s);           /* stała szerokość - zamazuje stary tekst */
    lcd_text(2, 4, pad, S.run ? C_WHITE : C_ORANGE, COL_BAR, 1);

    /* prawa kolumna: znaczniki kanałów i poziomu wyzwolenia */
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    for (int c = 0; c < NCH; c++) {
        uint16_t y = PLOT_Y + c * BAND_H + 2;
        uint16_t col = S.on[c] ? ch_color[c] : C_GRAY;
        lcd_fill_rect(PLOT_W + 3, y, 15, 11, col);
        char t[2] = { (char)('1' + c), 0 };
        lcd_text(PLOT_W + 8, y + 2, t, C_BLACK, col, 1);
    }
    uint16_t ty = PLOT_Y + code_to_y(S.trig_ch, lvl_code) - 5;
    lcd_fill_rect(PLOT_W + 3, ty, 15, 11, C_ORANGE);
    lcd_text(PLOT_W + 8, ty + 2, "T", C_BLACK, C_ORANGE, 1);

    /* dolny pasek: Vpp kanałów */
    for (int c = 0; c < NCH; c++) {
        uint32_t mv = (uint32_t)(vmax[c] - vmin[c]) * VREF_MV / 4095;
        char t[16];
        if (S.on[c]) snprintf(t, sizeof t, "%d:%lu.%02luVpp", c + 1, (unsigned long)(mv / 1000), (unsigned long)(mv / 10 % 100));
        else         snprintf(t, sizeof t, "%d:off", c + 1);
        uint16_t col = S.on[c] ? ch_color[c] : C_DGRAY;
        lcd_fill_rect(2 + c * 79, BAR_BOT_Y + 3, 76, 11, col);
        lcd_text(4 + c * 79, BAR_BOT_Y + 5, t, S.on[c] ? C_BLACK : C_GRAY, col, 1);
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
    gfx_trig_y = code_to_y(S.trig_ch, lvl_code);
    frame_pending = 1;
    t_last_frame = HAL_GetTick();
}

/* ---------------------------------------------------------------- obróbka bloku */

/* Częstotliwość: średni odstęp między kolejnymi wyzwoleniami w całym bloku */
static uint32_t measure_f_mHz(const uint16_t *d, int n, uint16_t level, uint16_t hyst)
{
    int32_t first = acq_find_trigger(d, 1, n, level, hyst, S.edge), last = first, k = 0;
    while (last >= 0) {
        int32_t nx = acq_find_trigger(d, last + 1, n, level, hyst, S.edge);
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

    /* poziom i histereza */
    const int tc = S.trig_ch;
    uint16_t hyst;
    if (S.level_mv < 0) {
        lvl_code = (uint16_t)((vmin[tc] + vmax[tc]) / 2);
        hyst = (uint16_t)((vmax[tc] - vmin[tc]) / 20);
        if (hyst < 8) hyst = 8;
    } else {
        lvl_code = (uint16_t)((uint32_t)S.level_mv * 4095 / VREF_MV);
        hyst = 20;
    }

    /* wyzwolenie na środku ekranu: szukamy w [half, n - half] */
    const uint16_t *d = acq_ch(tc);
    const float half = (float)(PLOT_W / 2) * acq_spx();
    int32_t from = (int32_t)half + 2, to = n - (int32_t)half - 2;
    int32_t t = acq_find_trigger(d, from, to, lvl_code, hyst, S.edge);
    f_mHz = measure_f_mHz(d, n, lvl_code, hyst);

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
    printf("#S run=%d mode=%s trig=%d edge=%c level=%d lvl=%u tb=%d tdiv=%s fs=%lu ovs=%lu ovsen=%d n=%d"
           " st=%s f=%lu.%03lu vpp=%lu,%lu,%lu,%lu on=%d%d%d%d fps=%lu fsm=%lu gen=%s,%lu\r\n",
           S.run, mode_name[S.mode], S.trig_ch + 1, S.edge ? 'F' : 'R', S.level_mv,
           (unsigned)((uint32_t)lvl_code * VREF_MV / 4095), S.tb, acq_tb(S.tb)->label,
           (unsigned long)acq_fs_hz(), (unsigned long)acq_ovs(), S.ovs, acq_n(), state,
           (unsigned long)(f_mHz / 1000u), (unsigned long)(f_mHz % 1000u),
           (unsigned long)((vmax[0] - vmin[0]) * VREF_MV / 4095), (unsigned long)((vmax[1] - vmin[1]) * VREF_MV / 4095),
           (unsigned long)((vmax[2] - vmin[2]) * VREF_MV / 4095), (unsigned long)((vmax[3] - vmin[3]) * VREF_MV / 4095),
           S.on[0], S.on[1], S.on[2], S.on[3], (unsigned long)fps, (unsigned long)fsm,
           gen_name(gen_shape()), (unsigned long)(gen_f_mhz() / 1000u));
}

static void help(void)
{
    printf("Komendy:\r\n"
           "  RUN | STOP | SINGLE            praca / zatrzymanie / jeden obraz\r\n"
           "  MODE AUTO|NORM                 tryb wyzwalania\r\n"
           "  TRIG <1-4> [R|F]               kanal i zbocze wyzwalania\r\n"
           "  LEVEL <mV>|AUTO                poziom wyzwalania (AUTO = srodek przebiegu)\r\n"
           "  TB <0-%d>|+|-|<np. 100us>      podstawa czasu, TBLIST = lista\r\n"
           "  CH <1-4> ON|OFF                widocznosc kanalu\r\n"
           "  OVS ON|OFF                     nadprobkowanie ADC przy wolnych T/dz\r\n"
           "  GEN SIN|SQR|TRI <Hz> | GEN OFF generator na PA4 (A2), np. GEN SIN 10k\r\n"
           "  STAT | HELP\r\n", TB_COUNT - 1);
}

static uint32_t parse_hz(const char *s)
{
    char *e;
    float v = strtof(s, &e);
    if (*e == 'K') v *= 1000.0f;
    return (v > 0.0f) ? (uint32_t)(v + 0.5f) : 0;
}

static void command(char *ln)
{
    for (char *p = ln; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *cmd = strtok(ln, " \t");
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    int ok = 1;
    if (!cmd) return;

    if (!strcmp(cmd, "RUN"))         { S.run = 1; if (S.mode == MODE_SINGLE) S.mode = MODE_AUTO; }
    else if (!strcmp(cmd, "STOP"))   { S.run = 0; state = "STOP"; }
    else if (!strcmp(cmd, "SINGLE")) { S.mode = MODE_SINGLE; S.run = 1; state = "WAIT"; }
    else if (!strcmp(cmd, "MODE") && a1) {
        if (!strcmp(a1, "AUTO")) S.mode = MODE_AUTO;
        else if (!strncmp(a1, "NORM", 4)) S.mode = MODE_NORM;
        else ok = 0;
    }
    else if (!strcmp(cmd, "TRIG") && a1 && a1[0] >= '1' && a1[0] <= '4') {
        S.trig_ch = (uint8_t)(a1[0] - '1');
        if (a2) S.edge = (a2[0] == 'F');
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "LEVEL") && a1) {
        if (!strcmp(a1, "AUTO")) S.level_mv = -1;
        else { int v = atoi(a1); S.level_mv = (int16_t)(v < 0 ? 0 : v > VREF_MV ? VREF_MV : v); }
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
        if (idx < 0 || idx >= TB_COUNT) ok = 0;
        else { S.tb = (uint8_t)idx; tb_dirty = 1; }
    }
    else if (!strcmp(cmd, "TBLIST")) {
        for (int i = 0; i < TB_COUNT; i++) printf("#TB %d %s\r\n", i, acq_tb(i)->label);
    }
    else if (!strcmp(cmd, "CH") && a1 && a2 && a1[0] >= '1' && a1[0] <= '4') {
        S.on[a1[0] - '1'] = !strcmp(a2, "ON");
        view_dirty = 1;
    }
    else if (!strcmp(cmd, "OVS") && a1) { S.ovs = !strcmp(a1, "ON"); tb_dirty = 1; }
    else if (!strcmp(cmd, "GEN") && a1) {
        int sh = !strcmp(a1, "SIN") ? GEN_SIN : !strcmp(a1, "SQR") ? GEN_SQR :
                 !strcmp(a1, "TRI") ? GEN_TRI : !strcmp(a1, "OFF") ? GEN_OFF : -1;
        uint32_t f = a2 ? parse_hz(a2) : (gen_f_mhz() / 1000u ? gen_f_mhz() / 1000u : 10000u);
        if (sh < 0) ok = 0; else gen_set(sh, f);
    }
    else if (!strcmp(cmd, "HELP")) help();
    else if (strcmp(cmd, "STAT")) ok = 0;

    printf(ok ? "OK\r\n" : "ERR nieznana komenda (HELP)\r\n");
    status_dirty = 1;
    send_status();
}

/* ---------------------------------------------------------------- start i pętla */

void app_init(void)
{
    dwt_init();
    printf("\r\n=== Rejestrator 4ch, krok A6+A7 (wyzwalanie, tryby, podstawa czasu), SYSCLK = %lu Hz ===\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("Wpisz HELP. Sterowanie z PC: tools/remote.py\r\n");
    lcd_init();
    gfx_init();
    for (int c = 0; c < NCH; c++) gfx_tr[c].color = ch_color[c];
    draw_static();

    gen_set(GEN_SIN, 10000);
    acq_init();
    link_init();
    ui_init();
    t_sec = HAL_GetTick() + 1000;
}

void app_poll(void)
{
    gfx_poll();
    acq_poll();

    if (ui_poll() == EV_RUNSTOP) {                        /* B1: RUN <-> STOP */
        S.run = !S.run;
        if (S.mode == MODE_SINGLE && S.run) state = "WAIT";
        if (!S.run) state = "STOP";
        status_dirty = 1;
    }
    char ln[96];
    while (link_getline(ln, sizeof ln)) command(ln);

    if (tb_dirty) {                                       /* zmiana podstawy czasu */
        acq_abort();
        acq_set_timebase(S.tb, S.ovs);
        tb_dirty = 0;
        have_block = 0;
        half_min = 0xFFFFFFFFu;
        status_dirty = 1;
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

    if (S.run && !acq_busy()) acq_arm();                  /* następny blok zbiera się w tle */

    if (gfx_busy()) return;                               /* SPI zajęte - reszta między ramkami */

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
