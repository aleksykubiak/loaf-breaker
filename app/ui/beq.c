/* SPDX-License-Identifier: MPL-2.0 */
/* The BASS's EQ (EDIT › EQ): the FX chain's EQ on the bass bus alone (brk_fx_kick_eq: LOAF BREAKER's KICK EQ, on the
 * bass now), its curve drawn as the FX page draws it. K1-K4: LOW, MID, HIGH, MID FREQ; ALGORITHM, their shape. */
static const char *const KEQ_NAME[2][4] = {{"LOW", "MID", "HIGH", "MID FREQ"}, {"LO FREQ", "MID Q", "HI FREQ", "LEVEL"}};
static void keq_value(int i, char *t)
{
    uint32_t j = (ui.keq_part & 1u) * 4u + (uint32_t)(i & 3);
    float v = set.keq[j];
    t[0] = 0;
    if (j < 3u || j == 7u) {             /* -12 .. +12 dB, flat in the middle */
        float db = (v - 0.5f) * 24.0f;
        if (db > -0.25f && db < 0.25f)
            text_cat(t, "0 DB");
        else
            signed_tenths(db, "DB", t);
    } else if (j == 5u)
        hundredths_text(0.3f * brk_exp2f(v * 3.737f), t);
    else
        hz_text(j == 3u ? 200.0f * brk_exp2f(v * 4.64f) : j == 4u ? 40.0f * brk_exp2f(v * 3.3219f)
                                                        : 2000.0f * brk_exp2f(v * 3.0f), t);
}
static void keq_turn(int i, int32_t d)
{
    uint32_t j = (ui.keq_part & 1u) * 4u + (uint32_t)(i & 3);
    char t[16];
    set.keq[j] = knob_f(set.keq[j], d, 0.02f, 0.0f, 1.0f);
    changed();
    keq_value(i, t);
    app_say(KEQ_NAME[ui.keq_part & 1u][i & 3], t);
    ui.redraw |= RG_BLOCKS;
}
static void draw_keq(void)
{
    const float *k = set.keq;
    float p[4], xx[4];
    int32_t x, y, yb, py = 0;
    uint32_t j;
    char t[16];
    for (j = 0; j < 4u; j++) {
        p[j] = k[j];
        xx[j] = k[4u + j];
    }
    cv_begin(240, 52, C_BLACK);          /* (0.9.2: the curve, then the FX section's parts as its other sections') */
    for (x = 0; x < 240; x += 4)         /* 0 dB */
        cv_rect(x, 18, 2, 1, C_LINE);
    for (x = 0; x < 240; x++) {          /* the curve, as the FX page's EQ draws it */
        float in, base, v = x_plot(FXT_EQ, x, p, xx, 0u, &in, &base);
        y = 18 - (int32_t)(brk_clampf(v, -1.0f, 1.0f) * 15.0f);
        yb = 18 - (int32_t)(base * 15.0f);
        cv_rect(x, y < yb ? y : yb, 1, y < yb ? yb - y : y - yb, MB_DIM[1]);
        if (x)
            cv_line(x - 1, py, x, y, MB_COL[1]);
        py = y;
    }
    if (keq_flat)                        /* (0.9.57: left out, SYSTEM › SETUP K2) */
        cv_text(4, 1, &FONT_XS, "OFF: SYSTEM > SETUP K2 (FLAT)", C_AMB);
    cv_text(4, 38, &FONT_XS, "ALGORITHM: THE OTHER FOUR   SELECT: EQ / FX", C_DIM);
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    for (j = 0; j < 8u; j++) {           /* the eight settings, the part K1-K4 turn lit */
        int lit = (j / 4u) == (ui.keq_part & 1u);
        int32_t px = 6 + (int32_t)(j % 4u) * 58, pyy = 2 + (int32_t)(j / 4u) * 25;
        uint8_t save = ui.keq_part;
        ui.keq_part = (uint8_t)(j / 4u);
        keq_value((int)(j % 4u), t);
        ui.keq_part = save;
        cv_text(px, pyy, &FONT_XS, KEQ_NAME[j / 4u][j % 4u], lit ? C_GRAY : C_DIM);
        cv_text(px, pyy + 10, &FONT_T, t, lit ? C_WHITE : C_DIM);
    }
    cv_blit(0, 140);
}

