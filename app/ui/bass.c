/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS's two pages (the lines and their engine: bline.c, dsp/bass.c). SEQ and EDIT are the bass's now; the
 * pattern's steps and the KICK menu stay where they were reached before (the pattern: PRESETS; the KICK menu, the
 * drums and the break: SEL + EDIT).
 *   BASS SEQ (PG_BSEQ, SEQ): eight lines of two bars. ALGORITHM the line (playing: at the next bar), SELECT the step,
 *     K1 its note, K2 ACCENT / SLIDE, K3 the line's TRANSPOSE, K4 its LEVEL. The keys are a keyboard (F to G, OCT- /
 *     OCT+ its octave): REC, playing, records them into the line at the nearest 16th (a key held: a long note);
 *     stopped, REC writes them step by step from the step selected. SEL + REC clears the line, SEQ held + white key
 *     1-8 picks a line (from any page: back there after), SEQ held + K1 turns the line a step round, SEL + SEQ copies
 *     the line, then on another pastes it.
 *   BASS EDIT (PG_BEDIT, EDIT): the sound of the line selected. SELECT its parts (OSC, FILTER, ENV), K1-K4 theirs,
 *     PRESETS a built-in sound, ALGORITHM the line; the keys play it. */

#define BS_KEYS 0x7FFFFFFu               /* the 27 keys' bits: each its semitone from the lowest, an F */
static int32_t tab(int32_t x, const char *s, int on);   /* (main.c) */
enum { BT_OSC, BT_FILT, BT_ENV, BT_N };
static const char *const BT_NAME[BT_N] = {"OSC", "FILTER", "ENV"};
static const uint8_t BT_PAR[BT_N][4] = {{BS_WAVE, BS_SUB, BS_DRIVE, 0xFFu},   /* (0xFF: the line's LEVEL) */
                                        {BS_CUTOFF, BS_RESO, BS_ENV, BS_KEYTRK},
                                        {BS_DECAY, BS_SUSTAIN, BS_ACCENT, BS_GLIDE}};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static struct {
    uint8_t cur;                         /* SEQ: the step selected, 0..31 */
    uint8_t tab;                         /* EDIT: its part, BT_* */
    uint8_t rec;                         /* REC on (the bass's own: the pattern's REC is HOME's) */
    uint8_t key;                         /* the key sounding, its bit + 1 (0: none) */
    uint8_t note;                        /* its note as played */
    int8_t wrote;                        /* recording: the step its key wrote last (-1: none) */
    int8_t clip;                         /* SEL + SEQ: the line copied (-1: none) */
    uint8_t seq_down, seq_used, seq_back, seq_from;   /* SEQ held: what it did, where it came from */
    uint32_t steps_seen, frame_t;
} bs = {0u, 0u, 0u, 0u, 0u, -1, -1, 0u, 0u, 0u, 0u, 0u, 0u};

static uint32_t bs_len(const char *t)   /* (no libc on the FM-1) */
{
    uint32_t n = 0u;
    while (t[n])
        n++;
    return n;
}

static int bs_page(void) { return ui.page == PG_BSEQ || ui.page == PG_BEDIT; }
static bl_line_t *bs_line(void) { return &bls.l[bls.sel % BL_N]; }
static void bs_dirty(void) { set_dirty = 1u; }

static void note_text(uint32_t n, char *t)   /* 45 -> "A2" */
{
    t[0] = 0;
    if (!n) {
        text_cat(t, "---");
        return;
    }
    text_cat(t, NOTE_NAME[n % 12u]);
    int_text((int32_t)(n / 12u) - 1, t + bs_len(t));
}

static void bs_open(uint32_t pg)
{
    ui.page = (uint8_t)pg;
    ui.redraw |= RG_HEAD | RG_WAVE | RG_BLOCKS | RG_FOOT;
    page_say();
}

static void bs_pick(uint32_t n)          /* a line selected: playing, at the next bar; stopped, at once */
{
    char t[16] = "LINE ";
    n %= BL_N;
    bls.sel = (uint8_t)n;
    int_text((int32_t)n + 1, t + 5);
    if (!playing_now()) {
        bl_play = (uint8_t)n;
        bl_sound_changed();
        app_say(t, bl_empty(n) ? "EMPTY: REC IT" : BASS_PRESET_NAME[bls.l[n].preset % BASS_PRESETS]);
    } else
        app_say(t, bl_play == n ? "PLAYING" : "AT THE NEXT BAR");
    bs_dirty();
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
}

/* recording, playing: the step nearest now (the one playing, or the next past its half) */
static uint32_t bs_rec_step(void)
{
    brk_status_t st;
    brk_status(&st);
    return (bl_pos + (st.phase > 0.5f ? 1u : 0u)) % BL_STEPS;
}

static void bs_write(uint32_t s, uint32_t note, uint32_t fl)
{
    bl_line_t *l = bs_line();
    int32_t v = (int32_t)note - l->trans;   /* (kept as heard: the TRANSPOSE taken off) */
    l->st[s % BL_STEPS].note = (uint8_t)(v < 12 ? 12 : v > 108 ? 108 : v);
    l->st[s % BL_STEPS].fl = (uint8_t)fl;
    bs_dirty();
    ui.redraw |= RG_BLOCKS;
}

static void bs_note_on(uint32_t bit)
{
    uint32_t note = 12u * (bls.oct + 1u) + 5u + bit;   /* (the lowest key an F: F1 29 at octave 1) */
    bs.key = (uint8_t)(bit + 1u);
    bs.note = (uint8_t)note;
    tut_lane++;                          /* (the TUTORIAL's THE BASS: notes played) */
    bl_live_note = (uint8_t)note;
    bl_live_held = 1u;
    bl_live_new = 1u;
    if (!bs.rec || ui.page != PG_BSEQ) {
        ui.redraw |= RG_FOOT;
        return;
    }
    if (playing_now()) {                 /* REC, playing: at the nearest 16th; held, on (bs_poll) */
        uint32_t s = bs_rec_step();
        bs_write(s, note, 0u);
        bs.wrote = (int8_t)s;
        bs.steps_seen = bl_steps_n;
    } else {                             /* REC, stopped: step by step */
        bs_write(bs.cur, note, 0u);
        bs.cur = (uint8_t)((bs.cur + 1u) % BL_STEPS);
    }
    ui.redraw |= RG_FOOT;
}

static void bs_note_off(void)
{
    bs.key = 0u;
    bs.wrote = -1;
    bl_live_held = 0u;
    bl_live_up = 1u;
    ui.redraw |= RG_FOOT;
}

/* the keys (input, on the two pages): a keyboard, last note first; SEQ held, white keys 1-8 the lines */
static void bs_keys(uint32_t keys, uint32_t kdn, uint32_t kup, uint32_t btn)
{
    uint32_t i;
    keys &= BS_KEYS;
    kdn &= BS_KEYS;
    kup &= BS_KEYS;
    if (btn & 1u << B_SEQ) {
        for (i = 0; i < 8u; i++)
            if (kdn >> WK[i] & 1u) {
                bs_pick(i);
                bs.seq_used = 1u;
            }
        return;
    }
    for (i = 0; i < 27u; i++)
        if (kdn >> i & 1u)
            bs_note_on(i);               /* (the last one pressed sounds) */
    if (bs.key && kup >> (bs.key - 1u) & 1u) {
        uint32_t j, other = 0u;
        for (j = 27u; j-- > 0u;)         /* (another still held: it sounds again) */
            if (keys >> j & 1u)
                other = j + 1u;
        bs_note_off();
        if (other)
            bs_note_on(other - 1u);
    }
}

/* SEQ held + white key from another page: back there when SEQ is let go */
static void bs_keys_any(uint32_t kdn, uint32_t btn)
{
    uint32_t i;
    if (!(btn & 1u << B_SEQ) || bs_page())
        return;
    for (i = 0; i < 8u; i++)
        if (kdn >> WK[i] & 1u) {
            bs_pick(i);
            bs.seq_used = 1u;
        }
}

static void bs_rec_toggle(void)
{
    bs.rec ^= 1u;
    app_say("BASS REC", bs.rec ? (playing_now() ? "PLAY THE KEYS" : "KEYS: STEP BY STEP") : "OFF");
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
}

static void bs_clear(void)
{
    char t[16] = "LINE ";
    bl_line_clear(bls.sel);
    int_text((int32_t)bls.sel + 1, t + 5);
    app_say(t, "CLEARED");
    bs_dirty();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

static void bs_copy_paste(void)          /* SEL + SEQ: the line copied; on another, pasted (its sound too) */
{
    char t[24];
    if (bs.clip < 0 || bs.clip == (int8_t)bls.sel) {
        bs.clip = (int8_t)bls.sel;
        t[0] = 0;
        text_cat(t, "COPIED LINE ");
        int_text((int32_t)bls.sel + 1, t + bs_len(t));
        app_say(t, "PICK ANOTHER, SEL + SEQ");
        return;
    }
    bls.l[bls.sel] = bls.l[(uint32_t)bs.clip % BL_N];
    t[0] = 0;
        text_cat(t, "PASTED ");
    int_text(bs.clip + 1, t + bs_len(t));
    text_cat(t, " > ");
    int_text((int32_t)bls.sel + 1, t + bs_len(t));
    app_say(t, "SEL + SAVE KEEPS IT");
    bs.clip = -1;
    if (bl_play == bls.sel)
        bl_sound_changed();
    bs_dirty();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

static void bs_rotate(int32_t d)         /* SEQ held + K1: the line a step later or earlier */
{
    bl_line_t *l = bs_line();
    bl_step_t t[BL_STEPS];
    uint32_t i;
    for (i = 0; i < BL_STEPS; i++)
        t[(i + BL_STEPS + (uint32_t)(d > 0 ? 1 : -1)) % BL_STEPS] = l->st[i];
    memcpy(l->st, t, sizeof t);
    bs_dirty();
    ui.redraw |= RG_BLOCKS;
}

/* OCT- / OCT+ on the two pages: the keys' octave (both: back to F1) */
static void bs_oct(uint32_t btn, uint32_t bdn)
{
    char t[8];
    if (!(bdn & (1u << B_OCTDN | 1u << B_OCTUP)))
        return;
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
        bls.oct = 1u;
    else if (bdn & 1u << B_OCTUP && bls.oct < 4u)
        bls.oct++;
    else if (bdn & 1u << B_OCTDN && bls.oct > 0u)
        bls.oct--;
    note_text(12u * (bls.oct + 1u) + 5u, t);
    app_say("KEYS FROM", t);
    ui.redraw |= RG_FOOT;
}

static float *bs_par(uint32_t i)         /* EDIT's knob i on its part: the setting */
{
    uint32_t p = BT_PAR[bs.tab % BT_N][i & 3u];
    return p == 0xFFu ? &bs_line()->snd.volume : &bs_line()->snd.p[p];
}

/* a knob on the two pages (input): 1 taken (SEL held: SEL's own, everywhere) */
static int bs_enc(int k, int32_t d, uint32_t btn)
{
    bl_line_t *l = bs_line();
    if (btn & 1u << B_SEL || !bs_page())
        return 0;
    if (k == EN_ALGO) {                  /* the line, on both */
        bs_pick((bls.sel + BL_N + (uint32_t)(d > 0 ? 1 : -1)) % BL_N);
        return 1;
    }
    if (ui.page == PG_BSEQ) {
        if (k == EN_K1 && btn & 1u << B_SEQ) {
            bs_rotate(d);
            bs.seq_used = 1u;
            return 1;
        }
        if (k == EN_SELECT)
            bs.cur = (uint8_t)((bs.cur + BL_STEPS * 4u + (uint32_t)d) % BL_STEPS);
        else if (k == EN_K1) {           /* the step's note: down past the lowest, a rest */
            bl_step_t *s = &l->st[bs.cur];
            int32_t v = s->note ? (int32_t)s->note + d : (d > 0 ? 36 : 0);
            s->note = (uint8_t)(v < 12 ? 0 : v > 108 ? 108 : v);
            if (!s->note)
                s->fl = 0u;
            bs_dirty();
            if (s->note && !playing_now()) {   /* (stopped: heard) */
                bl_live_note = (uint8_t)bl_note_of(l, bs.cur);
                bl_live_new = 1u;
                bl_live_up = 1u;
            }
        } else if (k == EN_K2) {         /* NORMAL, ACCENT, SLIDE, BOTH */
            bl_step_t *s = &l->st[bs.cur];
            s->fl = (uint8_t)((s->fl + 4u + (uint32_t)(d > 0 ? 1 : -1)) & 3u);
            bs_dirty();
        } else if (k == EN_K3) {
            int32_t v = l->trans + d;
            l->trans = (int8_t)(v < -12 ? -12 : v > 12 ? 12 : v);
            bs_dirty();
        } else if (k == EN_K4) {
            l->snd.volume = knob_f(l->snd.volume, d, 0.02f, 0.0f, 1.0f);
            if (bl_play == bls.sel)
                bl_sound_changed();
            bs_dirty();
        } else
            return 0;                    /* (PRESETS: the pattern, as everywhere) */
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return 1;
    }
    if (k == EN_SELECT) {                /* EDIT: its parts */
        bs.tab = (uint8_t)((bs.tab + BT_N + (uint32_t)(d > 0 ? 1 : -1)) % BT_N);
        page_say();
    } else if (k >= EN_K1 && k <= EN_K4) {
        float *p = bs_par((uint32_t)(k - EN_K1));
        *p = knob_f(*p, d, 0.01f, 0.0f, 1.0f);
        if (bl_play == bls.sel)
            bl_sound_changed();
        bs_dirty();
    } else if (k == EN_PRESET) {         /* a built-in sound (the line's LEVEL kept) */
        l->preset = (uint8_t)((l->preset + BASS_PRESETS + (uint32_t)(d > 0 ? 1 : -1)) % BASS_PRESETS);
        bass_preset(&l->snd, l->preset);
        app_say("BASS SOUND", BASS_PRESET_NAME[l->preset]);
        if (bl_play == bls.sel)
            bl_sound_changed();
        bs_dirty();
    } else
        return 0;
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
    return 1;
}

/* each frame: a key held while recording goes on into the steps it plays over (a tie); the screen follows */
static void bs_poll(uint32_t now)
{
    if (bl_steps_n != bs.steps_seen) {
        if (bs.rec && bs.key && bs.wrote >= 0 && playing_now() && ui.page == PG_BSEQ) {
            uint32_t s = bl_pos;
            if (s != (uint32_t)bs.wrote) {   /* (the key still down: this step the same note, tied from the last) */
                bl_line_t *l = bs_line();
                l->st[(uint32_t)bs.wrote].fl |= BLF_SLD;
                bs_write(s, bs.note, 0u);
                bs.wrote = (int8_t)s;
            }
        }
        bs.steps_seen = bl_steps_n;
        if (ui.page == PG_BSEQ)
            ui.redraw |= RG_BLOCKS;
    }
    if (ui.page == PG_BEDIT && now - bs.frame_t >= 50u) {   /* (EDIT's picture, 20 a second) */
        bs.frame_t = now;
        ui.redraw |= RG_BLOCKS;
    }
}

/* the foot's names and values */
static const char *bs_kname(int i)
{
    static const char *const SEQ_N[4] = {"NOTE", "ACC / SLD", "TRANSPOSE", "LEVEL"};
    if (ui.page == PG_BSEQ)
        return SEQ_N[i & 3];
    {
        uint32_t p = BT_PAR[bs.tab % BT_N][i & 3];
        return p == 0xFFu ? "LEVEL" : BASS_PARAM_NAME[p];
    }
}

static void bs_value(int i, char *t)
{
    const bl_line_t *l = bs_line();
    static const char *const FL[4] = {"-", "ACCENT", "SLIDE", "ACC SLD"};
    t[0] = 0;
    if (ui.page == PG_BSEQ) {
        if (i == 0)
            note_text(l->st[bs.cur].note ? bl_note_of(l, bs.cur) : 0u, t);
        else if (i == 1)
            text_cat(t, FL[l->st[bs.cur].fl & 3u]);
        else if (i == 2) {
            if (l->trans > 0)
                text_cat(t, "+");
            int_text(l->trans, t + bs_len(t));
            text_cat(t, " ST");
        } else
            pct_text(l->snd.volume, t);
        return;
    }
    {
        uint32_t p = BT_PAR[bs.tab % BT_N][i & 3];
        float v = *bs_par((uint32_t)i);
        if (p == BS_CUTOFF)
            hz_text(bass_cutoff_hz(v), t);
        else if (p == BS_DECAY || p == BS_GLIDE) {
            int_text((int32_t)(p == BS_DECAY ? bass_decay_ms(v) : bass_glide_ms(v)), t);
            text_cat(t, " MS");
        } else if (p == BS_WAVE)
            text_cat(t, v < 0.05f ? "SAW" : v > 0.95f ? "SQUARE" : "SAW+SQR");
        else
            pct_text(v, t);
    }
}

static float bs_norm(int i) { return ui.page == PG_BEDIT ? *bs_par((uint32_t)i) : -1.0f; }

static void bs_say(char *t)              /* the header's part */
{
    text_cat(t, "LINE ");
    int_text((int32_t)bls.sel + 1, t + bs_len(t));
    if (ui.page == PG_BEDIT) {
        text_cat(t, " ");
        text_cat(t, BT_NAME[bs.tab % BT_N]);
    }
}

static void bs_tabs(int32_t x)           /* the tabs under the header */
{
    uint32_t i;
    if (ui.page == PG_BEDIT) {
        for (i = 0; i < BT_N; i++)
            x = tab(x, BT_NAME[i], i == bs.tab);
    } else {
        char t[8];
        note_text(12u * (bls.oct + 1u) + 5u, t);
        x = tab(x, "LINES", 1);
        x = tab(x, bs.rec ? "REC ON" : "REC", bs.rec);
        cv_text(236 - text_w(&FONT_T, t) - text_w(&FONT_T, "KEYS "), 1, &FONT_T, "KEYS ", C_DIM);
        cv_text(236 - text_w(&FONT_T, t), 1, &FONT_T, t, C_GRAY);
    }
}

/* the foot's strip over the black keys: their notes */
static void bs_black_names(void)
{
    uint32_t i;
    for (i = 0; i < 11u; i++) {
        char t[6];
        uint32_t bit = BK[i], note = 12u * (bls.oct + 1u) + 5u + bit;
        int on = bs.key == bit + 1u;
        int32_t x = 2 + (int32_t)i * 21;
        t[0] = 0;
        text_cat(t, NOTE_NAME[note % 12u]);
        cv_rect(x, 0, 20, 14, on ? C_HI : C_BLOCK);
        cv_text(x + (20 - text_w(&FONT_T, t)) / 2, 1, &FONT_T, t, on ? C_BLACK : C_GRAY);
    }
}

/* SEQ's picture: the lines (tabs), the line selected's two bars as a roll, the step selected */
static void bs_seq_band(int32_t top)
{
    const bl_line_t *l = bs_line();
    brk_status_t st;
    uint32_t i, lo = 127u, hi = 0u, b;
    int playing;
    char t[40];
    brk_status(&st);
    playing = st.playing && bl_play == bls.sel;
    for (i = 0; i < BL_N; i++) {         /* the eight lines */
        int32_t x = 2 + (int32_t)i * 30, y = 46 - top;
        int sel = i == bls.sel, play = st.playing && i == bl_play, empty = bl_empty(i);
        cv_rect(x, y, 28, 16, sel ? C_HI : C_BLOCK);
        t[0] = 0;
        int_text((int32_t)i + 1, t);
        cv_text(x + (28 - text_w(&FONT_T, t)) / 2, y + 2, &FONT_T, t, sel ? C_BLACK : empty ? C_DIM : C_WHITE);
        if (play)
            cv_rect(x, y + 16, 28, 2, C_AMB);   /* (the line heard) */
    }
    for (i = 0; i < BL_STEPS; i++)
        if (l->st[i].note) {
            uint32_t n = bl_note_of(l, i);
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
        }
    if (hi < lo)
        lo = hi = 36u;
    if (hi - lo < 12u) {
        lo = lo > 6u ? lo - (12u - (hi - lo)) / 2u : lo;
        hi = lo + 12u;
    }
    for (b = 0; b < 2u; b++) {           /* the two bars */
        int32_t y0 = 68 - top + (int32_t)b * 44, h = 40;
        cv_text(0, y0 + 14, &FONT_XS, b ? "2" : "1", C_DIM);
        for (i = 0; i < 16u; i++) {
            uint32_t s = b * 16u + i, n = l->st[s].note ? bl_note_of(l, s) : 0u;
            int32_t x = 8 + (int32_t)i * 14;
            uint16_t g = playing && bl_pos == s ? RGB(90, 60, 40) : i % 4u == 0u ? RGB(38, 38, 48) : C_BLOCK;
            cv_rect(x, y0, 13, h, g);
            if (n) {
                int32_t y = y0 + h - 6 - (int32_t)((n - lo) * (uint32_t)(h - 8) / (hi - lo));
                uint16_t c = l->st[s].fl & BLF_ACC ? C_WHITE : C_HI;
                uint32_t nx = (s + 1u) % BL_STEPS;
                int tie = l->st[s].fl & BLF_SLD && l->st[nx].note == l->st[s].note && i < 15u;
                cv_rect(x + 1, y, tie ? 14 : 11, 5, c);
                if (l->st[s].fl & BLF_SLD && l->st[nx].note && !tie && i < 15u) {   /* (a slide: its glide drawn) */
                    uint32_t n2 = bl_note_of(l, nx);
                    int32_t y2 = y0 + h - 6 - (int32_t)((n2 - lo) * (uint32_t)(h - 8) / (hi - lo));
                    cv_line(x + 11, y + 2, x + 15, y2 + 2, C_AMB);
                }
            }
            if (s == bs.cur && (ui.page == PG_BSEQ)) {   /* (the step selected: framed) */
                cv_rect(x, y0, 13, 1, C_WHITE);
                cv_rect(x, y0 + h - 1, 13, 1, C_WHITE);
                cv_rect(x, y0, 1, h, C_WHITE);
                cv_rect(x + 12, y0, 1, h, C_WHITE);
            }
        }
    }
    {                                    /* what is going on */
        int32_t y = 158 - top;
        t[0] = 0;
        text_cat(t, "STEP ");
        int_text((int32_t)bs.cur + 1, t + bs_len(t));
        text_cat(t, "  ");
        note_text(l->st[bs.cur].note ? bl_note_of(l, bs.cur) : 0u, t + bs_len(t));
        if (l->st[bs.cur].fl & BLF_ACC)
            text_cat(t, "  ACC");
        if (l->st[bs.cur].fl & BLF_SLD)
            text_cat(t, "  SLD");
        cv_text(4, y, &FONT_T, t, C_WHITE);
        cv_text(236 - text_w(&FONT_T, BASS_PRESET_NAME[l->preset % BASS_PRESETS]), y, &FONT_T,
                BASS_PRESET_NAME[l->preset % BASS_PRESETS], C_GRAY);
        y += 16;
        if (st.playing && bls.sel != bl_play) {
            t[0] = 0;
        text_cat(t, "LINE ");
            int_text((int32_t)bls.sel + 1, t + 5);
            text_cat(t, " COMES IN AT THE NEXT BAR");
            cv_text(4, y, &FONT_XS, t, C_AMB);
        } else if (bs.rec)
            cv_text(4, y, &FONT_XS, st.playing ? "REC: PLAY IN TIME, HOLD A KEY: A LONG NOTE"
                                               : "REC: EACH KEY WRITES A STEP, THEN NEXT", C_AMB);
        else
            cv_text(4, y, &FONT_XS, "KEYS PLAY   REC RECORDS   SEQ + KEY 1-8", C_DIM);
    }
}

/* EDIT's picture: the bass's wave as it sounds, the part's four settings as cards */
static void bs_edit_band(int32_t top)
{
    const bl_line_t *l = bs_line();
    char t[24];
    uint32_t i;
    if (top < 104) {                     /* (the wave: rows 44..103) */
        uint32_t at = bass_scope_at, s0 = at - 240u, j;
        int32_t mid = 76 - top;
        for (j = 0; j < 240u; j++) {     /* (from a rising zero, so it stands still: 480 back at most, of 512) */
            uint32_t a = at - 240u - j;
            if (bass_scope[(a - 1u) % BASS_SCOPE_N] < 0 && bass_scope[a % BASS_SCOPE_N] >= 0) {
                s0 = a;
                break;
            }
        }
        int32_t pk = 12;                 /* (scaled to the window: its peak 26 px, a quiet one not blown up) */
        for (j = 0; j < 239u; j++) {
            int32_t v = bass_scope[(s0 + j) % BASS_SCOPE_N];
            pk = v > pk ? v : -v > pk ? -v : pk;
        }
        cv_rect(0, mid, 240, 1, RGB(40, 40, 50));
        for (j = 0; j < 238u; j++) {
            int32_t v0 = bass_scope[(s0 + j) % BASS_SCOPE_N], v1 = bass_scope[(s0 + j + 1u) % BASS_SCOPE_N];
            cv_line(1 + (int32_t)j, mid - v0 * 26 / pk, 2 + (int32_t)j, mid - v1 * 26 / pk, C_HI);
        }
        t[0] = 0;
        text_cat(t, "LINE ");
        int_text((int32_t)bls.sel + 1, t + 5);
        cv_text(4, 46 - top, &FONT_T, t, C_WHITE);
        cv_text(236 - text_w(&FONT_T, BASS_PRESET_NAME[l->preset % BASS_PRESETS]), 46 - top, &FONT_T,
                BASS_PRESET_NAME[l->preset % BASS_PRESETS], C_GRAY);
    }
    for (i = 0; i < 4u; i++) {           /* (the cards: rows 110..163) */
        int32_t x = 2 + (int32_t)i * 60, y = 110 - top;
        float v = *bs_par(i);
        cv_rect(x, y, 56, 52, C_BLOCK);
        cv_text(x + 4, y + 3, &FONT_XS, bs_kname((int)i), C_GRAY);
        bs_value((int)i, t);
        cv_text(x + 4, y + 22, text_w(&FONT_T, t) > 50 ? &FONT_XS : &FONT_T, t, C_HI);
        cv_rect(x + 4, y + 44, 48, 3, RGB(50, 50, 60));
        cv_rect(x + 4, y + 44, (int32_t)(v * 48.0f + 0.5f), 3, C_HI);
    }
    cv_text(4, 166 - top, &FONT_XS, "SELECT: OSC FILTER ENV  PRESETS: SOUND", C_DIM);
    cv_text(4, 178 - top, &FONT_XS, "ALGORITHM: THE LINE   THE KEYS PLAY IT", C_DIM);
}

static void draw_bass(void)
{
    int32_t top;
    for (top = 44; top < 192; top += 60) {   /* (the canvas holds 60 rows) */
        int32_t h = 192 - top < 60 ? 192 - top : 60;
        cv_begin(240, (uint32_t)h, C_BLACK);
        if (ui.page == PG_BSEQ)
            bs_seq_band(top);
        else
            bs_edit_band(top);
        cv_blit(0, (uint32_t)top);
    }
}

#ifdef PLAT_HOST
/* the host's checks (app_expect) */
static int bs_expect(const char *what, int *got)
{
    const bl_line_t *l = bs_line();
    uint32_t i, n = 0u;
    if (!strcmp(what, "bsel"))
        *got = bls.sel;
    else if (!strcmp(what, "bplay"))
        *got = bl_play;
    else if (!strcmp(what, "bcur"))
        *got = bs.cur;
    else if (!strcmp(what, "brec"))
        *got = bs.rec;
    else if (!strcmp(what, "boct"))
        *got = bls.oct;
    else if (!strcmp(what, "bnote"))     /* the step selected's note, 0 a rest */
        *got = l->st[bs.cur].note;
    else if (!strcmp(what, "bflags"))
        *got = l->st[bs.cur].fl;
    else if (!strcmp(what, "btrans"))
        *got = l->trans;
    else if (!strcmp(what, "btab"))
        *got = bs.tab;
    else if (!strcmp(what, "bcut"))      /* the line's CUTOFF, 0..100 */
        *got = (int)(l->snd.p[BS_CUTOFF] * 100.0f + 0.5f);
    else if (!strcmp(what, "bpre"))
        *got = l->preset;
    else if (!strcmp(what, "bnotes")) {  /* the line's steps with a note */
        for (i = 0; i < BL_STEPS; i++)
            n += l->st[i].note != 0u;
        *got = (int)n;
    } else if (!strcmp(what, "bties")) { /* its steps tied into the next (SLIDE onto the same note) */
        for (i = 0; i + 1u < BL_STEPS; i++)
            n += l->st[i].fl & BLF_SLD && l->st[i + 1u].note == l->st[i].note && l->st[i].note;
        *got = (int)n;
    } else if (!strcmp(what, "bmeter"))  /* the bass's level, dBFS (whole) */
        *got = meter_bass > 1e-6f ? (int)(20.0f * brk_log2f_c(meter_bass) * 0.30103f - 0.5f) : -120;
    else
        return 0;
    return 1;
}
#endif
