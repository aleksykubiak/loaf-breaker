/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS (bass.h): one monophonic voice, an oscillator, a four-pole low-pass and two envelopes. */
#include "bass.h"
#include "brk.h"
#include "brk_math.h"

const char *const BASS_PARAM_NAME[BS_PARAMS] = {
    "WAVE", "SUB", "CUTOFF", "RESO", "ENV", "DECAY", "ACCENT", "GLIDE", "DRIVE", "SUSTAIN", "KEYTRK",
};

const char *const BASS_PRESET_NAME[BASS_PRESETS] = { "ACID", "SQUELCH", "GABBER", "SUB", "PLUCK", "FAT" };
/*                                         WAVE  SUB   CUT   RESO  ENV   DECAY ACC   GLIDE DRIVE SUS   KEYTRK */
static const float PRESET[BASS_PRESETS][BS_PARAMS] = {
    /* ACID    */ { 0.00f, 0.15f, 0.30f, 0.70f, 0.60f, 0.35f, 0.60f, 0.35f, 0.25f, 0.00f, 0.30f },
    /* SQUELCH */ { 0.00f, 0.10f, 0.18f, 0.88f, 0.80f, 0.25f, 0.80f, 0.40f, 0.55f, 0.00f, 0.30f },
    /* GABBER  */ { 1.00f, 0.40f, 0.45f, 0.35f, 0.40f, 0.30f, 0.50f, 0.20f, 0.90f, 0.30f, 0.50f },
    /* SUB     */ { 0.00f, 0.90f, 0.12f, 0.10f, 0.15f, 0.60f, 0.20f, 0.30f, 0.15f, 0.80f, 0.60f },
    /* PLUCK   */ { 0.50f, 0.30f, 0.25f, 0.40f, 0.70f, 0.15f, 0.40f, 0.15f, 0.20f, 0.00f, 0.50f },
    /* FAT     */ { 0.70f, 0.50f, 0.40f, 0.50f, 0.45f, 0.50f, 0.50f, 0.30f, 0.60f, 0.60f, 0.40f },
};

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

static struct {
    bass_t set, next;
    volatile uint32_t pending;
    /* derived from set */
    float fdec, fdec_acc, glide_k, amp_dec, sus;
    /* the voice */
    float ph, sph, note, target, s[4], y4, fenv, aenv, acc;
    uint32_t gate_left, gated, playing, tie;
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
    B.gate_left = B.gated = B.playing = B.tie = 0u;
}

void bass_set(const bass_t *b)
{
    B.next = *b;
    B.pending = 1u;
}

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
    if (B.pending) {
        B.set = B.next;
        B.pending = 0u;
        derive();
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
        /* the filter */
        e = B.fenv * (envamt + B.acc * acc_amt * 2.5f);
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
    }
    while (k < nn)                       /* (offsets past the block: started at its end) */
        start(&nt[k++]);
}
