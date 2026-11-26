/* SPDX-License-Identifier: MPL-2.0 */
/* BASS LINES (included by app.c, before the render): 64 lines of two bars (32 steps) for the BASS (dsp/bass.c), in
 * eight BANKS of eight, each line with its own sound. SEQ records and edits them, EDIT shapes the sound of the one
 * selected (ui/bass.c). A bank picked keeps the line's place in it (line 3 of bank 1 -> line 3 of bank 5).
 *   - a step: a note (0: a rest), ACCENT, SLIDE (it glides into the next step's note; into the same note: a tie)
 *   - the line selected is the line playing: picked while playing, it comes in at the next bar, from its first
 *     step (a live change of lines, on the bar); picked stopped, at once
 *   - the steps are the break's (brk_step_events): the tempo and the swing (PATTERN › GROOVE) are theirs
 *   - the keys played on SEQ and EDIT: the bass at once (last note first); while one is held the line is quiet
 * All 64 in the pool (7.4 KB, the room the kick and the drums left), kept in two store objects, OBJ_BASS (the head
 * and banks 1-4) and OBJ_BASS2 (banks 5-8), each the struct's own run of bytes (no copy). */

#define BL_N 8u                          /* lines in a bank */
#define BL_BANKS 8u
#define BL_ALL (BL_N * BL_BANKS)
#define BL_HALF (BL_ALL / 2u)            /* (the lines in each object) */
#define BL_STEPS 32u                     /* two bars of 16ths */
#define BL_MAGIC 0x32534142u             /* "BAS2" (BAS1: one bank of eight, 25 Nov 2026) */
#define BL_MAGIC2 0x62534142u            /* "BASb": the second object's */
enum { BLF_ACC = 1u, BLF_SLD = 2u };
typedef struct {
    uint8_t note, fl;                    /* note: MIDI (0: a rest); fl: BLF_* */
} bl_step_t;
typedef struct {
    bl_step_t st[BL_STEPS];
    bass_t snd;                          /* its sound (EDIT), its volume the line's LEVEL */
    int8_t trans;                        /* TRANSPOSE, -12 .. +12 semitones */
    uint8_t preset;                      /* the built-in sound last put on it (EDIT's PRESETS) */
    uint8_t mod[2];                      /* BABSZTYL: its MOD, packed (bass.h BASS_MOD; little end first; 0 none, as an
                                          * older save's rsv) */
} bl_line_t;
typedef struct {
    uint32_t magic;
    uint8_t sel;                         /* the line selected, 0..63 (its bank sel / 8; playing, or coming in) */
    uint8_t oct;                         /* the keys' octave: the lowest key F of octave oct (F1 = 1) */
    uint8_t rsv[2];
    float vol;                           /* the BASS's fader (MIX › LEVELS K2, LFO + K2) */
    bl_line_t a[BL_HALF];                /* banks 1-4: OBJ_BASS, from magic to here */
    uint32_t magic2;
    bl_line_t b[BL_HALF];                /* banks 5-8: OBJ_BASS2, from magic2 */
} bl_store_t;
#define BL_SIZE_A offsetof(bl_store_t, magic2)
#define BL_SIZE_B (sizeof(bl_store_t) - offsetof(bl_store_t, magic2))
_Static_assert(BL_SIZE_A <= 3840u && BL_SIZE_B <= 3840u, "a store object holds 3840 bytes");
static bl_store_t bls PLAT_POOL;
static bl_line_t *bl_at(uint32_t n) { n %= BL_ALL; return n < BL_HALF ? &bls.a[n] : &bls.b[n - BL_HALF]; }
static uint32_t bl_bank_sel(void) { return bls.sel / BL_N; }

/* the render's (written there, read by the UI) */
static volatile uint8_t bl_play;         /* the line playing */
static volatile uint8_t bl_pos;          /* its step playing, 0..31 */
static volatile uint32_t bl_steps_n;     /* steps played (the screen follows) */
static uint8_t bl_org;                   /* the step (brk_step_events' 0..63) the line playing began on */
/* the main loop's, read by the render */
static volatile uint8_t bl_live_note, bl_live_held, bl_live_new, bl_live_up;
static volatile uint8_t bl_snd_new;      /* a sound changed: the render sends it to the engine */
static volatile uint8_t bl_mute;         /* KIL (held or latched): the line quiet */
static volatile uint8_t bl_sw;           /* the black keys' switches (app.c): bit 0 DST (its DRIVE pushed), bit 1 DRV (its
                                          * DRIVE off), bit 2 SUB (its SUB off) */
static volatile uint8_t bl_fx;           /* the black keys' X2 (bit 0: an octave up), X4 (bit 1: every note ACCENTED) */
static uint16_t bl_on[8];                /* the render's: where its notes began in the block, for DUCK */
static uint32_t bl_non;
static uint8_t bl_last;                  /* the last note the line played (the rolls play it again) */

static int bl_empty(uint32_t n)          /* (n: 0..63) */
{
    uint32_t i;
    for (i = 0; i < BL_STEPS; i++)
        if (bl_at(n)->st[i].note)
            return 0;
    return 1;
}

/* the lines a new FM-1 starts with: six to play with at once, two empty to record (-1: a rest, a held note: the
 * same note again after a SLIDE) */
static const int8_t BL_DEF[6][BL_STEPS] = {
    {33, 33, 45, 33, -1, 33, 48, 33, 33, 43, 33, 45, -1, 36, 33, 40,
     33, 33, 45, 33, -1, 33, 48, 33, 45, 43, 40, 36, 33, -1, 45, 33},   /* ACID: A1 */
    {31, 31, 43, 31, 31, 43, 31, 46, 31, 31, 43, 31, 34, 36, 38, 43,
     31, 31, 43, 31, 31, 43, 31, 46, 31, 43, 46, 43, 38, 36, 34, 31},   /* SQUELCH: G1, rolling */
    {-1, -1, 33, -1, -1, -1, 33, -1, -1, -1, 33, -1, -1, -1, 33, 33,
     -1, -1, 33, -1, -1, -1, 33, -1, -1, -1, 36, -1, -1, 38, 36, 33},   /* GABBER: off the beat, between kicks */
    {33, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 31, 31, 31, 31,
     33, 33, 33, 33, 33, 33, 33, 33, 28, 28, 28, 28, 31, 31, 31, 31},   /* SUB: long notes */
    {45, -1, -1, 45, -1, -1, 48, -1, 45, -1, -1, 43, -1, 40, -1, -1,
     45, -1, -1, 45, -1, -1, 48, -1, 52, -1, 50, -1, 48, -1, 45, -1},   /* PLUCK: sparse, higher */
    {26, 26, 26, 26, 26, 26, 26, -1, 29, 29, 29, -1, 26, -1, 33, 31,
     26, 26, 26, 26, 26, 26, 26, -1, 29, 29, 29, -1, 24, -1, 28, 29},   /* FAT: D1, held */
};
static const uint32_t BL_DEF_ACC[6] = {0x91218921u, 0x11111111u, 0x44444444u, 0x01010101u, 0x09090909u, 0x10011001u};
static const uint32_t BL_DEF_SLD[6] = {0x00002444u, 0x20002000u, 0x00000000u, 0x7F7F7F7Fu, 0x00000000u, 0x0E7F0E7Fu};
static const uint8_t BL_DEF_SND[BL_N] = {0u, 1u, 2u, 3u, 4u, 5u, 0u, 3u};   /* the lines' sounds (BASS_PRESET_NAME) */

static uint32_t bl_mod(const bl_line_t *l) { return (uint32_t)l->mod[0] | (uint32_t)l->mod[1] << 8; }
static void bl_mod_put(bl_line_t *l, uint32_t m)
{
    l->mod[0] = (uint8_t)m;
    l->mod[1] = (uint8_t)(m >> 8);
}
/* MOD's four (EDIT › MOD): 0 DETUNE 0..15, 1 OSC 2 0..15, 2 RATE 0..7, 3 DEPTH 0..31 */
static const uint8_t BL_MOD_SH[4] = {0u, 4u, 8u, 11u}, BL_MOD_MAX[4] = {15u, 15u, 7u, 31u};
static uint32_t bl_mod_get(const bl_line_t *l, uint32_t k) { return bl_mod(l) >> BL_MOD_SH[k & 3u] & BL_MOD_MAX[k & 3u]; }
static void bl_mod_set(bl_line_t *l, uint32_t k, uint32_t v)
{
    uint32_t m = bl_mod(l) & ~((uint32_t)BL_MOD_MAX[k & 3u] << BL_MOD_SH[k & 3u]);
    bl_mod_put(l, m | (v > BL_MOD_MAX[k & 3u] ? BL_MOD_MAX[k & 3u] : v) << BL_MOD_SH[k & 3u]);
}
static void bl_preset_put(bl_line_t *l, uint32_t p)   /* a built-in sound on a line (its LEVEL kept) */
{
    p %= BASS_PRESETS;
    l->preset = (uint8_t)p;
    bass_preset(&l->snd, p);
    bl_mod_put(l, bass_preset_mod(p));
}

static void bl_line_clear(uint32_t n)
{
    memset(bl_at(n)->st, 0, sizeof bls.a[0].st);
}

static void bl_line_default(uint32_t n)  /* (bank 1: six lines to play with, two empty; the other banks empty, their
                                          * sounds as bank 1's) */
{
    bl_line_t *l = bl_at(n);
    uint32_t i;
    memset(l, 0, sizeof *l);
    bass_defaults(&l->snd);
    bl_preset_put(l, BL_DEF_SND[n % BL_N]);
    if (n < 6u)
        for (i = 0; i < BL_STEPS; i++) {
            int8_t v = BL_DEF[n][i];
            uint32_t nx = (i + 1u) % BL_STEPS;
            l->st[i].note = v > 0 ? (uint8_t)v : 0u;
            l->st[i].fl = (uint8_t)((BL_DEF_ACC[n] >> i & 1u ? BLF_ACC : 0u) |
                                    (BL_DEF_SLD[n] >> i & 1u && v > 0 && BL_DEF[n][nx] > 0 ? BLF_SLD : 0u));
        }
}

static void bl_defaults(void)
{
    uint32_t n;
    memset(&bls, 0, sizeof bls);
    bls.magic = BL_MAGIC;
    bls.magic2 = BL_MAGIC2;
    bls.oct = 1u;
    bls.vol = 1.0f;
    for (n = 0; n < BL_ALL; n++)
        bl_line_default(n);
    bl_play = 0u;
    bl_snd_new = 1u;
}

static void bl_load(void)
{
    if (plat_store_load(OBJ_BASS, &bls, BL_SIZE_A) != (int)BL_SIZE_A || bls.magic != BL_MAGIC ||
        plat_store_load(OBJ_BASS2, &bls.magic2, BL_SIZE_B) != (int)BL_SIZE_B || bls.magic2 != BL_MAGIC2)
        bl_defaults();
    bls.sel %= BL_ALL;
    if (!(bls.vol >= 0.0f && bls.vol <= 1.0f))
        bls.vol = 1.0f;
    if (bls.oct > 4u)
        bls.oct = 1u;
    bl_play = bls.sel;
    bl_snd_new = 1u;
}

static int bl_save(void)
{
    return plat_store_save(OBJ_BASS, &bls, BL_SIZE_A) == 0 && plat_store_save(OBJ_BASS2, &bls.magic2, BL_SIZE_B) == 0;
}
static void bl_bank_pick(uint32_t b)     /* the bank b, the line's place in it kept; stopped: at once */
{
    bls.sel = (uint8_t)((b % BL_BANKS) * BL_N + bls.sel % BL_N);
}

static void bl_sound_changed(void) { bl_snd_new = 1u; }

/* a step's note as it plays: its own, the line's TRANSPOSE added (0: a rest) */
static uint32_t bl_note_of(const bl_line_t *l, uint32_t i)
{
    int32_t v = l->st[i % BL_STEPS].note;
    if (!v)
        return 0u;
    v += l->trans;
    return (uint32_t)(v < 12 ? 12 : v > 108 ? 108 : v);
}

/* BABSZTYL: SEQ › GENERATOR, the line as played: CORE % of its steps exactly as recorded (the same ones whatever the
 * amount: each step's own dice), the others changed VARIATION % of the time (an octave, a neighbouring note of the
 * line's own, a rest; an empty step a short note), ROLL % of the notes doubled (a ratchet) or slid, FILL more of both
 * in the second bar. The same dice every time round: the line, its step, nothing random at play. bl_gen: CORE,
 * VARIATION, ROLL, FILL (0..100), the main loop's (bs_gen_push) */
static volatile uint8_t bl_gen[4] = {100u, 0u, 0u, 0u};
enum { BLG_VAR = 1u, BLG_RAT = 2u, BLG_SLD = 4u, BLG_ACC = 8u, BLG_SHORT = 16u };
typedef struct {
    uint8_t note, fl;                    /* the note heard (0: none), BLG_* */
} bl_gen_t;
static uint32_t bl_hash(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    return a ^ a >> 16;
}
static uint32_t bl_near(const bl_line_t *l, uint32_t n, int up)   /* the line's next note above (below) n */
{
    uint32_t i, best = 0u;
    for (i = 0; i < BL_STEPS; i++) {
        uint32_t v = bl_note_of(l, i);
        if (v && (up ? v > n && (!best || v < best) : v < n && v > best))
            best = v;
    }
    return best ? best : up ? (n + 7u > 96u ? n - 5u : n + 7u) : (n < 29u ? n + 7u : n - 5u);
}
static uint32_t bl_gen_step(const bl_line_t *l, uint32_t line, uint32_t pos, bl_gen_t *g)
{
    uint32_t core = bl_gen[0], v = bl_gen[1], r = bl_gen[2], f = bl_gen[3], b = (line * BL_STEPS + pos) * 8u;
    uint32_t h1 = bl_hash(b + 1u) % 100u, h2 = bl_hash(b + 2u) % 100u, h3 = bl_hash(b + 3u), h4 = bl_hash(b + 4u) % 100u;
    uint32_t n = bl_note_of(l, pos), i;
    pos %= BL_STEPS;
    g->note = (uint8_t)n;
    g->fl = (uint8_t)((l->st[pos].fl & BLF_ACC ? BLG_ACC : 0u) | (l->st[pos].fl & BLF_SLD ? BLG_SLD : 0u));
    if (core >= 100u || h1 < core)       /* (CORE: as recorded) */
        return n;
    if (pos >= 16u) {                    /* (FILL: the second bar more) */
        v = v + f > 100u ? 100u : v + f;
        r = r + f / 2u > 100u ? 100u : r + f / 2u;
    }
    if (!n) {                            /* an empty step: a short note, the line's last before it */
        if (h2 >= v / 2u)
            return 0u;
        for (i = 1u; i < BL_STEPS && !n; i++)
            n = bl_note_of(l, (pos + BL_STEPS - i) % BL_STEPS);
        if (n && h3 >> 9 & 1u && n + 12u <= 96u)
            n += 12u;
        g->note = (uint8_t)n;
        g->fl = (uint8_t)(n ? BLG_VAR | BLG_SHORT : 0u);
        return n;
    }
    if (h2 < v) {                        /* VARIATION */
        uint32_t kind = h3 % 3u, upw = h3 >> 4 & 1u;
        if (kind == 2u)
            return 0u;                   /* (a rest) */
        if (kind == 0u)                  /* (an octave) */
            n = upw ? (n + 12u <= 96u ? n + 12u : n - 12u) : (n >= 36u ? n - 12u : n + 12u);
        else                             /* (the line's neighbouring note) */
            n = bl_near(l, n, (int)upw);
        g->note = (uint8_t)n;
        g->fl |= BLG_VAR;
    }
    if (h4 < r)                          /* ROLL: a ratchet or a slide */
        g->fl |= h3 >> 8 & 1u ? BLG_RAT : BLG_SLD;
    return n;
}

/* render: the block's steps (brk_step_events') into the bass's notes, the rolls' and the build's hits rh[0..nr-1]
 * (rising) its last note again, then the bass into out (overwritten) */
static void bl_render(float *out, uint32_t m, const uint16_t *off, const uint8_t *idx, uint32_t ne, int playing,
                      const uint16_t *rh, uint32_t nr)
{
    static uint8_t was_playing, live_was, rat_on;
    static uint32_t rat_due;             /* (a ratchet's second hit: samples from this block's start) */
    static bass_note_t rat_nt;
    uint16_t at[16];
    bass_note_t nt[16];
    uint32_t k, nn = 0u, r = 0u;
    bl_non = 0u;
    bass_set_mod(bl_mod(bl_at(bl_play)), brk_step_len_samples());   /* (MOD: the line's, at the tempo) */
    if (bl_snd_new) {
        bass_t b;
        bl_snd_new = 0u;
        b = bl_at(bl_play)->snd;
        b.volume *= bls.vol;             /* (the BASS's fader) */
        if (bl_sw & 1u)                  /* (DST: pushed) */
            b.p[BS_DRIVE] = b.p[BS_DRIVE] < 0.85f ? 0.85f : b.p[BS_DRIVE];
        if (bl_sw & 2u)                  /* (DRV: none) */
            b.p[BS_DRIVE] = 0.0f;
        if (bl_sw & 4u)                  /* (SUB: none) */
            b.p[BS_SUB] = 0.0f;
        bass_set(&b);
    }
    if (bl_live_up) {                    /* the key let go */
        bl_live_up = 0u;
        bass_gate_off();
    }
    if (bl_live_new) {                   /* a key down: at once */
        bl_live_new = 0u;
        at[nn] = 0u;
        nt[nn].note = bl_live_note;
        nt[nn].accent = 0u;
        nt[nn].slide = 0u;
        nt[nn].rsv = 0u;
        nt[nn].gate = 0u;
        nn++;
    }
    if (rat_on && (!playing || bl_live_held || bl_mute))
        rat_on = 0u;
    if (rat_on && rat_due < m) {         /* (the ratchet's second hit, due in this block) */
        at[nn] = (uint16_t)rat_due;
        nt[nn++] = rat_nt;
        rat_on = 0u;
    }
    if (playing && !was_playing)         /* started: the line from its first step */
        bl_org = ne ? (uint8_t)(idx[0] & 0xF0u) : 0u;
    if (!playing && was_playing && !bl_live_held)
        bass_gate_off();                 /* stopped: a note held (a slide) let go */
    if (live_was && !bl_live_held)
        live_was = 0u;
    for (k = 0; k < ne && playing; k++) {
        const bl_line_t *l;
        uint32_t pos, note, nx;
        if ((idx[k] & 15u) == 0u && bls.sel != bl_play) {   /* a bar's first step: the line picked comes in */
            bass_t b;
            bl_play = bls.sel % BL_ALL;
            bl_org = idx[k];
            bl_snd_new = 1u;             /* (its sound, switches and all, at the next block) */
            b = bl_at(bl_play)->snd;
            b.volume *= bls.vol;
            bass_set(&b);
        }
        pos = (uint32_t)(idx[k] - bl_org) & (BL_STEPS - 1u);
        bl_pos = (uint8_t)pos;
        if (pos == 0u || pos == 16u)     /* (the LFO on the bar) */
            bass_lfo_reset(off[k]);
        bl_steps_n = bl_steps_n + 1u;
        while (r < nr && rh[r] < off[k] && nn < 15u && bl_last && !bl_live_held) {   /* (the rolls' hits before it) */
            at[nn] = rh[r++];
            nt[nn].note = bl_last;
            nt[nn].accent = 1u;
            nt[nn].slide = nt[nn].rsv = 0u;
            nt[nn].gate = brk_step_len_samples() / 3u;
            nn++;
        }
        if (bl_live_held || bl_mute || nn >= 15u)   /* (a key held: the line quiet under it; KIL: quiet) */
            continue;
        l = bl_at(bl_play);
        {
            bl_gen_t g;                  /* (BABSZTYL: the GENERATOR's, as played) */
            uint32_t sl = brk_step_len_samples();
            note = bl_gen_step(l, bl_play, pos, &g);
            if (!note)
                continue;
            if (bl_fx & 1u && note <= 96u)   /* (X2: an octave up) */
                note += 12u;
            bl_last = (uint8_t)note;
            nx = (pos + 1u) % BL_STEPS;
            at[nn] = off[k];
            nt[nn].note = (uint8_t)note;
            nt[nn].accent = (uint8_t)(g.fl & BLG_ACC || bl_fx & 2u ? 1u : 0u);
            nt[nn].slide = (uint8_t)(g.fl & BLG_SLD && !(g.fl & BLG_RAT) && l->st[nx].note ? 1u : 0u);   /* (into a rest: none) */
            nt[nn].rsv = 0u;
            nt[nn].gate = g.fl & BLG_RAT ? sl * 2u / 5u : g.fl & BLG_SHORT ? sl / 3u
                        : g.fl & BLG_SLD ? sl : sl * 11u / 20u;   /* (a slide into a rest: the whole step) */
            if (g.fl & BLG_RAT) {        /* (a ratchet: the note again half a step on) */
                rat_nt = nt[nn];
                rat_nt.gate = sl / 4u;
                rat_due = off[k] + sl / 2u;
                rat_on = 1u;
            }
            if (bl_non < 8u)
                bl_on[bl_non++] = off[k];
            nn++;
            if (rat_on && rat_due < m && nn < 15u) {
                at[nn] = (uint16_t)rat_due;
                nt[nn++] = rat_nt;
                rat_on = 0u;
            }
        }
    }
    while (r < nr && nn < 16u && bl_last && !bl_live_held && playing) {   /* (the rolls' hits after the steps) */
        at[nn] = rh[r++];
        nt[nn].note = bl_last;
        nt[nn].accent = 1u;
        nt[nn].slide = nt[nn].rsv = 0u;
        nt[nn].gate = brk_step_len_samples() / 3u;
        nn++;
    }
    live_was = bl_live_held;
    was_playing = (uint8_t)(playing != 0);
    if (rat_on)
        rat_due -= m;                    /* (the next block's) */
    for (k = 1u; k < nn; k++) {          /* (rising, the ratchets among the rest) */
        uint32_t j;
        for (j = k; j > 0u && at[j - 1u] > at[j]; j--) {
            uint16_t ta = at[j];
            bass_note_t tn = nt[j];
            at[j] = at[j - 1u];
            nt[j] = nt[j - 1u];
            at[j - 1u] = ta;
            nt[j - 1u] = tn;
        }
    }
    bass_render(out, m, at, nt, nn);
}
