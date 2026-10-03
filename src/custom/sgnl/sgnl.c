/*
 * sgnl.c - "S.GN_L": a broken digital line (packet loss + codec damage), mono
 *
 * Sounds like a VoIP call on bad Wi-Fi, a DAB radio losing lock or a stream that keeps
 * buffering. The sound is cut into PACKETS (Size, 1..500 ms) and some of them never
 * arrive (Loss, Burst). What fills a hole is the character of the effect (Fill):
 *   GAP     a hard little silence
 *   REPT    the last packet that arrived is replayed over and over: a 5 ms packet becomes
 *           a robotic buzz at about 200 Hz, a 60 ms packet a stutter
 *   FADE    the same replay dying away over about 30 ms (how phone codecs hide a loss)
 *   NOISE   soft hiss at the level of the sound
 *   REVRS   the last packet played backwards: wobbly grain, or reverse-tape blips
 *   GARBL   corrupted data: random slices of the last packet, boosted and clipped, held for
 *           random lengths and mixed with noise: the screech of a broken MP3 stream
 *   LATE    packets arriving out of order: each lost packet plays a random older one
 *           (2 or 3 packets back), so phrases shuffle
 *   RND     each outage picks one of the others at random, except GARBL (it takes over)
 * On top of the losses the codec itself degrades (Codec): spectral holes (each of four
 * bands randomly switched off per packet: the "underwater" sound of a 32 kbps MP3), a
 * closing low-pass, a lower sample rate and fewer bits, all together on one knob. Jump
 * makes the quality fall suddenly for a few packets and come back, like adaptive bitrate.
 * At Codec above 50 the first packet after a silence is lost too, like a call clipping
 * the start of a word. Line puts the whole thing on a phone-style band first.
 *
 * SIGNAL CHAIN
 *   in -> Line band -> codec (holes, low-pass, rate hold, bits) -> packets: kept or lost,
 *   lost ones filled -> Edge crossfade between the live packet and the fill -> Mix
 *
 * PACKETS
 *   A packet is a whole number of 8-sample blocks (1 ms = 6 blocks, 500 ms = 2756), so
 *   every decision is made once per block, where the knobs are read. The Size knob is
 *   latched at the start of each packet.
 *   Loss uses a two-state (Gilbert-Elliott) model, like real networks: GOOD and BAD.
 *   Each packet a random number decides whether to change state. Loss sets the share of
 *   packets lost (0 = none, 100 = about 85 %); Burst sets how sticky BAD is: 0 = the
 *   losses are single and scattered, 100 = outages of about 12 packets on average, with
 *   the same overall share. (Above about Loss 70 the outages get longer even at Burst 0:
 *   a high share of single losses would need more than every other packet lost.)
 *   The packet being played is recorded; when a packet is lost, the last one that
 *   arrived is what REPT, FADE, REVRS and GARBL use. Four 500 ms buffers take turns
 *   (353 KB of the at least 705 KB arena): the one being recorded and the last three good packets (for LATE).
 *
 * KNOBS (screen values)
 *   0 Loss  0..100  how many packets are lost
 *   1 Size  0..100  packet length, 1..500 ms (log); shown in ms
 *   2 Codec 0..100  codec quality going down (0 = clean)
 *   3 Fill  0..7    GAP / REPT / FADE / NOISE / REVRS / GARBL / LATE / RND: what replaces
 *                   a lost packet
 *   4 Burst 0..100  0 = scattered single losses, 100 = long outages
 *   5 Jump  0..100  how often the codec suddenly drops to a much worse quality for a moment
 *   6 Line  0..3    HIFI (full band, untouched) / VOIP (200 Hz..5 kHz, steep low cut,
 *                   boxy headset bump at 1.5 kHz) / PHONE (300 Hz..3.4 kHz) /
 *                   WALKY (500 Hz..2.5 kHz, driven)
 *   7 Edge  0..100  cut at the packet edges: 0 = hard clicks, 100 = fades that fill most of
 *                   the packet (they scale with Size: about 12 ms at 10 ms packets)
 *   8 Mix   0..100  dry/wet crossfade, DJ style: dry full up to 50, wet full from 50,
 *                   both full at 50
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch and no if/else dispatch on the mode knobs (they become
 * float weights once per block), every helper forced inline, no calls. Powers of two by
 * shifts and by building the float exponent. Loss needs one 1/x: done as rsqrt(x)^2.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define SG_DO_PRAGMA(x) _Pragma(#x)
#define SG_EXPAND_PRAGMA(x) SG_DO_PRAGMA(x)
#define SG_ALWAYS_INLINE(fn) SG_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SG_CODE_SECTION(fn) SG_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SG_ALWAYS_INLINE(fn)
#define SG_CODE_SECTION(fn)
#endif

#define SG_MAGIC      0x53474E34u        /* "SGN4": change whenever SgState changes */
#define SG_PK_MAX     22056              /* samples: 500 ms rounded up to whole blocks */
#define SG_PKB_MAX    2757               /* SG_PK_MAX / 8 */
#define SG_MS_PER_BLK 0.18140590f        /* 8 / 44.1: one block in ms */
#define SG_FADE_K     0.99924f           /* FADE: replay gain per sample, about 30 ms */
#define SG_QUIET_LVL  0.003f             /* below this the input counts as silent */
#define SG_QUIET_BLKS 551                /* 100 ms of silence before a word can be clipped */

/* one-pole coefficients a = 1 - exp(-2 pi f / 44100) */
#define SG_A_100      0.014146f
#define SG_A_200      0.028093f
#define SG_A_300      0.041842f
#define SG_A_500      0.068760f
#define SG_A_1200     0.157150f
#define SG_A_2500     0.299670f
#define SG_A_3400     0.383930f
#define SG_A_4000     0.434450f
#define SG_A_5000     0.509524f
#define SG_A_7000     0.631180f
#define SG_F_1500     0.213307f          /* 2 sin(pi 1500 / fs), SVF tuning     */

typedef struct {
    unsigned int magic;
    unsigned int rng;      /* LCG                                             */
    int   left;            /* blocks left in the current packet                */
    int   bad;             /* Gilbert-Elliott state: 1 = BAD                   */
    int   lost;            /* the current packet is lost                       */
    int   cur;             /* buffer being recorded (0..3); cur-1 is the last good packet */
    int   rec_n;           /* samples recorded in the current packet           */
    int   len[4];          /* length of each buffer's packet, 0 = none yet     */
    int   src, sn;         /* replay source buffer and its length              */
    int   rev;             /* 1 = replay backwards (REVRS)                     */
    int   late;            /* 1 = each lost packet plays an older one (LATE)   */
    int   gc;              /* GARBL: samples left on the held value            */
    float gv;              /* GARBL: held value                                */
    float wrep, wfade, wnoise, wgarb; /* Fill of the current outage as weights */
    int   rp;              /* replay position in the last good packet          */
    int   jump;            /* packets left in a quality jump                   */
    int   quiet;           /* blocks of silence so far                         */
    float jq;              /* codec quality during the jump                    */
    float fg;              /* FADE gain                                        */
    float w;               /* crossfade: 1 = live packet, 0 = fill             */
    float env;             /* level of the decoded sound (for NOISE)           */
    float nlp;             /* NOISE colouring                                  */
    float ienv;            /* input level (for clipping the start of a word)   */
    /* line band */
    float lh, ll1, ll2;
    float lh2;             /* second low-cut pole (VOIP)                       */
    float vb, vl;          /* VOIP headset bump (state-variable filter)        */
    /* codec, set per packet */
    float b1, b2, b3;      /* crossover low-passes                             */
    float g0, g1, g2, g3;  /* band gains (smoothed)                            */
    float t0, t1, t2, t3;  /* band gain targets                                */
    float alp, lp1, lp2;   /* closing low-pass                                 */
    float qL, qiL, qon;    /* quantiser: levels, 1/levels, on                  */
    float hold;            /* sample-rate hold                                 */
    int   hn, hc;          /* hold length, counter                             */
    float buf[4][SG_PK_MAX];
} SgState;

typedef struct {
    unsigned int pgb, pbg; /* GOOD->BAD and BAD->GOOD chances, 0..65536        */
    unsigned int pj;       /* chance of a quality jump per packet, 0..65536    */
    int   pkb;             /* packet length in blocks                          */
    float q;               /* Codec 0..1                                       */
    int   fill;            /* Fill knob 0..7                                   */
    float lon, ah, al, lgain, drive;   /* Line                                 */
    float voip;                        /* VOIP: second low-cut pole and bump   */
    float c;               /* Edge crossfade coefficient                       */
    float dryG, wetG;      /* Mix: DJ crossfade gains                          */
} SgParams;

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer. */
SG_ALWAYS_INLINE(sg_ui)
static inline float sg_ui(float raw, float def_ui, float max_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > max_ui) ui = max_ui;
    if (ui < 0.0f) ui = 0.0f;
    return ui;
}

SG_ALWAYS_INLINE(sg_rsqrt)
static inline float sg_rsqrt(float x)
{
    union { float f; unsigned int u; } c;
    float y, h = 0.5f * x;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    y = c.f;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}

/* 2^x for x in [0, 9): whole octaves by doubling, the fraction by a polynomial */
SG_ALWAYS_INLINE(sg_exp2)
static inline float sg_exp2(float x)
{
    int   n = (int)x;
    float f = x - (float)n;
    float r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    for (; n > 0; n--) r += r;
    return r;
}

/* 2^-n for n in 0..30, from the float exponent field: no divide, no table */
SG_ALWAYS_INLINE(sg_pow2neg)
static inline float sg_pow2neg(int n)
{
    union { float f; unsigned int u; } c;
    c.u = ((unsigned int)(127 - n)) << 23;
    return c.f;
}

/* next random number, 16 bits */
SG_ALWAYS_INLINE(sg_rand16)
static inline unsigned int sg_rand16(SgState *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (s->rng >> 8) & 0xFFFFu;
}

/* Size 0..100 -> packet length in blocks: 1 ms * 500^(Size/100) */
SG_ALWAYS_INLINE(sg_size_blocks)
static inline int sg_size_blocks(float size_ui)
{
    float smp = 44.1f * sg_exp2(size_ui * 0.0896578f);     /* 8.96578 = log2(500) */
    int b = (int)(smp * 0.125f + 0.5f);
    if (b < 1) b = 1;
    if (b > SG_PKB_MAX) b = SG_PKB_MAX;
    return b;
}

SG_ALWAYS_INLINE(sg_init)
static inline void sg_init(SgState *s)
{
    s->rng = 0x2545F491u;
    s->left = 0; s->bad = 0; s->lost = 0; s->cur = 0; s->rec_n = 0; s->rp = 0;
    s->len[0] = 0; s->len[1] = 0; s->len[2] = 0; s->len[3] = 0;
    s->src = 0; s->sn = 0; s->rev = 0; s->late = 0; s->gc = 0; s->gv = 0.0f;
    s->wrep = 0.0f; s->wfade = 0.0f; s->wnoise = 0.0f; s->wgarb = 0.0f;
    s->jump = 0; s->quiet = 0; s->jq = 0.0f; s->fg = 1.0f; s->w = 1.0f;
    s->env = 0.0f; s->nlp = 0.0f; s->ienv = 0.0f;
    s->lh = 0.0f; s->ll1 = 0.0f; s->ll2 = 0.0f;
    s->lh2 = 0.0f; s->vb = 0.0f; s->vl = 0.0f;
    s->b1 = 0.0f; s->b2 = 0.0f; s->b3 = 0.0f;
    s->g0 = 1.0f; s->g1 = 1.0f; s->g2 = 1.0f; s->g3 = 1.0f;
    s->t0 = 1.0f; s->t1 = 1.0f; s->t2 = 1.0f; s->t3 = 1.0f;
    s->alp = 1.0f; s->lp1 = 0.0f; s->lp2 = 0.0f;
    s->qL = 1.0f; s->qiL = 1.0f; s->qon = 0.0f;
    s->hold = 0.0f; s->hn = 1; s->hc = 0;
    s->magic = SG_MAGIC;
}

/* u[] = screen values in manifest order */
SG_ALWAYS_INLINE(sg_prepare)
static inline void sg_prepare(SgParams *P, const float *u)
{
    float L = u[0] * 0.01f, B = u[4] * 0.01f, J = u[5] * 0.01f, e, k, pi, pbg, pgb, r;
    unsigned int fill = (unsigned int)(int)(u[3] + 0.5f);
    unsigned int line = (unsigned int)(int)(u[6] + 0.5f);

    /* Loss: share of lost packets pi; Burst: chance to leave BAD per packet.
     * pgb = pi * pbg / (1 - pi) keeps the share at pi whatever Burst is. */
    pi  = L * (0.25f + 0.6f * L);                    /* 0 .. 0.85 */
    pbg = 1.0f - 0.92f * B;                          /* 1 .. 0.08 */
    r   = sg_rsqrt(1.0f - pi);
    pgb = pi * pbg * r * r;
    if (pgb > 1.0f) {                                /* share too high for such short outages: */
        r   = sg_rsqrt(pi);                          /* lengthen them just enough, pbg = (1 - pi) / pi */
        pbg = (1.0f - pi) * r * r;
        pgb = 1.0f;
    }
    P->pgb = (unsigned int)(int)(pgb * 65536.0f);
    P->pbg = (unsigned int)(int)(pbg * 65536.0f);
    P->pj  = (unsigned int)(int)(J * J * 0.15f * 65536.0f);

    P->pkb = sg_size_blocks(u[1]);
    P->q   = u[2] * 0.01f;

    P->fill = (int)fill;

    /* Line as weights and coefficients */
    P->lon   = (float)(line >= 1u);
    P->ah    = (float)(line == 1u) * SG_A_200  + (float)(line == 2u) * SG_A_300  + (float)(line >= 3u) * SG_A_500;
    P->al    = (float)(line == 1u) * SG_A_5000 + (float)(line == 2u) * SG_A_3400 + (float)(line >= 3u) * SG_A_2500;
    P->lgain = 1.0f + 0.4f * (float)(line == 2u) + 0.5f * (float)(line == 1u);
    P->drive = (float)(line >= 3u);
    P->voip  = (float)(line == 1u);

    /* Edge: the fade time follows the packet length. Time constant
       tau = E (0.3 + 0.7 E) x 0.3 x packet, so 0 = hard cut and 100 = the fade
       fills most of the packet (lost packets become soft dips and swells).
       c = 1 / (1 + tau), done as rsqrt squared to avoid a divide. */
    e = u[7] * 0.01f;
    k = e * (0.3f + 0.7f * e) * 2.4f * (float)P->pkb;   /* 0.3 x pkb x 8 samples */
    e = sg_rsqrt(1.0f + k);
    P->c = e * e;

    {   /* Mix: DJ crossfade, both full at 50 */
        float m = u[8] * 0.01f;
        P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
        P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
    }
}

/* Start of a packet: decide lost or kept, set the codec for it. */
SG_ALWAYS_INLINE(sg_packet)
static inline void sg_packet(SgState *s, const SgParams *P)
{
    unsigned int ph, ph0;
    float q, qq;
    int lost, nb;

    /* the packet that just ended arrived: it becomes the last good packet */
    if (!s->lost && s->rec_n > 0) { s->len[s->cur] = s->rec_n; s->cur = (s->cur + 1) & 3; }
    s->rec_n = 0;

    /* quality jump: a few packets at a much worse quality */
    if (s->jump > 0) s->jump--;
    else if (sg_rand16(s) < P->pj) {
        s->jump = 2 + (int)(sg_rand16(s) & 7u);
        s->jq = 0.6f + 0.4f * (float)(int)sg_rand16(s) * (1.0f / 65536.0f);
    }
    q = P->q;
    if (s->jump > 0 && s->jq > q) q = s->jq;

    /* Gilbert-Elliott */
    if (s->bad) { if (sg_rand16(s) < P->pbg) s->bad = 0; }
    else        { if (sg_rand16(s) < P->pgb) s->bad = 1; }
    lost = s->bad;
    if (s->quiet < 0) { lost = 1; s->quiet = 0; }      /* a word starting after silence */
    if (lost) {
        int mode = P->fill, lg = (s->cur + 3) & 3, src = lg;
        unsigned int m;
        if (!s->lost) {                                 /* new outage: replay from the top */
            s->rp = 0; s->fg = 1.0f; s->gc = 0;
            if (mode >= 7) {                            /* RND: any but GARBL (too harsh) */
                mode = (int)((sg_rand16(s) * 6u) >> 16);   /* 0..5 */
                mode += (mode >= 5);                       /* 5 -> 6: skip GARBL */
            }
            else if (mode < 0) mode = 0;
            s->wrep   = (float)(mode == 1) + (float)(mode == 2) + (float)(mode == 4) + (float)(mode == 6);
            s->wfade  = (float)(mode == 2);
            s->wnoise = (float)(mode == 3);
            s->wgarb  = (float)(mode == 5);
            s->rev    = (mode == 4);
            s->late   = (mode == 6);
        }
        if (s->late) {                                     /* LATE: a random older packet each time */
            m = (sg_rand16(s) >> 15) + 2u;                 /* 2 or 3 back */
            src = (s->cur + 4 - (int)m) & 3;
            if (s->len[src] == 0) src = lg;
            s->rp = 0;
        }
        s->src = src; s->sn = s->len[src];
    }
    s->lost = lost;

    /* codec for this packet */
    qq = q * q;
    ph  = (unsigned int)(int)(qq * 0.45f * 65536.0f);  /* chance a band drops out */
    ph0 = ph >> 1;                                     /* the lowest band less often */
    s->t0 = (sg_rand16(s) < ph0) ? 0.0f : 1.0f;
    s->t1 = (sg_rand16(s) < ph)  ? 0.0f : 1.0f;
    s->t2 = (sg_rand16(s) < ph)  ? 0.0f : 1.0f;
    s->t3 = (sg_rand16(s) < ph)  ? 0.0f : 1.0f;
    s->alp = 1.0f - 0.85f * q * (2.0f - q);            /* 1 = open .. 0.15 (about 1 kHz) */
    s->hn  = 1 + (int)(qq * q * 12.0f);                /* hold 1 .. 13 samples */
    nb = 16 - (int)(q * 12.0f + 0.5f);                 /* 16 .. 4 bits */
    s->qL  = (float)(1 << (nb - 1));
    s->qiL = sg_pow2neg(nb - 1);
    s->qon = (float)(q > 0.02f);

    s->left = P->pkb;
}

SG_ALWAYS_INLINE(sg_process)
static inline void sg_process(SgState *s, const SgParams *P, float *buf, int n)
{
    int i;
    float *rec, *last;

    /* clip the start of a word: at Codec above 50, sound after 100 ms of silence
     * starts a new packet straight away, and that packet is lost */
    {
        float pk = 0.0f;
        for (i = 0; i < n; i++) { float a = buf[i]; if (a < 0.0f) a = -a; if (a > pk) pk = a; }
        if (pk < SG_QUIET_LVL) { if (s->quiet < SG_QUIET_BLKS + 1) s->quiet++; }
        else {
            if (s->quiet > SG_QUIET_BLKS && P->q > 0.5f) { s->quiet = -1; s->left = 0; }
            else s->quiet = 0;
        }
    }

    if (s->left <= 0) sg_packet(s, P);
    s->left--;

    rec  = s->buf[s->cur];
    last = s->buf[s->src];

    for (i = 0; i < n; i++) {
        float in = buf[i], x, y, f, a, lf, target;

        /* Line */
        s->lh  += P->ah * (in - s->lh);
        x = in - s->lh;
        s->lh2 += P->ah * (x - s->lh2);
        x = x - P->voip * s->lh2;
        s->ll1 += P->al * (x - s->ll1);
        s->ll2 += P->al * (s->ll1 - s->ll2);
        x = s->ll2 * P->lgain;
        {   /* VOIP headset bump: band-pass at 1.5 kHz (Q 1.5) added on top */
            float hp;
            s->vl += SG_F_1500 * s->vb;
            hp = x - s->vl - 0.6667f * s->vb;
            s->vb += SG_F_1500 * hp;
            x = x + P->voip * 0.7f * s->vb;
        }
        {
            float d = x * (1.0f + 1.5f * P->drive);
            if (d > 1.0f) d = 1.0f;
            if (d < -1.0f) d = -1.0f;
            d = 1.5f * d - 0.5f * d * d * d;
            x = x + P->drive * (0.55f * d - x);
        }
        x = in + P->lon * (x - in);

        /* codec: spectral holes (the four bands add back up to x when all are on) */
        s->b1 += SG_A_300  * (x - s->b1);
        s->b2 += SG_A_1200 * (x - s->b2);
        s->b3 += SG_A_4000 * (x - s->b3);
        s->g0 += 0.01f * (s->t0 - s->g0);
        s->g1 += 0.01f * (s->t1 - s->g1);
        s->g2 += 0.01f * (s->t2 - s->g2);
        s->g3 += 0.01f * (s->t3 - s->g3);
        y = s->g0 * s->b1 + s->g1 * (s->b2 - s->b1) + s->g2 * (s->b3 - s->b2) + s->g3 * (x - s->b3);
        /* closing low-pass */
        s->lp1 += s->alp * (y - s->lp1);
        s->lp2 += s->alp * (s->lp1 - s->lp2);
        y = s->lp2;
        /* lower sample rate */
        if (s->hc <= 0) { s->hold = y; s->hc = s->hn; }
        s->hc--;
        y = s->hold;
        /* fewer bits */
        {
            float v = y * s->qL;
            int iv = (int)(v + ((v >= 0.0f) ? 0.5f : -0.5f));
            y = y + s->qon * ((float)iv * s->qiL - y);
        }

        a = (y < 0.0f) ? -y : y;
        if (!s->lost) {
            if (s->rec_n < SG_PK_MAX) { rec[s->rec_n] = y; s->rec_n++; }
            s->env += 0.002f * (a - s->env);
        } else s->env *= 0.99995f;

        /* fill */
        f = 0.0f;
        if (s->lost) {
            float r = 0.0f, nz;
            nz = (float)((int)(sg_rand16(s)) - 32768) * (1.0f / 32768.0f);
            if (s->sn > 0) {
                r = last[s->rp + s->rev * (s->sn - 1 - 2 * s->rp)];   /* REVRS reads backwards */
                s->rp++;
                if (s->rp >= s->sn) s->rp = 0;
                if (s->wgarb > 0.0f) {                     /* GARBL: boosted random slices */
                    s->gc--;
                    if (s->gc <= 0) {
                        unsigned int j = (sg_rand16(s) * (unsigned int)s->sn) >> 16;
                        float g = 3.0f * last[j] + nz * s->env;
                        if (g > 0.4f) g = 0.4f;
                        if (g < -0.4f) g = -0.4f;
                        s->gv = g;
                        s->gc = 1 + (int)(sg_rand16(s) >> 10);          /* 1..64 samples */
                    }
                }
            }
            s->fg *= SG_FADE_K;
            lf = 1.0f + s->wfade * (s->fg - 1.0f);       /* REPT: 1, FADE: dying */
            s->nlp += 0.3f * (nz - s->nlp);
            f = s->wrep * r * lf + s->wnoise * s->nlp * s->env * 1.5f + s->wgarb * s->gv;
        }

        /* Edge: crossfade between the live packet and the fill */
        target = s->lost ? 0.0f : 1.0f;
        s->w += P->c * (target - s->w);
        y = f + s->w * (y - f);

        buf[i] = P->dryG * in + P->wetG * y;
    }
}

/* ---- on-screen text ------------------------------------------------------ */
SG_ALWAYS_INLINE(sg_put_int)
static inline int sg_put_int(int n, char *out)
{
    int h = 0, t = 0, len = 0;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    return len;
}

/* knob 1 Size: screen 0..100 -> packet length in ms, "2ms".."100ms" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int len, ms;
    if (value > 100u) value = 100u;
    ms = (int)((float)sg_size_blocks((float)(int)value) * SG_MS_PER_BLK + 0.5f);
    len = sg_put_int(ms, out);
    out[len] = 'm'; out[len + 1] = 's'; out[len + 2] = 0;
    return len + 2;
}

/* up to five characters, 0 ends early */
SG_ALWAYS_INLINE(sg_text)
static inline int sg_text(char *out, int c0, int c1, int c2, int c3, int c4)
{
    int len = 3;
    out[0] = (char)c0; out[1] = (char)c1; out[2] = (char)c2; out[3] = (char)c3; out[4] = (char)c4;
    if (c3 != 0) len = 4;
    if (c4 != 0) len = 5;
    out[len] = 0;
    return len;
}

/* knob 3 Fill: GAP REPT FADE NOISE REVRS GARBL LATE RND */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value >= 7u) return sg_text(out, 'R', 'N', 'D', 0, 0);
    if (value == 6u) return sg_text(out, 'L', 'A', 'T', 'E', 0);
    if (value == 5u) return sg_text(out, 'G', 'A', 'R', 'B', 'L');
    if (value == 4u) return sg_text(out, 'R', 'E', 'V', 'R', 'S');
    if (value == 3u) return sg_text(out, 'N', 'O', 'I', 'S', 'E');
    if (value == 2u) return sg_text(out, 'F', 'A', 'D', 'E', 0);
    if (value == 1u) return sg_text(out, 'R', 'E', 'P', 'T', 0);
    return sg_text(out, 'G', 'A', 'P', 0, 0);
}

/* knob 6 Line: 0 "HIFI", 1 "VOIP", 2 "PHONE", 3 "WALKY" */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    if (value >= 3u) { out[0] = 'W'; out[1] = 'A'; out[2] = 'L'; out[3] = 'K'; out[4] = 'Y'; out[5] = 0; return 5; }
    if (value == 2u) { out[0] = 'P'; out[1] = 'H'; out[2] = 'O'; out[3] = 'N'; out[4] = 'E'; out[5] = 0; return 5; }
    if (value == 1u) { out[0] = 'V'; out[1] = 'O'; out[2] = 'I'; out[3] = 'P'; out[4] = 0; return 4; }
    out[0] = 'H'; out[1] = 'I'; out[2] = 'F'; out[3] = 'I'; out[4] = 0;
    return 4;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef SGNL_HOST_TEST

#include "sgnl_params.h"

#ifndef SGNL_AUDIO_FUNC
#define SGNL_AUDIO_FUNC Fx_DLY_SGNL
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SG_CODE_SECTION(SGNL_AUDIO_FUNC)
void SGNL_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    SgState *s;
    SgParams P;
    float u[9];
    int i;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(SgState) || span < (end - base)) return;
    if (stateBase + sizeof(SgState) > end) return;

    s = (SgState *)stateBase;

    if (params[0] < 0.5f) return;                /* effect bypassed */

    u[0] = sg_ui(params[SGNL_LOSS_SLOT],  (float)SGNL_LOSS_UI_DEFAULT,  100.0f);
    u[1] = sg_ui(params[SGNL_SIZE_SLOT],  (float)SGNL_SIZE_UI_DEFAULT,  100.0f);
    u[2] = sg_ui(params[SGNL_CODEC_SLOT], (float)SGNL_CODEC_UI_DEFAULT, 100.0f);
    u[3] = sg_ui(params[SGNL_FILL_SLOT],  (float)SGNL_FILL_UI_DEFAULT,  7.0f);
    u[4] = sg_ui(params[SGNL_BURST_SLOT], (float)SGNL_BURST_UI_DEFAULT, 100.0f);
    u[5] = sg_ui(params[SGNL_JUMP_SLOT],  (float)SGNL_JUMP_UI_DEFAULT,  100.0f);
    u[6] = sg_ui(params[SGNL_LINE_SLOT],  (float)SGNL_LINE_UI_DEFAULT,  3.0f);
    u[7] = sg_ui(params[SGNL_EDGE_SLOT],  (float)SGNL_EDGE_UI_DEFAULT,  100.0f);
    u[8] = sg_ui(params[SGNL_MIX_SLOT],   (float)SGNL_MIX_UI_DEFAULT,   100.0f);

    sg_prepare(&P, u);
    if (s->magic != SG_MAGIC) sg_init(s);
    sg_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* SGNL_HOST_TEST */
