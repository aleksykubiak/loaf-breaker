/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS's two pages (the lines and their engine: bline.c, dsp/bass.c). SEQ and EDIT are the bass's now; the
 * pattern's steps and the KICK menu stay where they were reached before (the pattern: PRESETS; the KICK menu, the
 * drums and the break: SEL + EDIT).
 *   BASS SEQ (PG_BSEQ, SEQ): eight lines of two bars. ALGORITHM the line (playing: at the next bar), SELECT the step,
 *     K1 its note, K2 ACCENT / SLIDE, K3 the line's TRANSPOSE, K4 DEL (a click: the step a rest). The keys are a keyboard (F to G, OCT- /
 *     OCT+ its octave): REC, playing, records them into the line at the nearest 16th (a key held: a long note);
 *     stopped, REC writes them step by step from the step selected. SEL + REC clears the line, SEQ held + white key
 *     1-8 picks a line (from any page: back there after), SEQ held + K1 turns the line a step round, SEL + SEQ copies
 *     the line, then on another pastes it.
 *   BASS EDIT (PG_BEDIT, EDIT): the sound of the line selected. SELECT its parts (OSC, FILTER, ENV), K1-K4 theirs,
 *     PRESETS a built-in sound, ALGORITHM the line; the keys play it. */

#define BS_KEYS 0x7FFFFFFu               /* the 27 keys' bits: each its semitone from the lowest, an F */
static int32_t tab(int32_t x, const char *s, int on);   /* (main.c) */
enum { BT_OSC, BT_FILT, BT_ENV, BT_MOD, BT_N, BT_EQ = BT_N, BT_FX, BT_ALL };   /* (BABSZTYL: MOD, bline.c bl_mod_*) */   /* (EQ, FX: their own pages, PG_KEQ and the
                                          * FX page's BASS FX slots, LOAF BREAKER's KICK EQ and KICK FX) */
static const char *const BT_NAME[BT_ALL] = {"OSC", "FILTER", "ENV", "MOD", "EQ", "FX"};
static const char *const BT_MOD_NAME[4] = {"DETUNE", "OSC 2", "LFO RATE", "LFO DEPTH"};
static const uint8_t BT_PAR[BT_N][4] = {{BS_WAVE, BS_SUB, BS_DRIVE, 0xFFu},   /* (0xFF: the line's LEVEL) */
                                        {BS_CUTOFF, BS_RESO, BS_ENV, BS_KEYTRK},
                                        {BS_DECAY, BS_SUSTAIN, BS_ACCENT, BS_GLIDE},
                                        {0xFEu, 0xFEu, 0xFEu, 0xFEu}};   /* (0xFE: MOD's, packed in the line) */
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static struct {
    uint8_t cur;                         /* SEQ: the step selected, 0..31 */
    uint8_t stab;                        /* SEQ's tab: 0 the LINE, 1 the GENERATOR (BABSZTYL; SEQ again: the next) */
    uint8_t tab;                         /* EDIT: its part, BT_* */
    uint8_t rec;                         /* REC on (the bass's own: the pattern's REC is HOME's) */
    uint8_t key;                         /* the key sounding, its bit + 1 (0: none) */
    uint8_t note;                        /* its note as played */
    int8_t wrote;                        /* recording: the step its key wrote last (-1: none) */
    int8_t clip;                         /* SEL + SEQ: the line copied (-1: none) */
    uint8_t seq_down, seq_used, seq_back, seq_from;   /* SEQ held: what it did, where it came from */
    uint32_t steps_seen, frame_t;
} bs = {0u, 0u, 0u, 0u, 0u, 0u, -1, -1, 0u, 0u, 0u, 0u, 0u, 0u};

static uint32_t bs_len(const char *t)   /* (no libc on the FM-1) */
{
    uint32_t n = 0u;
    while (t[n])
        n++;
    return n;
}

static int bs_page_n(uint32_t p) { return p == PG_BSEQ || p == PG_BEDIT; }
static int bs_page(void) { return bs_page_n(ui.page); }
static void kfx_open(int on);            /* (main.c) */
#define IS_KFX() (ui.page == PG_FX && ui.fx_focus >= BRK_FX_SLOTS)   /* (main.c's, the BASS FX slots shown) */
static void bs_bank_go(uint32_t b);
static bl_line_t *bs_line(void) { return bl_at(bls.sel); }
static void bs_dirty(void) { set_dirty = 1u; }
/* SEQ › GENERATOR's four (bline.c bl_gen), kept in kx2.rsv[0..4] (KITS' old bytes): GEN_TAG, CORE, VARIATION, ROLL,
 * FILL; untagged CORE 100 (the line as recorded) */
#define GEN_TAG 0x6Eu
static const char *const GEN_NAME[4] = {"CORE", "VARIATION", "ROLL", "FILL"};
static void bs_gen_push(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        bl_gen[i] = kx2.rsv[0] == GEN_TAG ? (uint8_t)(kx2.rsv[1u + i] > 100u ? 100u : kx2.rsv[1u + i])
                                          : (uint8_t)(i ? 0u : 100u);
}
static void bs_gen_turn(uint32_t k, int32_t d)
{
    char t[8];
    int32_t v;
    if (kx2.rsv[0] != GEN_TAG) {
        uint32_t i;
        for (i = 0; i < 4u; i++)
            kx2.rsv[1u + i] = bl_gen[i];
        kx2.rsv[0] = GEN_TAG;
    }
    v = (int32_t)kx2.rsv[1u + (k & 3u)] + d * 2;
    kx2.rsv[1u + (k & 3u)] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    bs_gen_push();
    set_dirty = 1u;
    t[0] = 0;
    int_text(bl_gen[k & 3u], t);
    text_cat(t, " %");
    app_say(GEN_NAME[k & 3u], t);
}

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
    if (IS_KFX())
        kfx_open(0);
    if (bs.tab >= BT_N)                  /* (back from EQ or FX: OSC) */
        bs.tab = 0u;
    ui.page = (uint8_t)pg;
    ui.redraw |= RG_HEAD | RG_WAVE | RG_BLOCKS | RG_FOOT;
    page_say();
}

static void bs_line_text(uint32_t n, char *t)   /* 0..63 -> "B1 LINE 3" */
{
    t[0] = 'B';
    int_text((int32_t)(n % BL_ALL / BL_N) + 1, t + 1);
    text_cat(t, " LINE ");
    int_text((int32_t)(n % BL_N) + 1, t + bs_len(t));
}

static void bs_pick(uint32_t n)          /* a line selected (0..63): playing, at the next bar; stopped, at once */
{
    char t[16];
    n %= BL_ALL;
    bls.sel = (uint8_t)n;
    bs_line_text(n, t);
    if (!playing_now()) {
        bl_play = (uint8_t)n;
        bl_sound_changed();
        app_say(t, bl_empty(n) ? "EMPTY: REC IT" : BASS_PRESET_NAME[bl_at(n)->preset % BASS_PRESETS]);
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
        for (i = 0; i < 8u; i++) {
            if (kdn >> WK[i] & 1u) {
                bs_pick(bl_bank_sel() * BL_N + i);
                bs.seq_used = 1u;
            }
            if (kdn >> BK[i] & 1u) {
                bs_bank_go(i);
                bs.seq_used = 1u;
            }
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
    if (!(btn & 1u << B_SEQ) || ui.page == PG_BSEQ)   /* (on SEQ: bs_keys) */
        return;
    for (i = 0; i < 8u; i++) {
        if (kdn >> WK[i] & 1u) {
            bs_pick(bl_bank_sel() * BL_N + i);
            bs.seq_used = 1u;
        }
        if (kdn >> BK[i] & 1u) {
            bs_bank_go(i);
            bs.seq_used = 1u;
        }
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
    char t[16];
    bl_line_clear(bls.sel);
    bs_line_text(bls.sel, t);
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
        text_cat(t, "COPIED ");
        bs_line_text(bls.sel, t + bs_len(t));
        app_say(t, "PICK ANOTHER, SEL + SEQ");
        return;
    }
    *bl_at(bls.sel) = *bl_at((uint32_t)bs.clip);
    t[0] = 0;
        text_cat(t, "PASTED > ");
    bs_line_text(bls.sel, t + bs_len(t));
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

static float *bs_par(uint32_t i)         /* EDIT's knob i on its part: the setting (MOD: a copy, 0..1, for its bar) */
{
    static float modv;
    uint32_t p = BT_PAR[bs.tab % BT_N][i & 3u];
    if (p == 0xFEu) {
        modv = (float)bl_mod_get(bs_line(), i) / (float)BL_MOD_MAX[i & 3u];
        return &modv;
    }
    return p == 0xFFu ? &bs_line()->snd.volume : &bs_line()->snd.p[p];
}

/* SEL + K1-K4 on any page (LOAF BREAKER's kick knobs): the line playing's CUTOFF, RESO, ENV, DECAY; HOME's K4 its
 * CUTOFF too */
static const uint8_t BS_LIVE[4] = {BS_CUTOFF, BS_RESO, BS_ENV, BS_DECAY};
static const char *bs_live_name(uint32_t i) { return BASS_PARAM_NAME[BS_LIVE[i & 3u]]; }
static void bs_live_value(uint32_t i, char *t)
{
    float v = bl_at(bl_play)->snd.p[BS_LIVE[i & 3u]];
    t[0] = 0;
    if ((i & 3u) == 0u)
        hz_text(bass_cutoff_hz(v), t);
    else if ((i & 3u) == 3u) {
        int_text((int32_t)bass_decay_ms(v), t);
        text_cat(t, " MS");
    } else
        pct_text(v, t);
}
static void bs_live(uint32_t i, int32_t d)
{
    float *p = &bl_at(bl_play)->snd.p[BS_LIVE[i & 3u]];
    char t[16];
    *p = knob_f(*p, d, 0.01f, 0.0f, 1.0f);
    bl_sound_changed();
    bs_dirty();
    bs_live_value(i, t);
    app_say(bs_live_name(i), t);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* EDIT's parts, round: OSC, FILTER, ENV (its own page), EQ (PG_KEQ), FX (the FX page's BASS FX slots) */
static void bs_part_go(uint32_t t)
{
    t %= BT_ALL;
    if (IS_KFX())
        kfx_open(0);
    bs.tab = (uint8_t)t;
    if (t == BT_EQ)
        ui.page = PG_KEQ;
    else if (t == BT_FX) {
        ui.page = PG_FX;
        kfx_open(1);
    } else
        ui.page = PG_BEDIT;
    ui.redraw |= RG_HEAD | RG_WAVE | RG_BLOCKS | RG_FOOT;
    page_say();
}
static void bs_part_turn(int32_t d) { bs_part_go((bs.tab + BT_ALL + (uint32_t)(d > 0 ? 1 : -1)) % BT_ALL); }

/* a bank (SEQ + black key 1-8, SEL + PRESETS): the line's place in it kept; playing, at the next bar */
static void bs_bank_go(uint32_t b)
{
    char t[16];
    bl_bank_pick(b);
    if (!playing_now()) {
        bl_play = bls.sel;
        bl_sound_changed();
    }
    bs_line_text(bls.sel, t);
    app_say(t, playing_now() && bl_play != bls.sel ? "AT THE NEXT BAR" : bl_empty(bls.sel) ? "EMPTY: REC IT" : "");
    bs_dirty();
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
}
static void bs_bank_turn(int32_t d) { bs_bank_go((bl_bank_sel() + BL_BANKS + (uint32_t)(d > 0 ? 1 : -1)) % BL_BANKS); }

/* SEL + PRESETS on SEQ (BABSZTYL): a built-in sound on the bank's eight lines at once (each line's LEVEL kept; each
 * line shaped on in EDIT as before), from the line selected's */
static void bs_bank_preset(int32_t d)
{
    uint32_t b = bl_bank_sel(), i, p = (bs_line()->preset + BASS_PRESETS + (uint32_t)(d > 0 ? 1 : -1)) % BASS_PRESETS;
    char t[12] = "BANK ";
    for (i = 0; i < BL_N; i++) {
        bl_preset_put(bl_at(b * BL_N + i), p);
    }
    int_text((int32_t)b + 1, t + 5);
    text_cat(t, " SOUND");
    app_say(t, BASS_PRESET_NAME[p]);
    if (bl_play / BL_N == b)
        bl_sound_changed();
    bs_dirty();
    ui.redraw |= RG_HEAD | RG_BLOCKS | RG_FOOT;
}

/* DEL: the step s a rest; a note tied over several steps (SLIDE into the same note) all of it; the step before it
 * sliding into it no more. 0: nothing there */
static int bs_del(bl_line_t *l, uint32_t s)
{
    uint32_t a = s % BL_STEPS, n = 0u, note = l->st[a].note, pv;
    if (!note)
        return 0;
    while (n < BL_STEPS) {               /* (back to the note's first step) */
        pv = (a + BL_STEPS - 1u) % BL_STEPS;
        if (!(l->st[pv].fl & BLF_SLD) || l->st[pv].note != note || pv == s % BL_STEPS)
            break;
        a = pv;
        n++;
    }
    pv = (a + BL_STEPS - 1u) % BL_STEPS;
    l->st[pv].fl &= (uint8_t)~BLF_SLD;   /* (the step before: no slide into a rest) */
    for (n = 0; n < BL_STEPS; n++) {     /* (on through its tied steps) */
        uint32_t i = (a + n) % BL_STEPS, more = l->st[i].fl & BLF_SLD && l->st[(i + 1u) % BL_STEPS].note == note;
        l->st[i].note = 0u;
        l->st[i].fl = 0u;
        if (!more)
            break;
    }
    bs_dirty();
    app_say("DEL", "STEP CLEARED");
    return 1;
}

/* a knob on the two pages (input): 1 taken (SEL held: SEL's own, everywhere) */
static int bs_enc(int k, int32_t d, uint32_t btn)
{
    bl_line_t *l = bs_line();
    if (btn & 1u << B_SEL || !bs_page())
        return 0;
    if (k == EN_ALGO) {                  /* the line, on both */
        bs_pick((bls.sel + BL_ALL + (uint32_t)(d > 0 ? 1 : -1)) % BL_ALL);
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
        else if (bs.stab && k >= EN_K1 && k <= EN_K4)   /* (GENERATOR: CORE, VARIATION, ROLL, FILL) */
            bs_gen_turn((uint32_t)(k - EN_K1), d);
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
        } else if (k == EN_K4) {         /* DEL (BABSZTYL: LEVEL before, now EDIT › OSC's K4 alone): a click, the
                                          * step under the cursor a rest, a long note (its tied steps) whole */
            if (!bs_del(l, bs.cur))
                app_say("DEL", "NOTHING HERE");
        } else
            return 0;                    /* (PRESETS: the pattern, as everywhere) */
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return 1;
    }
    if (k == EN_SELECT) {                /* EDIT: its parts (EQ, FX: their pages) */
        bs_part_turn(d);
        return 1;
    } else if (k >= EN_K1 && k <= EN_K4 && bs.tab == BT_MOD) {   /* (MOD: a click a level) */
        uint32_t q = (uint32_t)(k - EN_K1);
        int32_t v = (int32_t)bl_mod_get(l, q) + (d > 0 ? 1 : -1);
        bl_mod_set(l, q, (uint32_t)(v < 0 ? 0 : v));
        bs_dirty();
    } else if (k >= EN_K1 && k <= EN_K4) {
        float *p = bs_par((uint32_t)(k - EN_K1));
        *p = knob_f(*p, d, 0.01f, 0.0f, 1.0f);
        if (bl_play == bls.sel)
            bl_sound_changed();
        bs_dirty();
    } else if (k == EN_PRESET) {         /* a built-in sound (the line's LEVEL kept) */
        bl_preset_put(l, (l->preset + BASS_PRESETS + (uint32_t)(d > 0 ? 1 : -1)) % BASS_PRESETS);
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
    static const char *const SEQ_N[4] = {"NOTE", "ACC / SLD", "TRANSPOSE", "DEL"};
    if (ui.page == PG_BSEQ)
        return bs.stab ? GEN_NAME[i & 3] : SEQ_N[i & 3];
    {
        uint32_t p = BT_PAR[bs.tab % BT_N][i & 3];
        return p == 0xFEu ? BT_MOD_NAME[i & 3] : p == 0xFFu ? "LEVEL" : BASS_PARAM_NAME[p];
    }
}

static void bs_value(int i, char *t)
{
    const bl_line_t *l = bs_line();
    static const char *const FL[4] = {"-", "ACCENT", "SLIDE", "ACC SLD"};
    t[0] = 0;
    if (ui.page == PG_BSEQ && bs.stab) {
        int_text(bl_gen[i & 3], t);
        text_cat(t, " %");
        return;
    }
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
            text_cat(t, l->st[bs.cur].note ? "TURN" : "-");
        return;
    }
    {
        uint32_t p = BT_PAR[bs.tab % BT_N][i & 3];
        float v = *bs_par((uint32_t)i);
        if (p == 0xFEu) {                /* (MOD) */
            uint32_t q = bl_mod_get(bs_line(), (uint32_t)i);
            if (i == 2)
                text_cat(t, BASS_RATE_NAME[q % BASS_RATES]);
            else if (!q)
                text_cat(t, "OFF");
            else if (i == 0) {
                int_text((int32_t)(bass_detune_st(q) * 100.0f + 0.5f), t);
                text_cat(t, " CT");
            } else
                pct_text(v, t);
        } else if (p == BS_CUTOFF)
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

static float bs_norm(int i)
{
    return ui.page == PG_BEDIT ? *bs_par((uint32_t)i) : ui.page == PG_BSEQ && bs.stab ? (float)bl_gen[i & 3] * 0.01f : -1.0f;
}

static void bs_say(char *t)              /* the header's part */
{
    bs_line_text(bls.sel, t + bs_len(t));
    if (ui.page == PG_BSEQ && bs.stab)
        text_cat(t, " GEN");
    if (ui.page == PG_BEDIT) {
        text_cat(t, " ");
        text_cat(t, BT_NAME[bs.tab % BT_ALL]);
    }
}

static void bs_tabs(int32_t x)           /* the tabs under the header */
{
    uint32_t i;
    if (ui.page != PG_BSEQ) {            /* (EDIT, its EQ and FX pages) */
        uint32_t on = ui.page == PG_KEQ ? BT_EQ : IS_KFX() ? BT_FX : bs.tab;
        for (i = 0; i < BT_ALL; i++)
            x = tab(x, BT_NAME[i], i == on);
    } else {
        char t[8];
        note_text(12u * (bls.oct + 1u) + 5u, t);
        char bk[8] = "BANK ";
        int_text((int32_t)bl_bank_sel() + 1, bk + 5);
        x = tab(x, bk, !bs.stab);
        x = tab(x, "GENERATOR", bs.stab);
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
    for (i = 0; i < BL_N; i++) {         /* the bank's eight lines */
        uint32_t n = bl_bank_sel() * BL_N + i;
        int32_t x = 2 + (int32_t)i * 30, y = 46 - top;
        int sel = n == bls.sel, play = st.playing && n == bl_play, empty = bl_empty(n);
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
            bl_gen_t gv = {0u, 0u};
            if (bs.stab)                 /* (GENERATOR: the line as it will play) */
                n = bl_gen_step(l, bls.sel, s, &gv);
            uint16_t g = playing && bl_pos == s ? RGB(90, 60, 40) : i % 4u == 0u ? RGB(38, 38, 48) : C_BLOCK;
            cv_rect(x, y0, 13, h, g);
            if (n) {
                int32_t y = y0 + h - 6 - ((int32_t)n - (int32_t)lo) * (h - 8) / (int32_t)(hi - lo);
                uint16_t c = gv.fl & BLG_VAR ? C_AMB : l->st[s].fl & BLF_ACC ? C_WHITE : C_HI;
                uint32_t nx = (s + 1u) % BL_STEPS;
                int tie = !bs.stab && l->st[s].fl & BLF_SLD && l->st[nx].note == l->st[s].note && i < 15u;
                y = y < y0 + 1 ? y0 + 1 : y > y0 + h - 6 ? y0 + h - 6 : y;   /* (an octave past the line's: at its edge) */
                cv_rect(x + 1, y, tie ? 14 : gv.fl & BLG_RAT ? 5 : 11, 5, c);
                if (gv.fl & BLG_RAT)      /* (a ratchet: two) */
                    cv_rect(x + 7, y, 5, 5, c);
                if (!bs.stab && l->st[s].fl & BLF_SLD && l->st[nx].note && !tie && i < 15u) {   /* (a slide: its glide drawn) */
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
            bs_line_text(bls.sel, t);
            text_cat(t, " AT THE NEXT BAR");
            cv_text(4, y, &FONT_XS, t, C_AMB);
        } else if (bs.rec)
            cv_text(4, y, &FONT_XS, st.playing ? "REC: PLAY IN TIME, HOLD A KEY: A LONG NOTE"
                                               : "REC: EACH KEY WRITES A STEP, THEN NEXT", C_AMB);
        else if (bs.stab)
            cv_text(4, y, &FONT_XS, "AS IT PLAYS: AMBER CHANGED   SEQ: HOME", C_DIM);
        else
            cv_text(4, y, &FONT_XS, "KEYS PLAY  SEQ: GENERATOR  SEQ + KEY 1-8", C_DIM);
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
        bs_line_text(bls.sel, t);
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
    cv_text(4, 166 - top, &FONT_XS, "SELECT: OSC FILTER ENV MOD  PRESETS: SOUND", C_DIM);
    cv_text(4, 178 - top, &FONT_XS, "ALGORITHM: THE LINE   KEYS: AS ON HOME", C_DIM);
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
    if (!strcmp(what, "bstab"))          /* (BABSZTYL) SEQ's tab: 0 LINE, 1 GENERATOR */
        *got = bs.stab;
    else if (!strcmp(what, "bcore"))
        *got = bl_gen[0];
    else if (!strcmp(what, "bvar"))
        *got = bl_gen[1];
    else if (!strcmp(what, "bgroll"))    /* (broll: the BUILD's) */
        *got = bl_gen[2];
    else if (!strcmp(what, "bfill"))
        *got = bl_gen[3];
    else if (!strcmp(what, "bmod"))      /* the line's MOD, packed */
        *got = (int)bl_mod(bs_line());
    else if (!strcmp(what, "kbas"))      /* KILL BASS on */
        *got = kbas_hold;
    else if (!strcmp(what, "bgenvar")) { /* the line's steps the GENERATOR changes, as it will play */
        uint32_t i, c = 0u;
        for (i = 0; i < BL_STEPS; i++) {
            bl_gen_t g;
            uint32_t n = bl_gen_step(bs_line(), bls.sel, i, &g);
            c += (g.fl & (BLG_VAR | BLG_RAT)) || (!n && bs_line()->st[i].note) ? 1u : 0u;
        }
        *got = (int)c;
    } else if (!strcmp(what, "bsel"))
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
