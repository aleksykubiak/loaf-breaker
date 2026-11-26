/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS's tests (app/dsp/bass.c), and a demo: an acid line over a sequenced kit (the kick, the snare, the hat,
 * every bass preset in turn. tests/engine/run_bass.sh [OUTDIR] */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bass.h"
#include "brk.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define BLK 64u
static float buf[BRK_SR * 20u];

static void wav(const char *path, const float *x, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, v, bytes = n * 2u;
    uint16_t a;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16u; fwrite(&v, 4, 1, f);
    a = 1; fwrite(&a, 2, 1, f); a = 1; fwrite(&a, 2, 1, f);
    v = BRK_SR; fwrite(&v, 4, 1, f); v = BRK_SR * 2u; fwrite(&v, 4, 1, f);
    a = 2; fwrite(&a, 2, 1, f); a = 16; fwrite(&a, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < n; i++) {
        float s = x[i] > 1.0f ? 1.0f : x[i] < -1.0f ? -1.0f : x[i];
        int16_t q = (int16_t)(s * 32000.0f);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
}

/* a held note, the filter open: its pitch from its zero crossings */
static void test_pitch(void)
{
    bass_t b;
    bass_note_t nt = {45u, 0u, 0u, 0u, 0u};   /* A2, 110 Hz, held */
    uint16_t at = 0u;
    uint32_t i, up = 0u, n = BRK_SR;
    bass_init();
    bass_defaults(&b);
    b.p[BS_CUTOFF] = 0.25f; b.p[BS_RESO] = 0.0f; b.p[BS_ENV] = 0.0f; b.p[BS_SUB] = 0.0f;
    b.p[BS_DRIVE] = 0.0f; b.p[BS_SUSTAIN] = 1.0f; b.p[BS_KEYTRK] = 0.0f;
    bass_set(&b);
    bass_render(buf, BLK, &at, &nt, 1u);
    for (i = BLK; i < n; i += BLK)
        bass_render(buf + i, BLK, 0, 0, 0u);
    for (i = BRK_SR / 10u; i + 1u < n; i++)
        if (buf[i] <= 0.0f && buf[i + 1u] > 0.0f)
            up++;
    {
        float hz = (float)up / 0.9f;
        CHECK(fabsf(hz - 110.0f) < 3.0f, "pitch %.1f Hz, not 110", (double)hz);
    }
}

/* every preset, the resonance at its top, notes all over the range: finite, bounded */
static void test_bounded(void)
{
    bass_t b;
    uint32_t pr, i, s;
    for (pr = 0; pr < BASS_PRESETS; pr++) {
        float peak = 0.0f;
        int finite = 1;
        bass_init();
        bass_defaults(&b);
        bass_preset(&b, pr);
        bass_set_mod(bass_preset_mod(pr), 4000u);   /* (BABSZTYL: its MOD too) */
        b.p[BS_RESO] = 1.0f;
        b.p[BS_ENV] = 1.0f;
        b.volume = 1.0f;
        bass_set(&b);
        for (s = 0; s < 64u; s++) {
            bass_note_t nt = {(uint8_t)(12u + (s * 7u) % 84u), (uint8_t)(s & 1u), (uint8_t)((s >> 1) & 1u), 0u,
                              2000u};
            uint16_t at = 3u;
            bass_render(buf, BLK, &at, &nt, 1u);
            for (i = 1u; i < 80u; i++)
                bass_render(buf + i * BLK, BLK, 0, 0, 0u);
            for (i = 0; i < 80u * BLK; i++) {
                if (!isfinite(buf[i]))
                    finite = 0;
                if (fabsf(buf[i]) > peak)
                    peak = fabsf(buf[i]);
            }
        }
        CHECK(finite, "%s: not finite", BASS_PRESET_NAME[pr]);
        CHECK(peak <= 1.05f, "%s: peak %.2f", BASS_PRESET_NAME[pr], (double)peak);
        CHECK(peak > 0.05f, "%s: silent (%.3f)", BASS_PRESET_NAME[pr], (double)peak);
    }
}

/* a gate's end: silence after the release */
static void test_release(void)
{
    bass_t b;
    bass_note_t nt = {36u, 0u, 0u, 0u, 4410u};   /* 100 ms */
    uint16_t at = 0u;
    uint32_t i;
    float tail = 0.0f;
    bass_init();
    bass_defaults(&b);
    bass_set(&b);
    bass_render(buf, BLK, &at, &nt, 1u);
    for (i = BLK; i < BRK_SR / 2u; i += BLK)
        bass_render(buf + i, BLK, 0, 0, 0u);
    for (i = BRK_SR / 2u - 2000u; i < BRK_SR / 2u; i++)
        if (fabsf(buf[i]) > tail)
            tail = fabsf(buf[i]);
    CHECK(tail < 1e-3f, "still sounding 400 ms after its gate: %.4f", (double)tail);
}

/* a slide: the second note does not start again (no new attack), its pitch gliding there */
static void test_slide(void)
{
    bass_t b;
    bass_note_t nt[2] = {{36u, 0u, 1u, 0u, 0u}, {48u, 0u, 0u, 0u, 0u}};
    uint16_t at;
    uint32_t i;
    float dip = 1.0f;
    bass_init();
    bass_defaults(&b);
    b.p[BS_SUSTAIN] = 1.0f;
    b.p[BS_DRIVE] = 0.0f;
    bass_set(&b);
    at = 0u;
    bass_render(buf, BLK, &at, &nt[0], 1u);
    for (i = BLK; i < 8820u; i += BLK)
        bass_render(buf + i, BLK, 0, 0, 0u);
    at = 0u;
    bass_render(buf + i, BLK, &at, &nt[1], 1u);
    {   /* the envelope around the joint, in 5 ms windows: no dip towards 0 (a new attack starts from where it is,
         * but the release before it would show) */
        uint32_t w, j, i0 = i;
        for (i += BLK; i < i0 + 8820u; i += BLK)
            bass_render(buf + i, BLK, 0, 0, 0u);
        for (w = i0 - 2205u; w < i0 + 4410u; w += 220u) {
            float pk = 0.0f;
            for (j = w; j < w + 220u; j++)
                if (fabsf(buf[j]) > pk)
                    pk = fabsf(buf[j]);
            if (pk < dip)
                dip = pk;
        }
    }
    CHECK(dip > 0.05f, "the slide's joint drops to %.3f", (double)dip);
}

/* the demo: 8 bars at 140 BPM, a 16-step acid line, a preset a bar (the bass alone: the drums are the break's) */
static void demo(const char *dir)
{
    static const int8_t LINE[16] = {0, 0, 12, 0, -1, 0, 15, 0, 0, 10, 0, 12, -1, 3, 0, 7};   /* -1: rest */
    static const uint8_t ACC[16] = {1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1};
    static const uint8_t SLD[16] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0};
    static const uint8_t ORDER[8] = {0u, 0u, 1u, 2u, 3u, 4u, 5u, 1u};
    char path[600];
    bass_t b;
    float bb[BLK];
    uint32_t spstep = BRK_SR * 60u / 140u / 4u, total = spstep * 16u * 8u, t, i;
    bass_init();
    bass_defaults(&b);
    for (t = 0; t < total; t += BLK) {
        uint32_t step0 = t / spstep, off = step0 * spstep;
        uint16_t ba[2];
        bass_note_t nt[2];
        uint32_t nb = 0u;
        if (off < t) {
            off += spstep;
            step0++;
        }
        if (off < t + BLK) {
            uint32_t s = step0 % 16u, bar = step0 / 16u, o = off - t;
            if (s == 0u) {
                bass_preset(&b, ORDER[bar % 8u]);
                bass_set(&b);
            }
            if (LINE[s] >= 0) {
                nt[nb].note = (uint8_t)(33 + LINE[s]);
                nt[nb].accent = ACC[s];
                nt[nb].slide = SLD[s];
                nt[nb].rsv = 0u;
                nt[nb].gate = spstep / 2u;
                ba[nb++] = (uint16_t)o;
            }
        }
        bass_render(bb, BLK, ba, nt, nb);
        for (i = 0; i < BLK && t + i < total; i++)
            buf[t + i] = 0.8f * bb[i];
    }
    snprintf(path, sizeof path, "%s/bass_demo.wav", dir);
    wav(path, buf, total);
    printf("demo: %s (%u s)\n", path, total / BRK_SR);
}

/* BABSZTYL: the presets added (DUB SUB on), two bars each at 165 BPM, a line under them; and MOD's cost */
static float pbuf[BRK_SR * 46u];
static void presets(const char *dir)
{
    static const int8_t LINE[16] = {0, -1, 0, 12, -1, 0, 3, -1, 0, 0, 12, -1, 7, -1, 5, 3};
    static const uint8_t ACC[16] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0};
    static const uint8_t SLD[16] = {0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
    char path[600];
    bass_t b;
    float bb[BLK];
    uint32_t spstep = BRK_SR * 60u / 165u / 4u, first = 6u, n = BASS_PRESETS - first, total = spstep * 32u * n, t, i;
    bass_init();
    bass_defaults(&b);
    for (t = 0; t < total; t += BLK) {
        uint32_t step0 = t / spstep, off = step0 * spstep;
        uint16_t ba[2];
        bass_note_t nt[2];
        uint32_t nb = 0u;
        if (off < t) {
            off += spstep;
            step0++;
        }
        bass_set_mod(bass_preset_mod(first + step0 / 32u), spstep);
        if (off < t + BLK) {
            uint32_t s = step0 % 16u, o = off - t;
            if (step0 % 32u == 0u) {
                bass_preset(&b, first + step0 / 32u);
                bass_set(&b);
                bass_set_mod(bass_preset_mod(first + step0 / 32u), spstep);
            }
            if (s == 0u)
                bass_lfo_reset(o);
            if (LINE[s] >= 0) {
                nt[nb].note = (uint8_t)(31 + LINE[s]);
                nt[nb].accent = ACC[s];
                nt[nb].slide = SLD[s];
                nt[nb].rsv = 0u;
                nt[nb].gate = spstep * 11u / 20u;
                ba[nb++] = (uint16_t)o;
            }
        }
        bass_render(bb, BLK, ba, nt, nb);
        for (i = 0; i < BLK && t + i < total; i++)
            pbuf[t + i] = 0.8f * bb[i];
    }
    snprintf(path, sizeof path, "%s/bass_presets.wav", dir);
    wav(path, pbuf, total);
    printf("presets: %s (%u s:", path, total / BRK_SR);
    for (i = first; i < BASS_PRESETS; i++)
        printf(" %s", BASS_PRESET_NAME[i]);
    printf(")\n");
}
static double cost(uint32_t pr, uint32_t mod)
{
    bass_t b;
    uint32_t i;
    clock_t c0;
    bass_init();
    bass_defaults(&b);
    bass_preset(&b, pr);
    bass_set(&b);
    bass_set_mod(mod, 4000u);
    c0 = clock();
    for (i = 0; i < 20u * BRK_SR / BLK; i++) {
        bass_note_t nt = {36u, 0u, 0u, 0u, 3000u};
        uint16_t at = 0u;
        bass_render(buf, BLK, &at, &nt, i % 64u == 0u ? 1u : 0u);
    }
    return (double)(clock() - c0) / CLOCKS_PER_SEC;
}
static void test_mod(void)
{
    double base = cost(0u, 0u), full = cost(15u, BASS_MOD(13u, 15u, 3u, 24u));
    printf("MOD's cost: %.0f %% of the voice without it\n", 100.0 * full / base);
    CHECK(full < base * 2.0, "MOD costs %.1fx", full / base);
}

int main(int argc, char **argv)
{
    test_pitch();
    test_bounded();
    test_release();
    test_slide();
    demo(argc > 1 ? argv[1] : ".");
    presets(argc > 1 ? argv[1] : ".");
    test_mod();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
