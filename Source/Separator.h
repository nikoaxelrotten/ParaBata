#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
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

// FFT radix-2 simple (se usa solo para analizar cada golpe, no en el camino del audio)
struct Fft
{
    int n = 0;
    std::vector<std::complex<double>> tw;
    std::vector<int> rev;
    void init (int size)
    {
        n = size; tw.resize ((size_t) n / 2); rev.assign ((size_t) n, 0);
        int lg = 0; while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * 3.14159265358979323846 * i / n);
    }
    void run (std::vector<std::complex<double>>& a) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (a[(size_t) i], a[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    const auto u = a[(size_t) (i + j)], v = a[(size_t) (i + j + len / 2)] * tw[(size_t) (j * (n / len))];
                    a[(size_t) (i + j)] = u + v; a[(size_t) (i + j + len / 2)] = u - v;
                }
    }
};

struct Hit { int offset; int cls; float vel; };     // golpe detectado: posicion dentro del bloque, pieza y velocidad (0..1)

struct SepParams
{
    float sensitivity = 5.0f;                       // 0..10: sensibilidad de deteccion
    float focus = 5.0f;                             // 0..10: cuanto se separa lo que no es golpe ("Other")
    float kickTom = 5.0f;                           // 0..10: 0 = mas golpes graves se toman como Kick, 10 = mas como Toms (frontera 130 -> 70 Hz)
    float hatCym = 5.0f;                            // 0..10: 0 = mas golpes se toman como Hi-Hat, 10 = mas como Cymbals
    float tomSnare = 5.0f;                          // 0..10: 0 = mas golpes se toman como Toms, 10 = mas como Snare (frontera 210 -> 90 Hz)
    float tailMs[kHitStems] = { 120.0f, 160.0f, 350.0f, 60.0f, 1800.0f };   // cuanto dura abierta cada pieza
};

class Separator
{
public:
    static constexpr int kMaxHits = 128;

    void prepare (double sr)
    {
        fs = sr;
        fftN = 2048; while ((double) fftN < 0.040 * sr) fftN <<= 1;
        fft.init (fftN);
        lat = fftN + (int) std::lround (0.012 * sr);                    // ventana de analisis + margen
        const double fc[kBands - 1] = { 80.0, 160.0, 350.0, 2000.0, 6000.0 };
        for (auto& s2 : split) s2.set (fc, sr);
        delayLen = lat + 1;
        for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) dl[c][b].assign ((size_t) delayLen, 0.0f);
        ring.assign (16384, 0.0f);
        win.resize ((size_t) fftN);
        for (int i = 0; i < fftN; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (i + 0.5) / fftN);
        buf.resize ((size_t) fftN); buf2.resize ((size_t) fftN);
        envAtk = 1.0 - std::exp (-1.0 / (0.0005 * sr));
        envRel = 1.0 - std::exp (-1.0 / (0.010 * sr));
        baseAtk = 1.0 - std::exp (-1.0 / (0.012 * sr));
        baseRel = 1.0 - std::exp (-1.0 / (0.12 * sr));
        refrac = (int) std::lround (0.025 * sr);
        reset();
    }

    void reset()
    {
        for (auto& s : split) s.reset();
        for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) std::fill (dl[c][b].begin(), dl[c][b].end(), 0.0f);
        for (int b = 0; b < kBands; ++b) env[b] = base[b] = 0.0;
        for (auto& a : act) a = 0.0;
        n = 0; nCaps = 0; sinceHit = 1 << 30; nPending = 0;
        std::fill (ring.begin(), ring.end(), 0.0f);
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
            ring[(size_t) (t & 16383)] = 0.5f * (xl + xr);
            ++sinceHit;
            if (sinceHit > refrac && nCaps < kMaxCaps)
            {
                bool onset = false;
                for (int b = 0; b < kBands; ++b)
                    if (env[b] > ratioThr * base[b] + 0.0015) { onset = true; break; }
                if (onset) { caps[nCaps++] = t; sinceHit = 0; }
            }
            for (int k = 0; k < nCaps;)
            {
                if (t - caps[k] >= fftN) { classify (caps[k], p); caps[k] = caps[--nCaps]; }
                else ++k;
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

    // Analisis espectral del golpe que empezo en n0 (ventana de fftN muestras desde un poco antes del inicio)
    void classify (int64_t onset, const SepParams& sp)
    {
        const int64_t start = onset - (int64_t) std::lround (0.002 * fs);
        std::vector<std::complex<double>>& a = buf;
        for (int i = 0; i < fftN; ++i)
        {
            const int64_t idx = start + i;
            a[(size_t) i] = (idx < 0 ? 0.0 : (double) ring[(size_t) (idx & 16383)]) * win[(size_t) i];
        }
        fft.run (a);
        // ventana anterior al golpe: lo que ya sonaba (colas de otros golpes) se resta del espectro
        auto& a2 = buf2;
        for (int i = 0; i < fftN; ++i)
        {
            const int64_t idx = start - fftN + i;
            a2[(size_t) i] = (idx < 0 ? 0.0 : (double) ring[(size_t) (idx & 16383)]) * win[(size_t) i];
        }
        fft.run (a2);
        const double df = fs / fftN;
        auto bin = [&] (double f) { return std::min (fftN / 2 - 1, std::max (1, (int) std::lround (f / df))); };
        const int k30 = bin (30.0), k95 = bin (130.0 - 6.0 * sp.kickTom), k320 = bin (340.0), k350 = bin (350.0);
        const int k170 = std::min (k320 - 2, std::max (k95 + 2, bin (210.0 - 12.0 * sp.tomSnare)));
        const int k2k = bin (2000.0), k6k = bin (6000.0), k16k = bin (16000.0);
        double mag[8192];
        const int nb = std::min (fftN / 2, 8192);
        for (int k = 0; k < nb; ++k)
        {
            const double p1 = std::norm (a[(size_t) k]), p0 = std::norm (a2[(size_t) k]);
            mag[k] = std::sqrt (std::max (0.0, p1 - 0.9 * p0));
        }
        auto peakIn = [&] (int lo, int hi) { double m = 0.0; for (int k = lo; k < hi; ++k) m = std::max (m, mag[k]); return m; };
        auto energy = [&] (int lo, int hi) { double e = 0.0; for (int k = lo; k < hi; ++k) e += mag[k] * mag[k]; return e; };
        const double Pk = peakIn (k30, k95), Pt = peakIn (k95, k170), Ps = peakIn (k170, k320);
        const double maxP = std::max ({ Pk, Pt, Ps, 1e-12 });
        const double Etot = energy (k30, k16k) + 1e-18;
        double cn = 0.0, cd = 1e-18; for (int k = k30; k < k16k; ++k) { cn += k * df * mag[k] * mag[k]; cd += mag[k] * mag[k]; }
        const double cent = cn / cd;
        // fundamental: el pico mas grave que no sea mucho menor que el maximo (entre 35 y 340 Hz)
        double f0 = 0.0;
        for (int k = k30 + 1; k < k320; ++k)
            if (mag[k] >= 0.5 * maxP && mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1]) { f0 = k * df; break; }
        const double hfAmp = std::sqrt (energy (bin (1200.0), k16k) / Etot);
        const double hfShare = energy (k2k, k16k) / Etot, airShare = energy (k6k, k16k) / Etot, lowShare = energy (k30, k350) / Etot;
        const double mh = energy (k350, k6k) / Etot;
        const double hc = 0.30 - 0.020 * sp.hatCym;
        double m[kHitStems];
        // graves: kick por la fundamental baja; toms y snare se separan por la fundamental (frontera ajustable) y el brillo
        const double kB = 130.0 - 6.0 * sp.kickTom, tsB = 210.0 - 12.0 * sp.tomSnare;   // hacia KICK / SNARE (izquierda = 0) suben las fronteras
        const double gate = smoothstep (lowShare, 0.10, 0.25);
        m[Kick]  = smoothstep (Pk / maxP, 0.35, 0.60) * gate;
        const double notKick = 1.0 - m[Kick];
        const double bright = smoothstep (hfAmp, 0.12, 0.25);
        const double highF0 = smoothstep (f0, tsB - 10.0, tsB + 10.0);
        const double inTom = smoothstep (f0, kB - 5.0, kB + 5.0);
        m[Toms]  = inTom * (1.0 - highF0) * (1.0 - bright) * notKick * gate;
        m[Snare] = std::min (1.0, highF0 + (1.0 - highF0) * inTom * bright) * notKick * gate;
        m[Hat]   = smoothstep (airShare, 0.30, 0.55) * (1.0 - smoothstep (mh, hc - 0.05, hc + 0.05)) * (1.0 - smoothstep (lowShare, 0.05, 0.15));
        m[Cym]   = smoothstep (hfShare, 0.45, 0.70) * smoothstep (mh, hc - 0.05, hc + 0.05) * (1.0 - smoothstep (lowShare, 0.06, 0.18));
        // velocidad: amplitud RMS de la ventana
        double rms = 0.0; for (int i = 0; i < fftN; ++i) { const int64_t idx = start + i; const double v = idx < 0 ? 0.0 : (double) ring[(size_t) (idx & 16383)]; rms += v * v; }
        rms = std::sqrt (rms / fftN);
        const float vel = (float) clampd ((20.0 * std::log10 (rms * 3.0 + 1e-9) + 40.0) / 34.0, 0.0, 1.0);
#ifdef PB_DEBUG
        debug.push_back ({ (double) onset, Pk / maxP, Pt / maxP, Ps / maxP, cent, lowShare, airShare, m[0], m[1], m[2], m[3], m[4], rms, f0 });
#endif
        if (rms < 0.004) return;                                  // demasiado bajo: ruido de fondo, no un golpe
        const double thr = 0.35 - 0.1 * (sp.sensitivity / 10.0 - 0.5);
        for (int c = 0; c < kHitStems; ++c)
            if (m[c] >= thr && nPending < kMaxPending)
                pending[nPending++] = { onset + lat - (int64_t) std::lround (0.001 * fs), c, (float) clampd (m[c] * 1.5, 0.0, 1.0), vel };
    }

#ifdef PB_DEBUG
public:
    std::vector<std::array<double, 14>> debug;
private:
#endif
    static constexpr int kMaxPending = 64;
    double fs = 48000.0;
    static constexpr int kMaxCaps = 8;
    int fftN = 2048, lat = 2624, delayLen = 2625, refrac = 1200, sinceHit = 0, nPending = 0, nCaps = 0;
    int hitCount[kStems] = { 0 };
    int64_t n = 0, caps[kMaxCaps] = { 0 };
    Fft fft;
    std::vector<float> ring;
    std::vector<double> win;
    std::vector<std::complex<double>> buf, buf2;
    double envAtk = 0.04, envRel = 0.002, baseAtk = 0.0001, baseRel = 0.0001;
    double env[kBands] = { 0 }, base[kBands] = { 0 };
    double act[kHitStems] = { 0 }, target[kHitStems] = { 0 };
    BandSplit split[2];
    std::vector<float> dl[2][kBands];
    Pending pending[kMaxPending];
};
} // namespace pb
