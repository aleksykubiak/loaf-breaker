/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS: a monophonic bass voice in the kick's place (kick.h), played by the pattern's bass lane (a note, an
 * ACCENT and a SLIDE on every step). One voice, made for acid, hardcore and sub lines:
 *   OSC    a saw and a square (polyBLEP, WAVE blends them) and a sine SUB an octave down
 *   FILTER a four-pole resonant low-pass (four TPT one-poles, a tanh in the loop): CUTOFF, RESO, ENV (how far the
 *          filter envelope opens it), DECAY (that envelope's), KEYTRK
 *   AMP    a fast attack, then a decay towards SUSTAIN while the gate is held; a release after it
 *   ACCENT louder, the filter opened further and its decay shorter (the 303's accent), by ACCENT
 *   SLIDE  a step marked SLIDE ties into the next one: no new attack, the pitch gliding over GLIDE
 *   DRIVE  a soft clip after the filter, then the volume
 *   MOD    (BABSZTYL) two more saws detuned either side (DETUNE, OSC 2: the REESE, the HOOVER, a SUPERSAW) and an LFO
 *          on the filter at the tempo (RATE 1/1 .. 1/16T, DEPTH: the WOBBLE, the YOI): bass_set_mod
 * Platform-free like the kick and the drums: settings from the main loop (bass_set, read once a block), notes at
 * sample offsets in the render. */
#pragma once
#include <stdint.h>

enum { BS_WAVE, BS_SUB, BS_CUTOFF, BS_RESO, BS_ENV, BS_DECAY, BS_ACCENT, BS_GLIDE, BS_DRIVE, BS_SUSTAIN,
       BS_KEYTRK, BS_PARAMS };
extern const char *const BASS_PARAM_NAME[BS_PARAMS];

typedef struct {
    float p[BS_PARAMS];                  /* 0..1 each, BS_* */
    float volume;                        /* 0..1: the mixer's BASS fader */
} bass_t;

#define BASS_PRESETS 20u                 /* (BABSZTYL: 6 then 14 more) */
extern const char *const BASS_PRESET_NAME[BASS_PRESETS];
void bass_preset(bass_t *b, uint32_t i); /* a built-in sound (the volume kept) */
/* MOD, packed (a line's 16 bits): bits 0-3 DETUNE (0..15), 4-7 OSC 2 (0..15, 0: none), 8-10 the LFO's RATE (BASS_RATES),
 * 11-15 its DEPTH (0..31, 0: none) */
#define BASS_MOD(det, mix, rate, depth) ((uint16_t)((det) | (mix) << 4 | (rate) << 8 | (depth) << 11))
#define BASS_RATES 8u
extern const char *const BASS_RATE_NAME[BASS_RATES];
uint16_t bass_preset_mod(uint32_t i);    /* a built-in sound's MOD */
float bass_detune_st(uint32_t det);      /* DETUNE's level in semitones (0 .. 0.6) */
void bass_defaults(bass_t *b);           /* ACID, volume 0.8 */
float bass_cutoff_hz(float v);           /* CUTOFF's knob in Hz (40 .. 12 000, exponential) */
float bass_decay_ms(float v);            /* DECAY's knob in ms (30 .. 2000) */
float bass_glide_ms(float v);            /* GLIDE's knob in ms (5 .. 400) */

/* a step's note: its MIDI note, its gate (how long it is held, in samples from its start; 0: until the next note),
 * ACCENT and SLIDE (it ties into the next note) */
typedef struct {
    uint8_t note, accent, slide, rsv;
    uint32_t gate;
} bass_note_t;

void bass_init(void);
void bass_set(const bass_t *b);          /* main loop: read at the next render */
void bass_set_mod(uint32_t mod, uint32_t step_len);   /* the render's: MOD (packed) and a 16th's length in samples */
void bass_lfo_reset(uint32_t at);        /* the render's: the LFO from its start at sample at of the next block */
void bass_panic(void);                   /* the voice silenced at once (stop) */
void bass_gate_off(void);                /* main loop: the note held let go at the next render (a key up) */
/* the screen's picture: the output, every BASS_SCOPE_DEC-th sample, in a ring of BASS_SCOPE_N (-127..127);
 * bass_scope_at the next one written */
#define BASS_SCOPE_N 512u
#define BASS_SCOPE_DEC 4u
extern int8_t bass_scope[BASS_SCOPE_N];
extern volatile uint32_t bass_scope_at;
extern volatile float bass_env_now;      /* the amp envelope, 0..1 (the screen's) */
/* render: n samples into out (overwritten); notes nt[0..nn-1] starting at the sample offsets at[0..nn-1] (rising) */
void bass_render(float *out, uint32_t n, const uint16_t *at, const bass_note_t *nt, uint32_t nn);
