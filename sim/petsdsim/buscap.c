/*
 * buscap.c - automatische VCD-Mitschnitte des IEEE-488-Busses (GTKWave)
 *
 * Eine fallende ATN-Flanke nach einer Ruhephase startet einen Mitschnitt
 * (mit kurzem Vorlauf aus einem Ringpuffer). Ist der Bus idle_ms lang frei
 * und ohne Flanke, endet er, ebenso sofort nach einem CLOSE. Der Dateiname entsteht aus Uhrzeit, Geraet und
 * dem mitgelesenen Vorgang, z. B.
 *   20261123-110612-345_d11_LOAD_spiel.vcd
 *   20261123-110700-012_d11_DIR.vcd
 *   20261123-110730-500_d11_CMD_S0-alt.vcd
 *   20261123-110800-001_d11_TALK2.vcd       (ohne OPEN, z. B. GET#)
 *
 * Pegel in der Datei sind elektrisch: 1 = high (frei), 0 = low (gezogen).
 * Datenbytes sind als Wert angegeben (low = 1, wie auf dem Bus gemeint).
 * Zeit: Simulationszeit in ns.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "buscap.h"

#define L_ATN  0x01
#define L_DAV  0x02
#define L_NRFD 0x04
#define L_NDAC 0x08
#define L_EOI  0x10
#define L_IFC  0x40

#define PRE_PS   1000000000ULL      /* Vorlauf 1 ms */
#define RING     512

typedef struct {
    uint64_t t;
    uint8_t pp, pd, ap, ad;         /* PET pull/data, AVR pull/data */
    uint8_t te, cs, isr;
} snap_t;

static const char *cap_dir;
static uint64_t idle_ps = 250000000000ULL;
static int on = 0;

static snap_t ring[RING];
static unsigned ring_n = 0, ring_head = 0;
static snap_t last;
static int have_last = 0;

static FILE *f;
static char tmpname[512], stamp[64];
static uint64_t last_act;
static snap_t written;
static int written_valid;

/* Mitlesen fuer den Dateinamen */
static int dev = -1, listening = 0, talk_sa = -1, open_sa = -1, cmd_sa = -1;
static int collecting = 0, name_done = 0, closed = 0;
static char fname[48];
static int fname_n = 0;
static char decoded[64];

void buscap_init(const char *dir, double idle_ms)
{
    cap_dir = dir;
    if (idle_ms > 0) {
        idle_ps = (uint64_t)(idle_ms * 1e9);
    }
    on = 1;
}

/* Var-Kennungen */
enum {
    V_P_ATN, V_P_DAV, V_P_NRFD, V_P_NDAC, V_P_EOI, V_P_IFC,
    V_A_DAV, V_A_NRFD, V_A_NDAC, V_A_EOI, V_A_TE,
    V_B_ATN, V_B_DAV, V_B_NRFD, V_B_NDAC, V_B_EOI,
    V_P_DATA, V_A_DATA, V_B_DATA, V_CS, V_ISR, V_TXT, V_NUM
};
static const char *vid[V_NUM] = {
    "!", "\"", "#", "$", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/", "0",
    "1", "2", "3", "4", "5", "6"
};

static void bits(FILE *o, uint8_t v, const char *id)
{
    int i;
    fputc('b', o);
    for (i = 7; i >= 0; i--) {
        fputc((v >> i) & 1 ? '1' : '0', o);
    }
    fprintf(o, " %s\n", id);
}

static void header(void)
{
    fprintf(f, "$date %s $end\n$version petsdsim buscap $end\n$timescale 1ns $end\n", stamp);
    fprintf(f, "$scope module pet $end\n");
    fprintf(f, "$var wire 1 %s ATN $end\n$var wire 1 %s DAV $end\n$var wire 1 %s NRFD $end\n",
            vid[V_P_ATN], vid[V_P_DAV], vid[V_P_NRFD]);
    fprintf(f, "$var wire 1 %s NDAC $end\n$var wire 1 %s EOI $end\n$var wire 1 %s IFC $end\n",
            vid[V_P_NDAC], vid[V_P_EOI], vid[V_P_IFC]);
    fprintf(f, "$var wire 8 %s DATA $end\n$upscope $end\n", vid[V_P_DATA]);
    fprintf(f, "$scope module petsd $end\n");
    fprintf(f, "$var wire 1 %s DAV $end\n$var wire 1 %s NRFD $end\n$var wire 1 %s NDAC $end\n",
            vid[V_A_DAV], vid[V_A_NRFD], vid[V_A_NDAC]);
    fprintf(f, "$var wire 1 %s EOI $end\n$var wire 1 %s TE $end\n$var wire 8 %s DATA $end\n",
            vid[V_A_EOI], vid[V_A_TE], vid[V_A_DATA]);
    fprintf(f, "$var wire 1 %s SD_CS $end\n$var wire 1 %s ATN_ISR $end\n$upscope $end\n",
            vid[V_CS], vid[V_ISR]);
    fprintf(f, "$scope module bus $end\n");
    fprintf(f, "$var wire 1 %s ATN $end\n$var wire 1 %s DAV $end\n$var wire 1 %s NRFD $end\n",
            vid[V_B_ATN], vid[V_B_DAV], vid[V_B_NRFD]);
    fprintf(f, "$var wire 1 %s NDAC $end\n$var wire 1 %s EOI $end\n$var wire 8 %s DATA $end\n",
            vid[V_B_NDAC], vid[V_B_EOI], vid[V_B_DATA]);
    fprintf(f, "$var string 1 %s BYTE $end\n$upscope $end\n$enddefinitions $end\n", vid[V_TXT]);
}

static void lvl(int force, uint8_t a, uint8_t b, uint8_t mask, int v)
{
    if (force || ((a ^ b) & mask)) {
        fprintf(f, "%d%s\n", (b & mask) ? 0 : 1, vid[v]);
    }
}

/* Zustand s in die Datei (nur Aenderungen gegenueber written) */
static void emit(const snap_t *s, const char *txt)
{
    int fo = !written_valid;
    const snap_t *w = &written;
    uint8_t bw = (uint8_t)(w->pp | w->ap), bs = (uint8_t)(s->pp | s->ap);
    fprintf(f, "#%llu\n", (unsigned long long)(s->t / 1000));
    lvl(fo, w->pp, s->pp, L_ATN, V_P_ATN);
    lvl(fo, w->pp, s->pp, L_DAV, V_P_DAV);
    lvl(fo, w->pp, s->pp, L_NRFD, V_P_NRFD);
    lvl(fo, w->pp, s->pp, L_NDAC, V_P_NDAC);
    lvl(fo, w->pp, s->pp, L_EOI, V_P_EOI);
    lvl(fo, w->pp, s->pp, L_IFC, V_P_IFC);
    lvl(fo, w->ap, s->ap, L_DAV, V_A_DAV);
    lvl(fo, w->ap, s->ap, L_NRFD, V_A_NRFD);
    lvl(fo, w->ap, s->ap, L_NDAC, V_A_NDAC);
    lvl(fo, w->ap, s->ap, L_EOI, V_A_EOI);
    lvl(fo, bw, bs, L_ATN, V_B_ATN);
    lvl(fo, bw, bs, L_DAV, V_B_DAV);
    lvl(fo, bw, bs, L_NRFD, V_B_NRFD);
    lvl(fo, bw, bs, L_NDAC, V_B_NDAC);
    lvl(fo, bw, bs, L_EOI, V_B_EOI);
    if (fo || w->te != s->te) fprintf(f, "%d%s\n", s->te, vid[V_A_TE]);
    if (fo || w->cs != s->cs) fprintf(f, "%d%s\n", s->cs, vid[V_CS]);
    if (fo || w->isr != s->isr) fprintf(f, "%d%s\n", s->isr, vid[V_ISR]);
    if (fo || w->pd != s->pd) bits(f, s->pd, vid[V_P_DATA]);
    if (fo || w->ad != s->ad) bits(f, s->ad, vid[V_A_DATA]);
    if (fo || (uint8_t)(w->pd | w->ad) != (uint8_t)(s->pd | s->ad)) {
        bits(f, (uint8_t)(s->pd | s->ad), vid[V_B_DATA]);
    }
    if (fo) fprintf(f, "s- %s\n", vid[V_TXT]);
    if (txt) fprintf(f, "s%s %s\n", txt, vid[V_TXT]);
    written = *s;
    written_valid = 1;
}

static void reset_names(void)
{
    dev = -1; listening = 0; talk_sa = -1; open_sa = -1; cmd_sa = -1;
    collecting = 0; name_done = 0; closed = 0; fname_n = 0; fname[0] = 0;
}

/* Ein uebertragenes Byte mitlesen; liefert Klartext fuer die BYTE-Spur */
static const char *decode(uint8_t b, int atn, int eoi)
{
    if (atn) {
        collecting = 0;
        if (b >= 0x20 && b <= 0x3e) {
            if (dev < 0) dev = b & 0x1f;
            listening = 1;
            snprintf(decoded, sizeof decoded, "LISTEN_%d", b & 0x1f);
        } else if (b == 0x3f) {
            listening = 0;
            if (fname_n) name_done = 1;
            snprintf(decoded, sizeof decoded, "UNLISTEN");
        } else if (b >= 0x40 && b <= 0x5e) {
            if (dev < 0) dev = b & 0x1f;
            listening = 0;
            snprintf(decoded, sizeof decoded, "TALK_%d", b & 0x1f);
        } else if (b == 0x5f) {
            snprintf(decoded, sizeof decoded, "UNTALK");
        } else if ((b & 0xf0) == 0x60) {
            if (listening) {
                if ((b & 0x0f) == 15 && cmd_sa < 0 && !name_done) {
                    cmd_sa = 15;
                    collecting = 1;
                }
            } else if (talk_sa < 0) {
                talk_sa = b & 0x0f;
            }
            snprintf(decoded, sizeof decoded, "SECOND_%d", b & 0x0f);
        } else if ((b & 0xf0) == 0xe0) {
            closed = 1;
            snprintf(decoded, sizeof decoded, "CLOSE_%d", b & 0x0f);
        } else if ((b & 0xf0) == 0xf0) {
            if (open_sa < 0 && !name_done) {
                open_sa = b & 0x0f;
                fname_n = 0;
                collecting = 1;
            }
            snprintf(decoded, sizeof decoded, "OPEN_%d", b & 0x0f);
        } else {
            snprintf(decoded, sizeof decoded, "ATN_%02X", b);
        }
        return decoded;
    }
    if (collecting && !name_done && fname_n < (int)sizeof fname - 1) {
        /* PETSCII wie im Kleinbuchstabenmodus des PET */
        char c = (char)b;
        if (b >= 0x41 && b <= 0x5a) c = (char)(b + 0x20);
        else if (b >= 0xc1 && b <= 0xda) c = (char)(b - 0x80);
        fname[fname_n++] = c;
        fname[fname_n] = 0;
        if (eoi) name_done = 1;
    }
    if (b >= 0x20 && b < 0x7f && b != ' ') {
        snprintf(decoded, sizeof decoded, "%02X_'%c'%s", b, b, eoi ? "_EOI" : "");
    } else {
        snprintf(decoded, sizeof decoded, "%02X%s", b, eoi ? "_EOI" : "");
    }
    return decoded;
}

static void clean(char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              c == '-' || c == '.' || c == '$' || c == '+')) {
            *s = '_';
        }
    }
}

static void start(uint64_t t)
{
    struct timeval tv;
    struct tm tm;
    time_t sec;
    unsigned i, n;
    gettimeofday(&tv, NULL);
    sec = tv.tv_sec;
    localtime_r(&sec, &tm);
    snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d-%03d", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
    snprintf(tmpname, sizeof tmpname, "%s/%s_laeuft.vcd", cap_dir, stamp);
    f = fopen(tmpname, "w");
    if (!f) {
        perror(tmpname);
        on = 0;
        return;
    }
    reset_names();
    written_valid = 0;
    header();
    /* Vorlauf: Zustaende der letzten PRE_PS aus dem Ring */
    n = ring_n;
    for (i = 0; i < n; i++) {
        const snap_t *s = &ring[(ring_head + RING - n + i) % RING];
        if (s->t + PRE_PS >= t || i == n - 1) {
            emit(s, NULL);
        }
    }
    last_act = t;
}

static void stop(void)
{
    char op[96], name[640];
    if (!f) return;
    fclose(f);
    f = NULL;
    clean(fname);
    if (open_sa == 0 && fname[0] == '$') {
        snprintf(op, sizeof op, "DIR%s%s", fname[1] ? "_" : "", fname + 1);
    } else if (open_sa == 0) {
        snprintf(op, sizeof op, "LOAD_%s", fname);
    } else if (open_sa == 1) {
        snprintf(op, sizeof op, "SAVE_%s", fname);
    } else if (open_sa == 15 || (open_sa < 0 && cmd_sa == 15)) {
        snprintf(op, sizeof op, "CMD%s%s", fname[0] ? "_" : "", fname);
    } else if (open_sa >= 0) {
        snprintf(op, sizeof op, "OPEN%d%s%s", open_sa, fname[0] ? "_" : "", fname);
    } else if (talk_sa >= 0) {
        snprintf(op, sizeof op, "TALK%d", talk_sa);
    } else {
        snprintf(op, sizeof op, "BUS");
    }
    if (dev >= 0) {
        snprintf(name, sizeof name, "%s/%s_d%d_%s.vcd", cap_dir, stamp, dev, op);
    } else {
        snprintf(name, sizeof name, "%s/%s_%s.vcd", cap_dir, stamp, op);
    }
    if (rename(tmpname, name) != 0) {
        perror(name);
    } else {
        fprintf(stderr, "buscap: %s\n", name);
    }
}

void buscap_state(uint64_t t, uint8_t pet_pull, uint8_t pet_data, uint8_t avr_pull, uint8_t avr_data,
                  int te, int sd_cs, int isr)
{
    snap_t s;
    const char *txt = NULL;
    uint8_t bus_old, bus_new;
    if (!on) return;
    s.t = t; s.pp = pet_pull; s.pd = pet_data; s.ap = avr_pull; s.ad = avr_data;
    s.te = (uint8_t)(te != 0); s.cs = (uint8_t)(sd_cs != 0); s.isr = (uint8_t)(isr != 0);
    if (have_last && !memcmp(&s.pp, &last.pp, 7)) {
        return;
    }
    bus_old = have_last ? (uint8_t)(last.pp | last.ap) : 0;
    bus_new = (uint8_t)(s.pp | s.ap);

    /* Ausloesen: ATN faellt, kein Mitschnitt aktiv */
    if (!f && (bus_new & L_ATN) && !(bus_old & L_ATN)) {
        start(t);
    }
    if (f) {
        /* Byte gilt, wenn DAV auf dem Bus faellt */
        if ((bus_new & L_DAV) && !(bus_old & L_DAV)) {
            txt = decode((uint8_t)(s.pd | s.ad), bus_new & L_ATN, bus_new & L_EOI);
        }
        emit(&s, txt);
        /* Nach CLOSE endet der Vorgang mit dem ATN-Zyklus: sofort abschliessen,
           damit direkt folgende Befehle eine eigene Datei bekommen */
        if (closed && !(bus_new & 0x7f)) {
            stop();
            last = s;
            have_last = 1;
            ring[ring_head] = s;
            ring_head = (ring_head + 1) % RING;
            if (ring_n < RING) ring_n++;
            return;
        }
        if (!have_last || (uint8_t)(last.pp | last.ap) != bus_new ||
            (uint8_t)(last.pd | last.ad) != (uint8_t)(s.pd | s.ad)) {
            last_act = t;
        }
    }
    ring[ring_head] = s;
    ring_head = (ring_head + 1) % RING;
    if (ring_n < RING) ring_n++;
    last = s;
    have_last = 1;
}

void buscap_poll(uint64_t t)
{
    if (!f) return;
    /* Ende: Bus frei (nur REN darf stehen) und idle lang keine Flanke */
    if (((last.pp | last.ap) & 0x7f) == 0 && t > last_act + idle_ps) {
        stop();
    }
}

void buscap_close(void)
{
    stop();
}
