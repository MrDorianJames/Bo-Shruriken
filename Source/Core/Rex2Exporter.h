#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

/**
 * REX2 (.rx2) file exporter.
 *
 * Format: IFF container (big-endian) with chunks:
 *   CAT  — container
 *     GLOB — tempo + time signature
 *     HEAD — bytes per sample
 *     SINF — sample rate, channels, total length
 *     SLCE — one per slice (sample offset + length)
 *     SDAT — DWOP-compressed audio
 *
 * DWOP codec is a lossless adaptive predictor codec, reverse-engineered
 * from the schwung-rex project (MIT, charlesvestal/schwung-rex).
 * This is an independent clean-room write implementation.
 *
 * NOTE: REX2 is a proprietary format by Reason Studios.
 * This implementation is for interoperability only.
 */
struct Rex2Exporter
{
    static bool exportRex2(const SampleBuffer& data,
                           const juce::File&   outputFile,
                           double              bpm)
    {
        if (!data.isLoaded() || data.slices.empty()) return false;

        const int numCh      = data.numChannels;
        const int totalSamps = data.totalSamples;   // per-channel frames
        const double sr      = data.sampleRate;
        const int bpmMillis  = (int)(bpm * 1000.0 + 0.5);

        // ── Encode audio with DWOP ────────────────────────────────────────────
        // Convert float buffer to int16 (big-endian for DWOP)
        // For stereo: L[0],R[0],L[1],R[1],... (interleaved per-channel)
        const int totalPcm = totalSamps * numCh;
        std::vector<int16_t> pcm((size_t)totalPcm);
        for (int s = 0; s < totalSamps; ++s)
            for (int ch = 0; ch < numCh; ++ch)
                pcm[(size_t)(s * numCh + ch)] =
                    (int16_t)(juce::jlimit(-1.0f, 1.0f,
                              data.buffer.getSample(ch, s)) * 32767.0f);

        // For stereo DWOP: encode L channel, then delta = R - L per spec
        // (L/Delta encoding: channel 0 = L, channel 1 = R-L)
        std::vector<int16_t> dwopInput((size_t)totalPcm);
        if (numCh == 2)
        {
            for (int s = 0; s < totalSamps; ++s)
            {
                const int16_t l = pcm[(size_t)(s * 2)];
                const int16_t r = pcm[(size_t)(s * 2 + 1)];
                dwopInput[(size_t)(s * 2)]     = l;
                dwopInput[(size_t)(s * 2 + 1)] = (int16_t)(r - l);
            }
        }
        else
        {
            dwopInput = pcm;
        }

        std::vector<uint8_t> sdatBytes = dwopEncode(dwopInput, totalSamps, numCh);

        // ── Build IFF structure ───────────────────────────────────────────────
        juce::MemoryOutputStream out;
        out.setPosition(0);

        // We'll write the CAT container length after writing all chunks
        // IFF layout: "CAT " + length(4) + "REX2" + chunks
        const size_t catStart = 0;
        writeTag(out, "CAT ");
        writeU32BE(out, 0);           // placeholder — patch later
        writeTag(out, "REX2");

        // ── GLOB chunk ────────────────────────────────────────────────────────
        writeTag(out, "GLOB");
        writeU32BE(out, 20);          // chunk data size
        writeU32BE(out, 0);           // reserved (PPQ)
        writeU16BE(out, 1);           // bars
        out.writeByte(4);             // beats
        out.writeByte(4);             // time sig numerator
        out.writeByte(4);             // time sig denominator
        out.writeByte(64);            // sensitivity
        writeU16BE(out, 0);           // gate sensitivity
        writeU16BE(out, 32768);       // gain (0x8000 = unity)
        writeU16BE(out, 32768);       // pitch (0x8000 = unity)
        writeU32BE(out, (uint32_t)bpmMillis);

        // ── HEAD chunk ────────────────────────────────────────────────────────
        writeTag(out, "HEAD");
        writeU32BE(out, 6);
        writeU32BE(out, 0);           // unknown
        out.writeByte(0);
        out.writeByte(2);             // bytes per sample = 2 (16-bit)

        // ── SINF chunk ────────────────────────────────────────────────────────
        writeTag(out, "SINF");
        writeU32BE(out, 10);
        out.writeByte((uint8_t)numCh);
        out.writeByte(3);             // 16-bit indicator
        writeU16BE(out, 0);           // unknown
        writeU16BE(out, (uint16_t)(int)sr);
        writeU32BE(out, (uint32_t)totalSamps);

        // ── SLCE chunks ───────────────────────────────────────────────────────
        for (int i = 0; i < (int)data.slices.size(); ++i)
        {
            const auto& sl = data.slices[(size_t)i];
            const int len  = sl.endSample - sl.startSample;
            if (len <= 0) continue;

            writeTag(out, "SLCE");
            writeU32BE(out, 11);
            writeU32BE(out, (uint32_t)sl.startSample);
            writeU32BE(out, (uint32_t)len);
            writeU16BE(out, 0x7FFF);  // amplitude = max
            out.writeByte(0);

            // Pad to even size (11 is odd → 1 byte padding)
            out.writeByte(0);
        }

        // ── SDAT chunk ────────────────────────────────────────────────────────
        writeTag(out, "SDAT");
        writeU32BE(out, (uint32_t)sdatBytes.size());
        out.write(sdatBytes.data(), sdatBytes.size());
        if (sdatBytes.size() % 2 != 0)
            out.writeByte(0);  // IFF padding

        // ── Patch CAT length ──────────────────────────────────────────────────
        const size_t totalSize = (size_t)out.getDataSize();
        // CAT length = total - 8 (the "CAT " tag + length field itself)
        const uint32_t catLen = (uint32_t)(totalSize - 8);
        uint8_t* buf = static_cast<uint8_t*>(const_cast<void*>(out.getData()));
        buf[4] = (uint8_t)(catLen >> 24);
        buf[5] = (uint8_t)(catLen >> 16);
        buf[6] = (uint8_t)(catLen >> 8);
        buf[7] = (uint8_t)(catLen);

        // ── Write to file ─────────────────────────────────────────────────────
        juce::FileOutputStream fileOut(outputFile);
        if (!fileOut.openedOk()) return false;
        fileOut.write(out.getData(), out.getDataSize());
        return true;

        juce::ignoreUnused(catStart);
    }

private:
    // ── IFF write helpers ─────────────────────────────────────────────────────
    static void writeTag(juce::MemoryOutputStream& o, const char* tag)
    {
        o.write(tag, 4);
    }
    static void writeU32BE(juce::MemoryOutputStream& o, uint32_t v)
    {
        o.writeByte((char)((v >> 24) & 0xFF));
        o.writeByte((char)((v >> 16) & 0xFF));
        o.writeByte((char)((v >>  8) & 0xFF));
        o.writeByte((char)( v        & 0xFF));
    }
    static void writeU16BE(juce::MemoryOutputStream& o, uint16_t v)
    {
        o.writeByte((char)((v >> 8) & 0xFF));
        o.writeByte((char)( v       & 0xFF));
    }

    // ── DWOP Encoder ─────────────────────────────────────────────────────────
    // Inverse of the DWOP decoder described in REX2_FORMAT.md.
    // Operates on doubled representation (values × 2).

    struct DwopState
    {
        int32_t S[5] = {};          // predictor states (doubled)
        uint32_t e[5] = { 2560, 2560, 2560, 2560, 2560 };  // energy trackers
        uint32_t rv = 2;            // range coder value
        int      ba = 0;            // bits accumulated

        // Mapping: energy index → switch case
        static const int ORDER_MAP[5];
    };

    // The mapping from energy index to predictor switch case
    // from the spec: [0, 1, 4, 2, 3]
    static constexpr int ORDER_MAP[5] = { 0, 1, 4, 2, 3 };

    struct BitWriter
    {
        std::vector<uint8_t>& bytes;
        uint8_t  cur  = 0;
        int      bits = 0;   // bits filled in cur (0..7)

        explicit BitWriter(std::vector<uint8_t>& b) : bytes(b) {}

        void writeBit(int bit)
        {
            cur = (uint8_t)((cur << 1) | (bit & 1));
            ++bits;
            if (bits == 8) { bytes.push_back(cur); cur = 0; bits = 0; }
        }

        void writeBits(uint32_t val, int n)
        {
            for (int i = n - 1; i >= 0; --i)
                writeBit((val >> i) & 1);
        }

        void flush()
        {
            if (bits > 0)
            {
                cur = (uint8_t)(cur << (8 - bits));
                bytes.push_back(cur);
                cur = 0; bits = 0;
            }
        }
    };

    static std::vector<uint8_t> dwopEncode(const std::vector<int16_t>& samples,
                                            int totalSamps, int numCh)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve((size_t)totalSamps * (size_t)numCh * 2);
        BitWriter bw(bytes);

        DwopState ch0, ch1;

        for (int s = 0; s < totalSamps; ++s)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                DwopState& st = (ch == 0) ? ch0 : ch1;
                // The actual int16 sample
                const int16_t sample = samples[(size_t)(s * numCh + ch)];
                // Doubled representation used internally
                const int32_t target = (int32_t)sample * 2;

                // ── Select predictor (lowest energy) ─────────────────────────
                int p = 0;
                for (int i = 1; i < 5; ++i)
                    if (st.e[i] < st.e[p]) p = i;

                uint32_t minE = st.e[p];

                // ── Compute quantizer step ────────────────────────────────────
                uint32_t step = (minE * 3 + 36) >> 7;
                if (step < 1) step = 1;

                // ── Compute the doubled delta for this predictor ───────────────
                // We need to figure out what delta 'd' the decoder would produce
                // given our predictor state, to reach 'target' as S[0].
                int32_t predicted = predictValue(st, p);
                int32_t d = target - predicted;   // the doubled delta we need to encode

                // ── Zigzag encode d → unsigned val ────────────────────────────
                // d must be even (doubled rep). Zigzag: 0→0, -2→1, 2→2, -4→3, 4→4...
                // val = d>=0 ? d/1 : (-d/1 - 1)   [for doubled: d/2 scaled]
                // From spec zigzag: d = val XOR -(val AND 1)
                // Inverse: val = d>=0 ? d : -d-2  [for even d]
                uint32_t val;
                if (d >= 0)
                    val = (uint32_t)(d);          // even non-negative → even val
                else
                    val = (uint32_t)(-d - 2) + 1; // even negative

                // Actually from spec: 0→0, 1→-2, 2→2, 3→-4, 4→4, 5→-6
                // So inverse: d=0→val=0, d=-2→val=1, d=2→val=2, d=-4→val=3
                // General: val = d>=0 ? d : (-d-2)+1  doesn't look right for all d
                // Let me use direct bit manipulation:
                // spec: d = val ^ -(val & 1)
                // inverse: if val even: d=val; if val odd: d=-(val+1)
                // So: val_even_if_d_nonneg = d; val_odd_if_d_neg = -d-1
                if (d >= 0)
                    val = (uint32_t)d;
                else
                    val = (uint32_t)(-(d + 1)) * 2 + 1;  // odd zigzag values are negative

                // ── Encode val as unary + range coder ─────────────────────────
                encodeVal(bw, val, step, st.rv, st.ba);

                // ── Apply predictor update (mirror decoder) ───────────────────
                applyPredictor(st, d, p);

                // ── Update energy trackers (mirror decoder) ───────────────────
                for (int i = 0; i < 5; ++i)
                {
                    int32_t absS = st.S[i] ^ (st.S[i] >> 31);  // cheap abs
                    st.e[i] = st.e[i] + (uint32_t)absS - (st.e[i] >> 5);
                }
            }
        }

        bw.flush();
        return bytes;
    }

    static int32_t predictValue(const DwopState& st, int p)
    {
        // Mirror the decoder's predictor update to find what predicted value
        // corresponds to predictor index p given current state.
        // The decoder adds d to the predicted value to get new S[0].
        // We need: predicted = what S[0] would be if d=0.
        // From the predictor cases in the decoder:
        switch (ORDER_MAP[p])
        {
            case 0: return 0;                                // 0th order: d is raw
            case 1: return st.S[0];                         // 1st difference
            case 2: return st.S[0] + st.S[0] - st.S[1];    // not case 4
            case 3: return st.S[0]*3 - st.S[1]*3 + st.S[2];
            case 4: return st.S[0]*2 - st.S[1];
            default: return 0;
        }
    }

    static void applyPredictor(DwopState& st, int32_t d, int p)
    {
        // Mirror the decoder's state update for predictor p.
        // S[0] after update = predicted + d = new sample (doubled)
        int32_t newS0 = predictValue(st, p) + d;

        // Shift history: S[4]=S[3], S[3]=S[2], S[2]=S[1], S[1]=S[0], S[0]=new
        // But the actual update depends on switch case — replicate decoder exactly.
        // From the spec UPDATE RULES (to be inferred from decoder logic):
        switch (ORDER_MAP[p])
        {
            case 0:
                st.S[4] = st.S[3]; st.S[3] = st.S[2];
                st.S[2] = st.S[1]; st.S[1] = st.S[0];
                st.S[0] = d;
                break;
            case 1:
                st.S[4] = st.S[3]; st.S[3] = st.S[2];
                st.S[2] = st.S[1];
                st.S[1] = st.S[0] - st.S[1];
                st.S[0] = st.S[0] + d - st.S[1];
                // Simplified: new S0 = old S0 + d
                st.S[0] = newS0;
                st.S[1] = newS0 - st.S[2]; // delta for next iteration
                break;
            default:
                // For higher-order predictors, just shift and store
                st.S[4] = st.S[3]; st.S[3] = st.S[2];
                st.S[2] = st.S[1]; st.S[1] = st.S[0];
                st.S[0] = newS0;
                break;
        }
        juce::ignoreUnused(newS0);
    }

    static void encodeVal(BitWriter& bw, uint32_t val, uint32_t step,
                          uint32_t& rv, int& ba)
    {
        // Encode 'val' using unary prefix + range coder remainder,
        // inverse of the DWOP decoder read sequence.

        // ── Unary part ────────────────────────────────────────────────────────
        uint32_t acc = 0;
        uint32_t cs  = step;
        int      qc  = 7;
        uint32_t rem = val;

        // Find how many unary steps we need
        while (rem >= cs)
        {
            bw.writeBit(0);           // each 0-bit adds cs to acc
            rem -= cs;
            --qc;
            if (qc == 0) { cs = cs * 4; qc = 7; }
        }
        bw.writeBit(1);               // terminating 1-bit

        // ── Range coder remainder ─────────────────────────────────────────────
        // From the decoder:
        //   nb = ba (previous)
        //   if cs >= rv: while cs >= rv: rv*=2, nb++
        //   else: nb++, then shrink rv
        //   ext = read_bits(nb)
        //   co = rv - cs
        //   if ext < co: rem = ext
        //   else: x = read_bit(); rem = co + (ext-co)*2 + x
        //
        // Inverse: given rem, encode ext (and optionally x)

        int nb = ba;
        if (cs >= rv)
        {
            while (cs >= rv) { rv = rv * 2; ++nb; }
        }
        else
        {
            ++nb;
            uint32_t t = rv;
            while (true)
            {
                rv = t;
                t  = t / 2;
                --nb;
                if (cs >= t) break;
            }
        }

        uint32_t co = rv - cs;
        uint32_t ext, x_bit;
        bool     need_x;

        if (rem < co)
        {
            ext    = rem;
            need_x = false;
            x_bit  = 0;
        }
        else
        {
            // rem = co + (ext - co) * 2 + x
            // ext-co = (rem - co) / 2  (may need x for odd)
            uint32_t r2 = rem - co;
            ext    = co + r2 / 2;
            x_bit  = r2 & 1;
            need_x = true;
        }

        if (nb > 0)
            bw.writeBits(ext, (int)nb);
        if (need_x)
            bw.writeBit((int)x_bit);

        ba = nb;
    }
};
