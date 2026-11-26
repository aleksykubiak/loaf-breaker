/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS (bass.h): one monophonic voice, an oscillator, a four-pole low-pass and two envelopes. */
#include "bass.h"
#include "brk.h"
#include "brk_math.h"

const char *const BASS_PARAM_NAME[BS_PARAMS] = {
    "WAVE", "SUB", "CUTOFF", "RESO", "ENV", "DECAY", "ACCENT", "GLIDE", "DRIVE", "SUSTAIN", "KEYTRK",
};

const char *const BASS_PRESET_NAME[BASS_PRESETS] = { "ACID", "SQUELCH", "GABBER", "SUB", "PLUCK", "FAT",
                                                     "DUB SUB", "RUBBER", "STAB", "TECHNO", "SCREAM 303", "WARM",
                                                     "BLIP", "REESE", "HOOVER", "SUPERSAW", "WOBBLE 1/4",
                                                     "WOBBLE 1/8", "WOBBLE 1/16", "YOI" };
const char *const BASS_RATE_NAME[BASS_RATES] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T" };
static const float RATE16[BASS_RATES] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 2.6667f, 1.3333f, 0.6667f };   /* 16ths a cycle */
/*                                         WAVE  SUB   CUT   RESO  ENV   DECAY ACC   GLIDE DRIVE SUS   KEYTRK */
static const float PRESET[BASS_PRESETS][BS_PARAMS] = {
    /* ACID    */ { 0.00f, 0.15f, 0.30f, 0.70f, 0.60f, 0.35f, 0.60f, 0.35f, 0.25f, 0.00f, 0.30f },
    /* SQUELCH */ { 0.00f, 0.10f, 0.18f, 0.88f, 0.80f, 0.25f, 0.80f, 0.40f, 0.55f, 0.00f, 0.30f },
    /* GABBER  */ { 1.00f, 0.40f, 0.45f, 0.35f, 0.40f, 0.30f, 0.50f, 0.20f, 0.90f, 0.30f, 0.50f },
    /* SUB     */ { 0.00f, 0.90f, 0.12f, 0.10f, 0.15f, 0.60f, 0.20f, 0.30f, 0.15f, 0.80f, 0.60f },
    /* PLUCK   */ { 0.50f, 0.30f, 0.25f, 0.40f, 0.70f, 0.15f, 0.40f, 0.15f, 0.20f, 0.00f, 0.50f },
    /* FAT     */ { 0.70f, 0.50f, 0.40f, 0.50f, 0.45f, 0.50f, 0.50f, 0.30f, 0.60f, 0.60f, 0.40f },
    /* DUB SUB */ { 0.00f, 0.95f, 0.08f, 0.05f, 0.10f, 0.70f, 0.20f, 0.30f, 0.10f, 0.90f, 0.50f },
    /* RUBBER  */ { 1.00f, 0.30f, 0.25f, 0.60f, 0.50f, 0.20f, 0.50f, 0.45f, 0.30f, 0.20f, 0.40f },
    /* STAB    */ { 0.00f, 0.20f, 0.22f, 0.45f, 0.85f, 0.10f, 0.90f, 0.10f, 0.45f, 0.00f, 0.40f },
    /* TECHNO  */ { 1.00f, 0.40f, 0.15f, 0.30f, 0.30f, 0.55f, 0.30f, 0.20f, 0.35f, 0.50f, 0.40f },
    /* SCRM303 */ { 0.00f, 0.10f, 0.22f, 0.97f, 0.85f, 0.30f, 0.90f, 0.40f, 0.95f, 0.00f, 0.30f },
    /* WARM    */ { 0.00f, 0.50f, 0.20f, 0.10f, 0.20f, 0.60f, 0.20f, 0.30f, 0.15f, 0.70f, 0.50f },
    /* BLIP    */ { 0.50f, 0.10f, 0.50f, 0.50f, 0.70f, 0.14f, 0.70f, 0.05f, 0.40f, 0.15f, 0.80f },
    /* REESE   */ { 0.00f, 0.40f, 0.30f, 0.15f, 0.10f, 0.70f, 0.20f, 0.30f, 0.30f, 0.90f, 0.40f },
    /* HOOVER  */ { 0.20f, 0.30f, 0.50f, 0.20f, 0.30f, 0.50f, 0.40f, 0.60f, 0.60f, 0.80f, 0.50f },
    /* SUPERSW */ { 0.00f, 0.20f, 0.55f, 0.10f, 0.20f, 0.50f, 0.30f, 0.20f, 0.20f, 0.70f, 0.50f },
    /* WOB 1/4 */ { 0.60f, 0.60f, 0.16f, 0.55f, 0.00f, 0.50f, 0.20f, 0.30f, 0.60f, 1.00f, 0.40f },
    /* WOB 1/8 */ { 0.60f, 0.60f, 0.16f, 0.55f, 0.00f, 0.50f, 0.20f, 0.30f, 0.60f, 1.00f, 0.40f },
    /* WOB1/16 */ { 0.60f, 0.60f, 0.16f, 0.55f, 0.00f, 0.50f, 0.20f, 0.30f, 0.60f, 1.00f, 0.40f },
    /* YOI     */ { 0.00f, 0.30f, 0.18f, 0.85f, 0.20f, 0.50f, 0.40f, 0.30f, 0.70f, 0.90f, 0.40f },
};
static const uint16_t PRESET_MOD[BASS_PRESETS] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    BASS_MOD(4u, 15u, 0u, 0u),           /* REESE: two saws a little apart, slow beating */
    BASS_MOD(10u, 12u, 0u, 0u),          /* HOOVER: wider, the glide long */
    BASS_MOD(13u, 15u, 0u, 0u),          /* SUPERSAW: wide */
    BASS_MOD(3u, 8u, 2u, 28u), BASS_MOD(3u, 8u, 3u, 28u), BASS_MOD(3u, 8u, 4u, 26u),   /* WOBBLE 1/4, 1/8, 1/16 */
    BASS_MOD(2u, 10u, 4u, 18u),          /* YOI: the resonance swept fast */
};
uint16_t bass_preset_mod(uint32_t i) { return i < BASS_PRESETS ? PRESET_MOD[i] : 0u; }
float bass_detune_st(uint32_t det) { return (float)(det & 15u) * (0.6f / 15.0f); }

void bass_preset(bass_t *b, uint32_t i)
{
    uint32_t k;
    if (i >= BASS_PRESETS)
        i = 0u;
    for (k = 0; k < BS_PARAMS; k++)
        b->p[k] = PRESET[i][k];
}

void bass_defaults(bass_t *b)
{
    bass_preset(b, 0u);
    b->volume = 0.8f;
}

float bass_cutoff_hz(float v) { return 40.0f * brk_exp2f_c(brk_clampf(v, 0.0f, 1.0f) * 8.23f); }   /* 40..12k */
float bass_decay_ms(float v) { return 30.0f * brk_exp2f_c(brk_clampf(v, 0.0f, 1.0f) * 6.06f); }    /* 30..2000 */
float bass_glide_ms(float v) { return 5.0f * brk_exp2f_c(brk_clampf(v, 0.0f, 1.0f) * 6.32f); }     /* 5..400 */

/* a sample's factor for a fall to -60 dB over ms */
static float fall(float ms) { return brk_exp2f_c(-9.966f / (ms * 0.001f * (float)BRK_SR)); }

BRK_INL float tanh_a(float x)            /* tanh, a rational fit, exact enough to |x| 3 and flat past it */
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

BRK_INL float blep(float t, float dt)    /* the polyBLEP step correction at phase t (0..1) */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

int8_t bass_scope[BASS_SCOPE_N];
volatile uint32_t bass_scope_at;
volatile float bass_env_now;
static uint32_t scope_dec;

static struct {
    bass_t set, next;
    volatile uint32_t pending, off;
    /* derived from set */
    float fdec, fdec_acc, glide_k, amp_dec, sus;
    /* the voice */
    float ph, sph, note, target, s[4], y4, fenv, aenv, acc;
    uint32_t gate_left, gated, playing, tie;
    /* MOD (BABSZTYL) */
    volatile uint32_t mod, step_len, lfo_rst;   /* (lfo_rst: 1 + the sample the LFO starts again at, 0 none) */
    float pa, pb, lph;                   /* the two detuned saws' phases, the LFO's */
} B;

static void derive(void)
{
    const float *p = B.set.p;
    float dms = bass_decay_ms(p[BS_DECAY]);
    B.fdec = fall(dms);
    B.fdec_acc = fall(dms * (1.0f - 0.6f * p[BS_ACCENT]));   /* the accent's decay shorter */
    B.amp_dec = fall(dms * 3.0f + 60.0f);
    B.sus = p[BS_SUSTAIN];
    B.glide_k = 1.0f - fall(bass_glide_ms(p[BS_GLIDE]) * 0.5f);
}

void bass_init(void)
{
    uint32_t i;
    bass_defaults(&B.set);
    B.next = B.set;
    B.pending = 0u;
    derive();
    B.ph = B.sph = 0.0f;
    for (i = 0; i < 4u; i++)
        B.s[i] = 0.0f;
    B.y4 = B.fenv = B.aenv = B.acc = 0.0f;
    B.note = B.target = 36.0f;
    B.gate_left = B.gated = B.playing = B.tie = B.off = 0u;
    B.mod = 0u;
    B.step_len = 5000u;
    B.lfo_rst = 0u;
    B.pa = 0.33f;
    B.pb = 0.71f;
    B.lph = 0.0f;
}

void bass_set(const bass_t *b)
{
    B.next = *b;
    B.pending = 1u;
}

void bass_gate_off(void) { B.off = 1u; }
void bass_set_mod(uint32_t mod, uint32_t step_len)
{
    B.mod = mod;
    B.step_len = step_len ? step_len : 1u;
}
void bass_lfo_reset(uint32_t at) { B.lfo_rst = at + 1u; }

void bass_panic(void)
{
    uint32_t i;
    B.aenv = B.fenv = 0.0f;
    B.gated = B.playing = B.tie = 0u;
    for (i = 0; i < 4u; i++)
        B.s[i] = 0.0f;
    B.y4 = 0.0f;
}

static void start(const bass_note_t *n)
{
    B.target = (float)(n->note > 127u ? 127u : n->note);
    if (B.tie && B.playing) {            /* the step before slid into this one: the pitch glides, no attack */
        B.acc = n->accent ? 1.0f : 0.0f;
    } else {
        B.note = B.target;
        B.fenv = 1.0f;
        B.acc = n->accent ? 1.0f : 0.0f;
        B.playing = 1u;
    }
    B.gated = 1u;
    B.tie = n->slide ? 1u : 0u;
    B.gate_left = n->slide ? 0u : n->gate;   /* a slide holds until the next note */
}

void bass_render(float *out, uint32_t n, const uint16_t *at, const bass_note_t *nt, uint32_t nn)
{
    const float *p;
    uint32_t i, k = 0u;
    float wave, sub, cut, reso, envamt, acc_amt, drive, kt, dg, dnorm;
    float rr, mix2, ldt, ldepth;          /* (MOD: the detune's ratio, OSC 2's level, the LFO's step, its depth) */
    uint32_t mod = B.mod, lrst = B.lfo_rst;
    if (B.pending) {
        B.set = B.next;
        B.pending = 0u;
        derive();
    }
    if (B.off) {                         /* a key let go: the release (a slide held let go too) */
        B.off = 0u;
        B.gated = 0u;
        B.tie = 0u;
    }
    p = B.set.p;
    wave = p[BS_WAVE];
    sub = p[BS_SUB];
    cut = bass_cutoff_hz(p[BS_CUTOFF]);
    reso = p[BS_RESO] * 3.9f;            /* (4: self-oscillation) */
    envamt = p[BS_ENV] * 6.0f;           /* octaves */
    acc_amt = p[BS_ACCENT];
    drive = p[BS_DRIVE];
    kt = p[BS_KEYTRK];
    dg = 1.0f + drive * 11.0f;
    dnorm = 1.0f / tanh_a(dg * 0.5f + 0.5f);
    rr = brk_exp2f_c(bass_detune_st(mod) * (1.0f / 12.0f));
    mix2 = (float)(mod >> 4 & 15u) * (1.0f / 15.0f);
    ldepth = (float)(mod >> 11 & 31u) * (4.0f / 31.0f);   /* (octaves, 0..4) */
    ldt = 1.0f / (RATE16[(mod >> 8 & 7u) % BASS_RATES] * (float)B.step_len);
    B.lfo_rst = 0u;
    for (i = 0; i < n; i++) {
        float hz, dt, saw, sq, t2, osc, fc, g, G, u, x, y, e, lvl;
        uint32_t j;
        while (k < nn && at[k] <= i)
            start(&nt[k++]);
        if (B.gated && B.gate_left) {    /* the gate's end: the release */
            if (--B.gate_left == 0u)
                B.gated = 0u;
        }
        /* the envelopes */
        B.fenv *= B.acc > 0.5f ? B.fdec_acc : B.fdec;
        if (B.gated) {
            if (B.playing == 1u) {       /* the attack, ~2 ms */
                B.aenv += (1.0f - B.aenv) * 0.012f;
                if (B.aenv > 0.98f)
                    B.playing = 2u;
            } else
                B.aenv = B.sus + (B.aenv - B.sus) * B.amp_dec;
        } else
            B.aenv *= 0.9985f;           /* the release, ~30 ms to -60 dB */
        if (B.aenv < 1e-5f && !B.gated) {
            B.aenv = 0.0f;
            B.playing = 0u;
        }
        /* the pitch */
        B.note += (B.target - B.note) * B.glide_k;
        hz = 440.0f * brk_exp2f((B.note - 69.0f) * (1.0f / 12.0f));
        dt = hz * (1.0f / (float)BRK_SR);
        /* the oscillators */
        B.ph += dt;
        if (B.ph >= 1.0f)
            B.ph -= 1.0f;
        B.sph += dt * 0.5f;
        if (B.sph >= 1.0f)
            B.sph -= 1.0f;
        saw = 2.0f * B.ph - 1.0f - blep(B.ph, dt);
        t2 = B.ph + 0.5f;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        sq = (B.ph < 0.5f ? 1.0f : -1.0f) + blep(B.ph, dt) - blep(t2, dt);
        osc = saw + (sq - saw) * wave;
        if (mix2 > 0.0f) {               /* (MOD: two saws, one above, one below) */
            float da = dt * rr, db = dt / rr, sa, sb;
            B.pa += da;
            if (B.pa >= 1.0f)
                B.pa -= 1.0f;
            B.pb += db;
            if (B.pb >= 1.0f)
                B.pb -= 1.0f;
            sa = 2.0f * B.pa - 1.0f - blep(B.pa, da);
            sb = 2.0f * B.pb - 1.0f - blep(B.pb, db);
            osc = osc * (1.0f - 0.45f * mix2) + (sa + sb) * 0.5f * mix2;
        }
        if (lrst && i + 1u == lrst)      /* (the LFO from its start: the line's bar) */
            B.lph = 0.0f;
        B.lph += ldt;
        if (B.lph >= 1.0f)
            B.lph -= 1.0f;
        /* the filter */
        e = B.fenv * (envamt + B.acc * acc_amt * 2.5f);
        if (ldepth > 0.0f)               /* (the LFO: the cutoff up and down, from where it is) */
            e += ldepth * (0.5f - 0.5f * brk_sin_turns(B.lph + 0.25f));
        fc = cut * brk_exp2f(e + kt * (B.note - 48.0f) * (1.0f / 12.0f));
        if (fc > 0.42f * (float)BRK_SR)
            fc = 0.42f * (float)BRK_SR;
        g = brk_tan_pi(fc * (1.0f / (float)BRK_SR));
        G = g / (1.0f + g);
        x = osc * (1.0f + reso * 0.35f);  /* (the passband's loss under resonance, made up) */
        u = tanh_a(x - reso * B.y4);
        for (j = 0; j < 4u; j++) {
            float v = (u - B.s[j]) * G;
            y = v + B.s[j];
            B.s[j] = y + v;
            u = y;
        }
        B.y4 = u;
        /* the sub under the filter, the amp, the drive */
        lvl = B.aenv * (1.0f + B.acc * acc_amt * 0.6f);
        y = (u + sub * brk_sin_turns(B.sph) * 1.2f) * lvl * 0.5f;
        y = tanh_a(y * dg) * dnorm;
        out[i] = y * B.set.volume;
        if (++scope_dec >= BASS_SCOPE_DEC) {
            float q = y * 127.0f;
            scope_dec = 0u;
            bass_scope[bass_scope_at % BASS_SCOPE_N] = (int8_t)(q > 127.0f ? 127.0f : q < -127.0f ? -127.0f : q);
            bass_scope_at = bass_scope_at + 1u;
        }
    }
    bass_env_now = B.aenv;
    while (k < nn)                       /* (offsets past the block: started at its end) */
        start(&nt[k++]);
}
