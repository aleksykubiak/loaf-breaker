/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the pattern sequencer (SEQ), the drum lanes and REC. (Part of app.c's unity: included there in order.) */

/* ---- the pattern sequencer (SEQ) and the drum lanes: the white keys on the SEQ, KICK and SNARE / HAT pages, REC */
enum { SL_BREAK1, SL_BREAK2, SL_KICK, SL_KICK2, SL_KICK3, SL_KICK4, SL_SNARE, SL_SNARE2, SL_SNARE3, SL_SNARE4, SL_HAT,
       SL_HAT2, SL_HAT3, SL_HAT4, SL_CLAP, SL_CLAP2, SL_CLAP3, SL_CLAP4, NSEQL };   /* (0.9.30, FLEXOR: every drum four bars) */   /* (LOAF BREAKER, 0.209: the MELODY's two gone) */                /* GLOUPI BREAKS: the kick's four bars and the snare's two, a lane each;
                                          * LOAF BREAKER: the hat's two, the clap's two */
static const char *const SEQ_LANE_NAME[NSEQL] = {"STEPS 1-16", "STEPS 17-32", "KICK 1-16", "KICK 17-32", "KICK 33-48",
                                                 "KICK 49-64", "SNARE 1-16", "SNARE 17-32", "SNARE 33-48", "SNARE 49-64",
                                                 "HAT 1-16", "HAT 17-32", "HAT 33-48", "HAT 49-64", "CLAP 1-16",
                                                 "CLAP 17-32", "CLAP 33-48", "CLAP 49-64"};
#define SQ_BRK_MIN 18                    /* LOAF BREAKER (0.242, FLEXOR): a break row's pitch, the rows' average (the grid's
                                          * height over its rows), between this (its number inside its box) and 24 */
#define SQ_BRK_MAX 24
#define SQ_H 148                         /* the picture's rows (screen 44..191) */
#define SQ_X 30                          /* the steps begin (the names before) */
static uint32_t text_n(const char *t) { uint32_t n = 0u; while (t[n]) n++; return n; }   /* (no libc's strlen on the FM-1) */
static const char *const DRUM_NAME[NDRUMS] = {"KICK", "SNARE", "HAT", "CLAP"};
static const char *const DRUM_TAG[NDRUMS] = {"KICK", "SN", "HH", "CP"};   /* (EDIT's tabs) */
static const uint8_t SL_DR[NSEQL] = {0u, 0u, DR_KICK, DR_KICK, DR_KICK, DR_KICK, DR_SNARE, DR_SNARE, DR_SNARE, DR_SNARE,
                                     DR_HAT, DR_HAT, DR_HAT, DR_HAT, DR_CLAP, DR_CLAP, DR_CLAP, DR_CLAP};   /* a drum
                                          * lane's drum, and its bar */
static const uint8_t SL_BAR[NSEQL] = {0u, 1u, 0u, 1u, 2u, 3u, 0u, 1u, 2u, 3u, 0u, 1u, 2u, 3u, 0u, 1u, 2u, 3u};
static void seq_value(int i, char *t);
enum { UNDO_RANDOM = 1, UNDO_CLEAR, UNDO_LANE };   /* (UNDO_LANE + the knob) */
static void undo_mark(uint32_t what);
static pat_t *pat_now(void) { return &pats.p[set.pattern % NPAT]; }

/* undo: a copy of the pattern taken before a change that wipes (SEL + ARP, SEL + REC, the drum lanes' FILL, CLEAR /
 * 4X4, COPY); a burst of the same change within 1.5 s is one. SEL + SAVE swaps it back in, and again swaps it out (redo) */
static pat_t undo_pat;
static uint8_t undo_kn[13];              /* (LOAF BREAKER: its kick notes kept apart, pats.kn; its drums' HITS) */
static uint8_t undo_steps[STEPB_N];
static uint16_t undo_dx[6];              /* (0.9.30: its drums' bars 3 and 4) */
static uint8_t undo_idx = 0xFFu, undo_what;
static uint32_t undo_t;
static void undo_mark(uint32_t what)     /* what: which change (UNDO_*), a burst of the same one is one */
{
    uint32_t now = plat_ms(), k;
    if (undo_idx != set.pattern % NPAT || what != undo_what || now - undo_t > 1500u) {
        undo_what = (uint8_t)what;
        undo_pat = *pat_now();
        undo_idx = (uint8_t)(set.pattern % NPAT);
        for (k = 0; k < 13u; k++)
            undo_kn[k] = pats.kn[undo_idx][k];
        for (k = 0; k < STEPB_N; k++)
            undo_steps[k] = steps.b[undo_idx][k];
        for (k = 0; k < 6u; k++)
            undo_dx[k] = dx.b[set.pgrp % NGRP][undo_idx][k];
    }
    undo_t = now;
}
static void undo_swap(void)
{
    pat_t t;
    char n[6] = "P";
    if (undo_idx >= NPAT) {
        app_say("UNDO", "NOTHING YET");
        return;
    }
    t = pats.p[undo_idx];
    pats.p[undo_idx] = undo_pat;
    undo_pat = t;
    {
        uint32_t k;
        for (k = 0; k < 13u; k++) {
            uint8_t b = pats.kn[undo_idx][k];
            pats.kn[undo_idx][k] = undo_kn[k];
            undo_kn[k] = b;
        }
        for (k = 0; k < STEPB_N; k++) {
            uint8_t b = steps.b[undo_idx][k];
            steps.b[undo_idx][k] = undo_steps[k];
            undo_steps[k] = b;
        }
        for (k = 0; k < 6u; k++) {
            uint16_t b = dx.b[set.pgrp % NGRP][undo_idx][k];
            dx.b[set.pgrp % NGRP][undo_idx][k] = undo_dx[k];
            undo_dx[k] = b;
        }
    }
    undo_t = 0u;                         /* (the next change takes a fresh copy) */
    if (undo_idx == set.pattern % NPAT)
        pat_apply();
    set_dirty = 1u;
    int_text((int32_t)(set.pgrp % NGRP * NPAT + undo_idx) + 1, n + 1);
    app_say("UNDO", n);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static uint32_t pat_len(void)            /* the steps the engine plays of a pattern: the break's (2 bars: 32) */
{
    const brk_break_t *b = brk_get_break();
    uint32_t n = (b ? b->bars : 2u) * BRK_STEPS_PER_BAR;
    return n < PAT_STEPS ? n : PAT_STEPS;
}
/* REC: the step a key or a note lands on (the one playing, or the next past its middle) */
static uint32_t rec_at(uint32_t len, int bar)
{
    brk_status_t st;
    int32_t k;
    brk_status(&st);
    k = bar ? st.bar16 : st.step;
    if (k < 0)
        k = 0;
    return ((uint32_t)k + (st.phase > 0.5f ? 1u : 0u)) % len;
}
static int playing_now(void)
{
    brk_status_t st;
    brk_status(&st);
    return st.playing;
}
static uint8_t rec_step;                 /* LOAF BREAKER: REC stopped, step by step: the step the next key writes */
static void rec_slice(uint32_t s)
{
    int live = playing_now();
    uint32_t n = pat_len(), k = live ? rec_at(n, 0) : rec_step % n;   /* (stopped: the step shown, then the next) */
    pat_now()->st[k][0] = (uint8_t)(s + 2u);
    pat_step_out(k);
    set_dirty = 1u;
    if (!live) {
        char t[12], u[4];
        rec_step = (uint8_t)((k + 1u) % n);
        int_text((int32_t)rec_step + 1, t);
        int_text((int32_t)n, u);
        text_cat(t, " / ");
        text_cat(t, u);
        app_say("REC STEP", t);
        ui.redraw |= RG_BLOCKS;
    }
}
/* LOAF BREAKER: REC stopped, step by step: OCT+ a REST (the step left as it is, on to the next), OCT- a step back */
static void rec_move(int32_t d)
{
    char t[12], u[4];
    uint32_t n = pat_len();
    rec_step = (uint8_t)((rec_step % n + (d > 0 ? 1u : n - 1u)) % n);
    int_text((int32_t)rec_step + 1, t);
    int_text((int32_t)n, u);
    text_cat(t, " / ");
    text_cat(t, u);
    app_say(d > 0 ? "REST" : "BACK", t);
    ui.redraw |= RG_BLOCKS;
}
static void midi_slice(int s, int on)    /* a note 48..63: as the white key, recorded with REC */
{
    if (on && ui.rec)
        rec_slice((uint32_t)s);
    brk_key(s, on);
}
static void midi_bass(uint32_t note, int on)   /* a MIDI note below 48: the BASS (its keys' way: last note first) */
{
    if (on) {
        bl_live_note = (uint8_t)note;
        bl_live_held = 1u;
        bl_live_new = 1u;
    } else if (bl_live_note == note) {
        bl_live_held = 0u;
        bl_live_up = 1u;
    }
}
/* K1-K4 on a drum lane (SEQ): FILL n hits spread evenly (Euclidean, from the downbeat), KICK FLT (LOAF BREAKER, in
 * place of ROTATE: the kick's DJ filter, HOME's K3's),
 * CLEAR (left) or four on the floor (right), LENGTH (GLOUPI BREAKS, in place of COPY: the kick 16, 32 or 64 steps, the
 * snare 16 or 32) */
/* LOAF BREAKER (FLEXOR, 11 Nov 2026): BEGINNER's drum lanes as EXPERT's (0.229: K1 FILL again, the BEAT presets of
 * 0.224-0.228 gone, FLEXOR: "a sequencer without them"), but no step menu (a step held: nothing) and K4 LENGTH the
 * kick's alone (0.227: 16, 32, 64; a drum lane says EXPERT) */
/* LOAF BREAKER: SEQ held, the lane shown: + a white key 1..4, its bar copied onto that one (its steps' HITS, COND,
 * NUDGE and notes with it); + K1, the whole lane (every bar of its drum, or the break's 32 steps) a step later or
 * earlier, round; + REC, every drum lane of the pattern cleared, the break kept. SEL + SAVE undoes each */
/* SEL + REC: 0 the whole pattern (elsewhere), 1 on SEQ the lane shown; LOAF BREAKER: 2 REC held on SEQ, the break's
 * steps, both halves (1-16, 17-32), the drums kept */
static void seq_clear(int mode)
{
    pat_t *p = pat_now();
    uint32_t k;
    undo_mark(UNDO_CLEAR);
    (void)mode;
    for (k = 0; k < PAT_STEPS; k++) {    /* (the break's steps: follow, as they start; the drums' lanes: gone) */
        p->st[k][0] = 1u;
        p->st[k][1] = 0u;
        p->st[k][2] = 0u;
    }
    pat_apply();
    set_dirty = 1u;
    app_say("CLEARED", "PATTERN");
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static const char *grp_name(uint32_t g)  /* LOAF BREAKER: a group of 16 as the screen says it */
{
    static const char *const N[NGRP] = {"P1-16", "P17-32", "P33-48", "P49-64"};
    return N[g % NGRP];
}
static int grp_blocked(uint32_t g)       /* LOAF BREAKER: pattern g (0..63) can't come in now: another group, this
                                          * one's changes not saved, playing (said) */
{
    if (g / NPAT % NGRP == set.pgrp || !grp_changed() || !playing_now())
        return 0;
    app_say("SAVE OR STOP", grp_name(set.pgrp));
    return 1;
}
static int pat_go(uint32_t g)            /* LOAF BREAKER: pattern g (0..63) the one playing (its group in first: this
                                          * one's changes saved before, stopped); 0 refused */
{
    uint32_t grp = g / NPAT % NGRP;
    if (grp != set.pgrp) {
        if (grp_blocked(g))
            return 0;
        if (grp_changed() && !grp_save()) {
            app_say("SAVE", "FAILED");
            return 0;
        }
        grp_load(grp);
        undo_idx = 0xFFu;                /* (an undo was the other group's) */
    }
    set.pattern = (uint8_t)(g % NPAT);
    return 1;
}
static void pattern_pick(int32_t d)      /* PRESETS: the next or previous pattern (playing: at the loop's end) */
{
    int32_t cur = pat_next >= 0 ? pat_next : (int32_t)pat_cur();
    int32_t want = (cur + (d > 0 ? 1 : (int32_t)NPAT_ALL - 1)) % (int32_t)NPAT_ALL;
    if (grp_blocked((uint32_t)want))
        return;
    if (playing_now())
        pat_next = (int8_t)want;
    else {
        if (!pat_go((uint32_t)want))
            return;
        pat_apply();
        set_dirty = 1u;
    }
    say_int(playing_now() ? "NEXT PATTERN" : "PATTERN", want + 1, "");
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
}
static uint32_t track_keys_lit(void);
static int8_t lit_head = -1;             /* (0.9.57: the step playing on a lane's page, 0..15, -1 none: leds() shows it
                                          * at the middle level; it was the step's light turned over) */
static uint32_t keys_lit(const brk_status_t *st)   /* the white keys' lights on a lane's page (ENV: its own) */
{
    lit_head = -1;
    if (ui.page == PG_TRACK)
        return track_keys_lit();
    if (ui.page == PG_OVL)               /* (0.9.27, FLEXOR: the OVERLAY page, its effects on lit: held, latched, the
                                          * ARP key's) */
        return ui.opage ? 0u : ovl_held | ovl_latched | ovl_pad_bits();
    if (ui.page == PG_GLOBAL)            /* (LOAF BREAKER: GLO EFFECTS: the effects on) */
        return ui.gpage == 3u ? set.sfx_on : 0u;
    uint32_t m = 0u, i;
    for (i = 0; i < 16u; i++)            /* SEQ's break steps: the ones with a slice of their own */
        if (pat_now()->st[ui.seq_lane * 16u + i][0] >= 2u)
            m |= 1u << i;
    if (st->playing && st->step >= 0 && (uint32_t)st->step / 16u == ui.seq_lane)
        lit_head = (int8_t)(st->step % 16);
    return m;
}
/* LOAF BREAKER: the SEQ page, everything at once. Under the tabs (y 44..191, drawn in three bands), a row of 16 steps
 * each: the break's half the keys edit, when they edit it (each step its slice, LOOP a dot, off a dash; the one edited
 * outlined; gone while a drum lane is picked), then every drum
 * over the length it has (the kick's 1, 2 or 4 bars, the snare's, hat's and clap's 1 or 2), named by its letters
 * ("K", "SN"; its other bars "2", "3", "4"). The rows share the height: fewer rows, bigger steps. The step playing
 * white; the lane the keys edit named lit, a line under it */
