#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Separador de bateria por DSP (no usa IA): divide una pista de bateria estereo en 6 piezas
// (Kick, Snare, Toms, Hi-Hat, Cymbals, Other) que suman exactamente la senal original (con latencia).
//
// Como funciona:
//  1. La senal se parte en 5 bandas por sustraccion (la suma de bandas es igual a la entrada).
//  2. En paralelo, sobre la senal sin retardo, se detectan los golpes (onsets) y se clasifica cada uno segun
//     como se reparte su energia entre las bandas (kick = graves, snare = cuerpo + ruido, hat = aire, ...).
//  3. La senal de audio va retardada (latencia), asi que cada golpe "abre" su pieza justo en el instante en que
//     llega al audio, y su actividad decae segun el Tail de esa pieza. Cada banda se reparte entre piezas con
//     pesos que suman 1.
namespace pb
{
constexpr int kStems = 6;      // Kick, Snare, Toms, Hat, Cym, Other
constexpr int kHitStems = 5;   // las que disparan golpes: Kick, Snare, Toms, Hat, Cym
constexpr int kBands = 6;
enum Stem { Kick = 0, Snare, Toms, Hat, Cym, Other };

inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double smoothstep (double x, double a, double b)
{
    const double t = clampd ((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Biquad generico en doble precision (pasabajos, pasaaltos y pasatodo RBJ)
struct Bq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, AP };
    void set (Type t, double fc, double q, double fs)
    {
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        if (t == LP) { b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; }
        else if (t == HP) { b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0; }
        else { b0 = (1.0 - al) / a0; b1 = -2.0 * c / a0; b2 = (1.0 + al) / a0; }
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0; }
};

// Cruces Linkwitz-Riley de 4to orden en cascada, con pasatodo de compensacion: las bandas suman con magnitud
// plana (la fase total es la de un pasatodo), y cada banda queda bien aislada de las demas.
struct BandSplit
{
    Bq lp[kBands - 1][2], hp[kBands - 1][2], apc[kBands - 1][kBands - 1];   // apc[banda][cruce superior]
    void set (const double* fc, double fs)
    {
        const double q = 0.70710678;
        for (int i = 0; i < kBands - 1; ++i)
        {
            for (int k = 0; k < 2; ++k) { lp[i][k].set (Bq::LP, fc[i], q, fs); hp[i][k].set (Bq::HP, fc[i], q, fs); }
            for (int b = 0; b < i; ++b) apc[b][i].set (Bq::AP, fc[i], q, fs);
        }
    }
    void reset()
    {
        for (int i = 0; i < kBands - 1; ++i) { lp[i][0].reset(); lp[i][1].reset(); hp[i][0].reset(); hp[i][1].reset(); for (int b = 0; b < kBands - 1; ++b) apc[b][i].reset(); }
    }
    void process (double x, double* out)
    {
        double rest = x;
        for (int i = 0; i < kBands - 1; ++i)
        {
            double lo = lp[i][1].process (lp[i][0].process (rest));
            rest = hp[i][1].process (hp[i][0].process (rest));
            // compensacion: los pasatodo de los cruces superiores
            for (int j = i + 1; j < kBands - 1; ++j) lo = apc[i][j].process (lo);
            out[i] = lo;
        }
        out[kBands - 1] = rest;
    }
};

struct Hit { int offset; int cls; float vel; };     // golpe detectado: posicion dentro del bloque, pieza y velocidad (0..1)

struct SepParams
{
    float sensitivity = 5.0f;                       // 0..10: sensibilidad de deteccion
    float focus = 5.0f;                             // 0..10: cuanto se separa lo que no es golpe ("Other")
    float kickTom = 5.0f;                           // 0..10: frontera Kick / Toms (mas alto = mas golpes graves se toman como kick)
    float hatCym = 5.0f;                            // 0..10: frontera Hi-Hat / Cymbals (mas alto = mas golpes se toman como hi-hat)
    float tailMs[kHitStems] = { 120.0f, 160.0f, 350.0f, 60.0f, 1800.0f };   // cuanto dura abierta cada pieza
};

class Separator
{
public:
    static constexpr int kMaxHits = 128;

    void prepare (double sr)
    {
        fs = sr;
        lat = (int) std::lround (sr * 0.040);
        const double fc[kBands - 1] = { 80.0, 160.0, 350.0, 2000.0, 6000.0 };
        for (auto& s : split) s.set (fc, sr);
        delayLen = lat + 1;
        for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) dl[c][b].assign ((size_t) delayLen, 0.0f);
        envAtk = 1.0 - std::exp (-1.0 / (0.0005 * sr));
        envRel = 1.0 - std::exp (-1.0 / (0.010 * sr));
        baseAtk = 1.0 - std::exp (-1.0 / (0.012 * sr));
        baseRel = 1.0 - std::exp (-1.0 / (0.12 * sr));
        capLen = (int) std::lround (0.020 * sr);
        refrac = (int) std::lround (0.045 * sr);
        reset();
    }

    void reset()
    {
        for (auto& s : split) s.reset();
        for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) std::fill (dl[c][b].begin(), dl[c][b].end(), 0.0f);
        for (int b = 0; b < kBands; ++b) env[b] = base[b] = peak[b] = base0[b] = 0.0;
        for (auto& a : act) a = 0.0;
        n = 0; capturing = false; capLeft = 0; sinceHit = 1 << 30; nPending = 0;
        for (auto& c : hitCount) c = 0;
    }

    int latency() const { return lat; }

    // Procesa un bloque. outL/outR[stem] reciben cada pieza (pueden ser nullptr). hits recibe los golpes cuyo
    // inicio cae dentro de este bloque (ya alineados con el audio retardado).
    void process (const float* inL, const float* inR, int numSamples, float* const outL[kStems], float* const outR[kStems],
                  const SepParams& p, Hit* hits, int& numHits)
    {
        numHits = 0;
        const double ratioThr = 3.0 - 1.4 * clampd (p.sensitivity / 10.0, 0.0, 1.0);
        const double otherScale = 1.0 - 0.85 * clampd (p.focus / 10.0, 0.0, 1.0);
        double decay[kHitStems];
        for (int c = 0; c < kHitStems; ++c) decay[c] = std::exp (-1.0 / (std::max (5.0, (double) p.tailMs[c]) * 0.001 * fs));
        const double actAtk = 1.0 - std::exp (-1.0 / (0.0004 * fs));
        static const double prior[kBands][kStems] = {
            //   kick  snare toms  hat   cym   other
            {    1.00, 0.05, 0.35, 0.00, 0.00, 0.35 },     // < 80 Hz
            {    0.50, 0.20, 1.00, 0.00, 0.00, 0.35 },     // 80 - 160
            {    0.10, 1.00, 0.70, 0.00, 0.03, 0.35 },     // 160 - 350
            {    0.15, 1.00, 0.40, 0.03, 0.25, 0.50 },     // 350 - 2k
            {    0.20, 0.85, 0.25, 0.70, 1.00, 0.35 },     // 2k - 6k
            {    0.00, 0.35, 0.00, 1.00, 1.00, 0.20 } };   // > 6k

        for (int j = 0; j < numSamples; ++j)
        {
            const int64_t t = n++;
            const float xl = inL[j], xr = inR[j];
            double bl[kBands], br[kBands];
            split[0].process (xl, bl);
            split[1].process (xr, br);
            const size_t wi = (size_t) (t % delayLen), ri = (size_t) ((t + 1) % delayLen);
            for (int b = 0; b < kBands; ++b)
            {
                dl[0][b][wi] = (float) bl[b]; dl[1][b][wi] = (float) br[b];
                const double m = std::abs (0.5 * (bl[b] + br[b]));
                env[b] += (m > env[b] ? envAtk : envRel) * (m - env[b]);
                base[b] += (env[b] > base[b] ? baseAtk : baseRel) * (env[b] - base[b]);
            }

            // ---- deteccion y clasificacion de golpes (sin retardo)
            ++sinceHit;
            if (! capturing)
            {
                if (sinceHit > refrac)
                {
                    bool onset = false;
                    for (int b = 0; b < kBands; ++b)
                        if (env[b] > ratioThr * base[b] + 0.0015) { onset = true; break; }
                    if (onset)
                    {
                        capturing = true; capLeft = capLen; n0 = t;
                        for (int b = 0; b < kBands; ++b) { peak[b] = env[b]; base0[b] = base[b]; }
                        capCount = 1;
                    }
                }
            }
            else
            {
                for (int b = 0; b < kBands; ++b) peak[b] += env[b];      // se promedia la envolvente en la ventana
                ++capCount;
                if (--capLeft <= 0)
                {
                    for (int b = 0; b < kBands; ++b) peak[b] /= (double) capCount;
                    capturing = false;
                    sinceHit = 0;
                    classify (t, p);
                }
            }

            // ---- activacion de golpes en el instante en que llegan al audio retardado
            for (int k = 0; k < nPending;)
            {
                if (pending[k].due <= t)
                {
                    const int c = pending[k].cls;
                    target[c] = std::max (target[c], (double) pending[k].strength);
                    if (pending[k].due >= t && numHits < kMaxHits) { hits[numHits++] = { j, c, pending[k].vel }; ++hitCount[c]; }
                    pending[k] = pending[--nPending];
                }
                else ++k;
            }
            for (int c = 0; c < kHitStems; ++c)
            {
                if (target[c] > act[c]) act[c] += (target[c] - act[c]) * actAtk;
                target[c] *= decay[c];
                act[c] *= decay[c];
            }

            // ---- reparto de cada banda entre las piezas (los pesos suman 1)
            double sl[kStems] = { 0 }, sr[kStems] = { 0 };
            for (int b = 0; b < kBands; ++b)
            {
                double w[kStems], den = prior[b][Other] * otherScale;
                for (int c = 0; c < kHitStems; ++c) { w[c] = prior[b][c] * act[c]; den += w[c]; }
                w[Other] = prior[b][Other] * otherScale;
                const double inv = 1.0 / den;
                const double vl = dl[0][b][ri], vr = dl[1][b][ri];
                for (int c = 0; c < kStems; ++c) { const double g = w[c] * inv; sl[c] += vl * g; sr[c] += vr * g; }
            }
            for (int c = 0; c < kStems; ++c)
            {
                if (outL[c]) outL[c][j] = (float) sl[c];
                if (outR[c]) outR[c][j] = (float) sr[c];
            }
        }
    }

    // actividad actual de cada pieza (0..1) para los indicadores, y cantidad de golpes por pieza
    float activity (int c) const { return c < kHitStems ? (float) clampd (act[c], 0.0, 1.0) : 1.0f; }
    int hitsCount (int c) const { return hitCount[c]; }

private:
    struct Pending { int64_t due; int cls; float strength; float vel; };

    void classify (int64_t now, const SepParams& sp)
    {
        static const double cal[kBands] = { 1.0, 1.0, 1.0, 1.1, 1.3, 1.6 };      // compensacion suave de la caida espectral
        double x[kBands], S = 0.0;
        for (int b = 0; b < kBands; ++b) { x[b] = std::max (0.0, peak[b] - base0[b]) * cal[b]; S += x[b]; }
        if (S < 1e-4) return;
        double q[kBands];
        for (int b = 0; b < kBands; ++b) q[b] = x[b] / S;
#ifdef PB_DEBUG
        debug.push_back ({ (double) n0, q[0], q[1], q[2], q[3], q[4], q[5] });
#endif
        const double sub = q[0], low = q[1], lmid = q[2], mid = q[3], hi = q[4], air = q[5], hiAll = hi + air;
        const double kt = 2.6 - 0.16 * sp.kickTom;                       // low/sub: por debajo = kick, por encima = toms (5 -> 1.8)
        const double hc = 0.30 - 0.020 * sp.hatCym;                      // fraccion de mid+hi: por debajo = hat, por encima = cymbal (5 -> 0.20)
        const double mh = mid + hi;
        const double ratio = low / (sub + 1e-9);
        double m[kHitStems];
        m[Kick]  = smoothstep (sub + low, 0.40, 0.60) * (1.0 - smoothstep (ratio, kt - 0.35, kt + 0.35)) * (1.0 - smoothstep (lmid, 0.28, 0.45));
        m[Toms]  = smoothstep (sub + low + lmid, 0.60, 0.80) * smoothstep (ratio, kt - 0.35, kt + 0.35) * (1.0 - smoothstep (hiAll, 0.20, 0.40));
        m[Snare] = smoothstep (low + lmid, 0.08, 0.20) * smoothstep (hiAll, 0.15, 0.35) * (1.0 - smoothstep (sub + low, 0.45, 0.70));
        m[Hat]   = smoothstep (air, 0.35, 0.60) * (1.0 - smoothstep (mh, hc - 0.05, hc + 0.05)) * (1.0 - smoothstep (lmid, 0.10, 0.25));
        m[Cym]   = smoothstep (hiAll, 0.40, 0.65) * smoothstep (mh, hc - 0.05, hc + 0.05) * (1.0 - smoothstep (low + lmid, 0.05, 0.14));
        double total = 0.0;
        for (int b = 0; b < kBands; ++b) total += peak[b];
        const float vel = (float) clampd ((20.0 * std::log10 (total + 1e-9) + 40.0) / 34.0, 0.0, 1.0);
        const double thr = 0.35 - 0.1 * (sp.sensitivity / 10.0 - 0.5);
        for (int c = 0; c < kHitStems; ++c)
            if (m[c] >= thr && nPending < kMaxPending)
                pending[nPending++] = { n0 + lat - (int64_t) std::lround (0.001 * fs), c, (float) clampd (m[c] * 1.5, 0.0, 1.0), vel };
        (void) now;
    }

#ifdef PB_DEBUG
public:
    std::vector<std::array<double, 7>> debug;
private:
#endif
    static constexpr int kMaxPending = 64;
    double fs = 48000.0;
    int capCount = 0, lat = 1200, delayLen = 1201, capLen = 336, refrac = 1440, capLeft = 0, sinceHit = 0, nPending = 0;
    int hitCount[kStems] = { 0 };
    int64_t n = 0, n0 = 0;
    bool capturing = false;
    double envAtk = 0.04, envRel = 0.002, baseAtk = 0.0001, baseRel = 0.0001;
    double env[kBands] = { 0 }, base[kBands] = { 0 }, peak[kBands] = { 0 }, base0[kBands] = { 0 };
    double act[kHitStems] = { 0 }, target[kHitStems] = { 0 };
    BandSplit split[2];
    std::vector<float> dl[2][kBands];
    Pending pending[kMaxPending];
};
} // namespace pb
