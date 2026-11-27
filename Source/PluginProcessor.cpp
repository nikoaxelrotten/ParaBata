#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
static const int kMidiNote[pb::kHitStems] = { 36, 38, 45, 42, 49 };      // GM: kick, snare, tom, hi-hat cerrado, crash

AudioProcessorValueTreeState::ParameterLayout ParaBataProcessor::createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [&layout] (const String& id, const String& name, float lo, float hi, float def, float step, float skew, const String& unit)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (skew != 1.0f) r.setSkewForCentre (lo + (hi - lo) * skew);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto b = [&layout] (const String& id, const String& name, bool def) { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    f ("sens",    "Sensitivity", 0.0f, 10.0f, 5.0f, 0.1f, 1.0f, "");
    f ("focus",   "Focus",       0.0f, 10.0f, 5.0f, 0.1f, 1.0f, "");
    f ("kicktom", "Kick-Toms",   0.0f, 10.0f, 5.0f, 0.1f, 1.0f, "");
    f ("tomsnare", "Toms-Snare", 0.0f, 10.0f, 5.0f, 0.1f, 1.0f, "");
    f ("hatcym",  "Hat-Cymbals", 0.0f, 10.0f, 5.0f, 0.1f, 1.0f, "");
    f ("velsens", "Velocity",    0.0f, 10.0f, 7.0f, 0.1f, 1.0f, "");
    b ("midiout", "MIDI Out", false);

    const float tailDef[pb::kHitStems] = { 120.0f, 160.0f, 350.0f, 60.0f, 1800.0f };
    const float tailMax[pb::kHitStems] = { 500.0f, 600.0f, 1500.0f, 400.0f, 4000.0f };
    for (int s = 0; s < pb::kStems; ++s)
    {
        const String id (kStemIds[s]), nm (String (kStemNames[s]).toLowerCase());
        f (id + "_lvl", nm + " level", -24.0f, 12.0f, 0.0f, 0.1f, 1.0f, " dB");
        b (id + "_mute", nm + " mute", false);
        b (id + "_solo", nm + " solo", false);
        if (s < pb::kHitStems)
        {
            f (id + "_tail", nm + " tail", 20.0f, tailMax[s], tailDef[s], 1.0f, 0.3f, " ms");
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_mode", 1 }, nm + " replace", StringArray { "Off", "Layer", "Replace" }, 0));
            f (id + "_slvl", nm + " sample level", -24.0f, 12.0f, 0.0f, 0.1f, 1.0f, " dB");
        }
    }
    return layout;
}

ParaBataProcessor::ParaBataProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",   AudioChannelSet::stereo(), true)
                          .withOutput ("Mix",     AudioChannelSet::stereo(), true)
                          .withOutput ("Kick",    AudioChannelSet::stereo(), false)
                          .withOutput ("Snare",   AudioChannelSet::stereo(), false)
                          .withOutput ("Toms",    AudioChannelSet::stereo(), false)
                          .withOutput ("Hi-Hat",  AudioChannelSet::stereo(), false)
                          .withOutput ("Cymbals", AudioChannelSet::stereo(), false)
                          .withOutput ("Other",   AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    formats.registerBasicFormats();
    for (auto& a : activity) a = 0.0f;
    for (auto& h : hitCount) h = 0;
}

bool ParaBataProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainInputChannelSet() != AudioChannelSet::stereo()) return false;
    if (l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != AudioChannelSet::stereo()) return false;
    return true;
}

void ParaBataProcessor::prepareToPlay (double sampleRate, int)
{
    const bool changed = std::abs (sampleRate - sr) > 1.0;
    sr = sampleRate;
    sep.prepare (sr);
    setLatencySamples (sep.latency());
    inL.assign ((size_t) kChunk, 0.0f); inR.assign ((size_t) kChunk, 0.0f);
    for (auto& v : stemL) v.assign ((size_t) kChunk, 0.0f);
    for (auto& v : stemR) v.assign ((size_t) kChunk, 0.0f);
    for (auto& v : smpL) v.assign ((size_t) kChunk, 0.0f);
    for (auto& v : smpR) v.assign ((size_t) kChunk, 0.0f);
    for (auto& row : voices) for (auto& v : row) v = {};
    pendingOffs.clear();
    samplesDone = 0;
    if (changed) reloadSamplesFromState();
}

//==============================================================================
std::shared_ptr<AudioBuffer<float>> ParaBataProcessor::readSample (const File& file) const
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    const int len = (int) std::min<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * 8.0));
    AudioBuffer<float> raw ((int) std::max (2u, reader->numChannels), len);
    raw.clear();
    reader->read (&raw, 0, len, 0, true, true);
    if (reader->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
    const double ratio = reader->sampleRate / sr;
    const int outLen = std::max (1, (int) std::lround ((double) len / ratio));
    auto out = std::make_shared<AudioBuffer<float>> (2, outLen);
    for (int c = 0; c < 2; ++c)
    {
        const float* s = raw.getReadPointer (c);
        float* d = out->getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
        {
            const double p = (double) i * ratio; const int i0 = (int) p; const float fr = (float) (p - i0);
            const float a = s[std::min (i0, len - 1)], bb = s[std::min (i0 + 1, len - 1)];
            d[i] = a + (bb - a) * fr;
        }
    }
    return out;
}

void ParaBataProcessor::loadSamples (int stem, const Array<File>& files, bool append)
{
    if (stem < 0 || stem >= pb::kHitStems) return;
    Slot next;
    {
        const SpinLock::ScopedLockType sl (slotLock);
        if (append) next = slots[stem];
    }
    for (const auto& f : files)
    {
        if (next.bufs.size() >= (size_t) kMaxSamples) break;
        if (auto b = readSample (f)) { next.bufs.push_back (b); next.paths.add (f.getFullPathName()); }
    }
    {
        const SpinLock::ScopedLockType sl (slotLock);
        slots[stem] = next;
        for (auto& v : voices[stem]) v.active = false;
    }
    for (int i = 0; i < kMaxSamples; ++i)
        apvts.state.setProperty ("sample_" + String (kStemIds[stem]) + "_" + String (i), i < next.paths.size() ? next.paths[i] : String(), nullptr);
}

void ParaBataProcessor::clearSamples (int stem) { loadSamples (stem, {}, false); }

StringArray ParaBataProcessor::getSampleNames (int stem) const
{
    StringArray n;
    const SpinLock::ScopedLockType sl (slotLock);
    for (auto& p : slots[stem].paths) n.add (File (p).getFileName());
    return n;
}

void ParaBataProcessor::reloadSamplesFromState()
{
    for (int s = 0; s < pb::kHitStems; ++s)
    {
        Array<File> files;
        for (int i = 0; i < kMaxSamples; ++i)
        {
            const String p = apvts.state.getProperty ("sample_" + String (kStemIds[s]) + "_" + String (i)).toString();
            if (p.isNotEmpty() && File (p).existsAsFile()) files.add (File (p));
        }
        loadSamples (s, files, false);
    }
}

void ParaBataProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void ParaBataProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            reloadSamplesFromState();
        }
}

//==============================================================================
void ParaBataProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    midi.clear();
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += kChunk)
        processChunk (start, std::min (kChunk, total - start), buffer, midi);
}

void ParaBataProcessor::processChunk (int start, int n, AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int inCh = mainIn.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) i] = mainIn.getSample (0, start + i);
        inR[(size_t) i] = mainIn.getSample (inCh > 1 ? 1 : 0, start + i);
    }

    pb::SepParams sp;
    sp.sensitivity = param ("sens"); sp.focus = param ("focus"); sp.kickTom = param ("kicktom"); sp.hatCym = param ("hatcym"); sp.tomSnare = param ("tomsnare");
    for (int c = 0; c < pb::kHitStems; ++c) sp.tailMs[c] = param (String (kStemIds[c]) + "_tail");
    float* oL[pb::kStems]; float* oR[pb::kStems];
    for (int c = 0; c < pb::kStems; ++c) { oL[c] = stemL[c].data(); oR[c] = stemR[c].data(); }
    pb::Hit hits[pb::Separator::kMaxHits]; int numHits = 0;
    sep.process (inL.data(), inR.data(), n, oL, oR, sp, hits, numHits);

    // ---- golpes: disparar samples y notas MIDI
    const bool midiOut = param ("midiout") > 0.5f;
    const float vs = param ("velsens") / 10.0f;
    {
        const SpinLock::ScopedTryLockType sl (slotLock);
        for (int h = 0; h < numHits; ++h)
        {
            const int c = hits[h].cls;
            if (midiOut)
            {
                const int vel = jlimit (1, 127, (int) std::lround (20.0f + 107.0f * hits[h].vel));
                midi.addEvent (MidiMessage::noteOn (10, kMidiNote[c], (uint8) vel), start + hits[h].offset);
                pendingOffs.push_back ({ samplesDone + hits[h].offset + (int64_t) (0.06 * sr), kMidiNote[c] });
            }
            if (! sl.isLocked() || param (String (kStemIds[c]) + "_mode") < 0.5f) continue;
            auto& slot = slots[c];
            if (slot.bufs.empty()) continue;
            int layer = 0;
            if (slot.bufs.size() > 1) layer = jlimit (0, (int) slot.bufs.size() - 1, (int) (hits[h].vel * (float) slot.bufs.size()));
            auto& pool = voices[c];
            int vi = 0;
            for (; vi < kVoices; ++vi) if (! pool[(size_t) vi].active) break;
            if (vi == kVoices) vi = rr[c]++ % kVoices;
            pool[(size_t) vi] = { slot.bufs[(size_t) layer], 0, hits[h].offset, (1.0f - vs) + vs * hits[h].vel, true };
        }
    }
    // notas de apagado pendientes
    for (size_t k = 0; k < pendingOffs.size();)
    {
        const int64_t rel = pendingOffs[k].at - samplesDone;
        if (rel < n) { midi.addEvent (MidiMessage::noteOff (10, pendingOffs[k].note), start + (int) std::max<int64_t> (0, rel)); pendingOffs.erase (pendingOffs.begin() + (long) k); }
        else ++k;
    }

    // ---- reproducir los samples activos de cada pieza
    for (int c = 0; c < pb::kHitStems; ++c)
    {
        std::fill (smpL[c].begin(), smpL[c].begin() + n, 0.0f);
        std::fill (smpR[c].begin(), smpR[c].begin() + n, 0.0f);
        for (auto& v : voices[c])
        {
            if (! v.active || v.buf == nullptr) continue;
            const int len = v.buf->getNumSamples();
            const float* sl = v.buf->getReadPointer (0); const float* sr2 = v.buf->getReadPointer (1);
            int i = std::max (0, v.delay);
            const int skip = v.delay > 0 ? 0 : -v.delay;       // (si el delay ya paso)
            (void) skip;
            for (; i < n && v.pos < len; ++i, ++v.pos) { smpL[c][(size_t) i] += sl[v.pos] * v.gain; smpR[c][(size_t) i] += sr2[v.pos] * v.gain; }
            v.delay = std::max (0, v.delay - n);
            if (v.pos >= len) { v.active = false; v.buf.reset(); }
        }
    }

    // ---- niveles, mute/solo y mezcla
    bool anySolo = false;
    for (int c = 0; c < pb::kStems; ++c) if (param (String (kStemIds[c]) + "_solo") > 0.5f) anySolo = true;
    float mixL[kChunk], mixR[kChunk];
    std::fill (mixL, mixL + n, 0.0f); std::fill (mixR, mixR + n, 0.0f);
    for (int c = 0; c < pb::kStems; ++c)
    {
        const String id (kStemIds[c]);
        const float gain = Decibels::decibelsToGain (param (id + "_lvl"));
        const bool mute = param (id + "_mute") > 0.5f, solo = param (id + "_solo") > 0.5f;
        const int mode = c < pb::kHitStems ? (int) param (id + "_mode") : 0;
        const float sgain = c < pb::kHitStems ? Decibels::decibelsToGain (param (id + "_slvl")) : 0.0f;
        const bool inMix = ! mute && (! anySolo || solo);
        auto bus = getBusBuffer (buffer, false, c + 1);
        const bool busOn = c + 1 < getBusCount (false) && getBus (false, c + 1)->isEnabled() && bus.getNumChannels() >= 2;
        for (int i = 0; i < n; ++i)
        {
            float l = stemL[c][(size_t) i], r = stemR[c][(size_t) i];
            if (mode == 2) { l = 0.0f; r = 0.0f; }
            if (mode >= 1) { l += smpL[c][(size_t) i] * sgain; r += smpR[c][(size_t) i] * sgain; }
            l *= gain; r *= gain;
            const float ol = mute ? 0.0f : l, orr = mute ? 0.0f : r;
            if (busOn) { bus.setSample (0, start + i, ol); bus.setSample (1, start + i, orr); }
            if (inMix) { mixL[i] += l; mixR[i] += r; }
        }
    }
    auto mixBus = getBusBuffer (buffer, false, 0);
    for (int i = 0; i < n; ++i)
    {
        mixBus.setSample (0, start + i, mixL[i]);
        if (mixBus.getNumChannels() > 1) mixBus.setSample (1, start + i, mixR[i]);
    }
    for (int c = 0; c < pb::kStems; ++c) { activity[c].store (sep.activity (c)); hitCount[c].store (sep.hitsCount (c)); }
    samplesDone += n;
}

AudioProcessorEditor* ParaBataProcessor::createEditor() { return new ParaBataEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ParaBataProcessor(); }
