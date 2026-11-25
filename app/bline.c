/* SPDX-License-Identifier: MPL-2.0 */
/* BASS LINES (included by app.c, before the render): eight lines of two bars (32 steps) for the BASS (dsp/bass.c),
 * each with its own sound. SEQ records and edits them, EDIT shapes the sound of the one selected (ui/bass.c).
 *   - a step: a note (0: a rest), ACCENT, SLIDE (it glides into the next step's note; into the same note: a tie)
 *   - the line selected is the line playing: picked while playing, it comes in at the next bar, from its first
 *     step (a live change of lines, on the bar); picked stopped, at once
 *   - the steps are the break's (brk_step_events): the tempo and the swing (PATTERN › GROOVE) are theirs
 *   - the keys played on SEQ and EDIT: the bass at once (last note first); while one is held the line is quiet
 * Kept in their own store object, OBJ_BASS (the room of KICK 11..20's memories, km2: the KICK menu is no longer
 * EDIT's, those memories start as the built-in kicks at each power-on). */

#define BL_N 8u                          /* lines */
#define BL_STEPS 32u                     /* two bars of 16ths */
#define BL_MAGIC 0x31534142u             /* "BAS1" */
#define OBJ_BASS OBJ_KMEM2
enum { BLF_ACC = 1u, BLF_SLD = 2u };
typedef struct {
    uint8_t note, fl;                    /* note: MIDI (0: a rest); fl: BLF_* */
} bl_step_t;
typedef struct {
    bl_step_t st[BL_STEPS];
    bass_t snd;                          /* its sound (EDIT), its volume the line's LEVEL */
    int8_t trans;                        /* TRANSPOSE, -12 .. +12 semitones */
    uint8_t preset;                      /* the built-in sound last put on it (EDIT's PRESETS) */
    uint8_t rsv[2];
} bl_line_t;
typedef struct {
    uint32_t magic;
    uint8_t sel;                         /* the line selected (and playing, or coming in) */
    uint8_t oct;                         /* the keys' octave: the lowest key F of octave oct (F1 = 1) */
    uint8_t rsv[2];
    bl_line_t l[BL_N];
} bl_store_t;
_Static_assert(sizeof(bl_store_t) <= 3840u, "a store object holds 3840 bytes");
static bl_store_t bls;

/* the render's (written there, read by the UI) */
static volatile uint8_t bl_play;         /* the line playing */
static volatile uint8_t bl_pos;          /* its step playing, 0..31 */
static volatile uint32_t bl_steps_n;     /* steps played (the screen follows) */
static uint8_t bl_org;                   /* the step (brk_step_events' 0..63) the line playing began on */
/* the main loop's, read by the render */
static volatile uint8_t bl_live_note, bl_live_held, bl_live_new, bl_live_up;
static volatile uint8_t bl_snd_new;      /* a sound changed: the render sends it to the engine */

static int bl_empty(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < BL_STEPS; i++)
        if (bls.l[n % BL_N].st[i].note)
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

static void bl_line_clear(uint32_t n)
{
    memset(bls.l[n % BL_N].st, 0, sizeof bls.l[0].st);
}

static void bl_line_default(uint32_t n)
{
    bl_line_t *l = &bls.l[n % BL_N];
    uint32_t i;
    memset(l, 0, sizeof *l);
    bass_defaults(&l->snd);
    bass_preset(&l->snd, BL_DEF_SND[n % BL_N]);
    l->preset = BL_DEF_SND[n % BL_N];
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
    bls.oct = 1u;
    for (n = 0; n < BL_N; n++)
        bl_line_default(n);
    bl_play = 0u;
    bl_snd_new = 1u;
}

static void bl_load(void)
{
    if (plat_store_load(OBJ_BASS, &bls, sizeof bls) != (int)sizeof bls || bls.magic != BL_MAGIC)
        bl_defaults();
    bls.sel %= BL_N;
    if (bls.oct > 4u)
        bls.oct = 1u;
    bl_play = bls.sel;
    bl_snd_new = 1u;
}

static int bl_save(void) { return plat_store_save(OBJ_BASS, &bls, sizeof bls) == 0; }

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

/* render: the block's steps (brk_step_events') into the bass's notes, then the bass into out (overwritten) */
static void bl_render(float *out, uint32_t m, const uint16_t *off, const uint8_t *idx, uint32_t ne, int playing)
{
    static uint8_t was_playing, live_was;
    uint16_t at[8];
    bass_note_t nt[8];
    uint32_t k, nn = 0u;
    if (bl_snd_new) {
        bl_snd_new = 0u;
        bass_set(&bls.l[bl_play % BL_N].snd);
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
            bl_play = bls.sel % BL_N;
            bl_org = idx[k];
            bass_set(&bls.l[bl_play].snd);
        }
        pos = (uint32_t)(idx[k] - bl_org) & (BL_STEPS - 1u);
        bl_pos = (uint8_t)pos;
        bl_steps_n = bl_steps_n + 1u;
        if (bl_live_held || nn >= 8u)    /* (a key held: the line quiet under it) */
            continue;
        l = &bls.l[bl_play % BL_N];
        note = bl_note_of(l, pos);
        if (!note)
            continue;
        nx = (pos + 1u) % BL_STEPS;
        at[nn] = off[k];
        nt[nn].note = (uint8_t)note;
        nt[nn].accent = (uint8_t)(l->st[pos].fl & BLF_ACC ? 1u : 0u);
        nt[nn].slide = (uint8_t)(l->st[pos].fl & BLF_SLD && l->st[nx].note ? 1u : 0u);   /* (into a rest: none) */
        nt[nn].rsv = 0u;
        nt[nn].gate = l->st[pos].fl & BLF_SLD ? brk_step_len_samples()   /* (a slide into a rest: the whole step) */
                                              : brk_step_len_samples() * 11u / 20u;
        nn++;
    }
    live_was = bl_live_held;
    was_playing = (uint8_t)(playing != 0);
    bass_render(out, m, at, nt, nn);
}
