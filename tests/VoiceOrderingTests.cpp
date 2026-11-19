/*
  VoiceOrderingTests.cpp — directed checks for fixed-capacity voice steal ordering.

  Mirrors the ABI golden SlotAllocator::steal priority rules against an
  independent reference that uses the shared stable_sort_indices_n helper.
*/
#include "HostReplayAdapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
struct RefSlot
{
    int note = -1;
    bool keyDown = false;
    bool sustain = false;
    bool envelopeLive = false;
    uint32_t noteOnTime = 0;
};

// Independent copy of AbiGoldenTests SlotAllocator::steal using stable_sort_indices_n.
int ref_steal (const std::array<RefSlot, 8>& slots, int midiNote)
{
    std::array<int, 8> order {};
    int n = 0;
    for (int i = 0; i < 8; ++i)
        if (slots[static_cast<size_t> (i)].envelopeLive)
            order[static_cast<size_t> (n++)] = i;

    gardenia_core_test::stable_sort_indices_n (order.data(), n, [&] (int a, int b) {
        return slots[static_cast<size_t> (a)].noteOnTime < slots[static_cast<size_t> (b)].noteOnTime;
    });

    int low = -1, top = -1;
    for (int k = 0; k < n; ++k)
    {
        const int i = order[static_cast<size_t> (k)];
        const auto& s = slots[static_cast<size_t> (i)];
        if (s.note < 0)
            continue;
        if (s.note >= 0 && ! s.keyDown && ! s.sustain)
            continue;
        if (low < 0 || s.note < slots[static_cast<size_t> (low)].note)
            low = i;
        if (top < 0 || s.note > slots[static_cast<size_t> (top)].note)
            top = i;
    }
    if (top == low)
        top = -1;
    auto prot = [&] (int i) { return i == low || i == top; };
    for (int k = 0; k < n; ++k)
        if (slots[static_cast<size_t> (order[static_cast<size_t> (k)])].note == midiNote)
            return order[static_cast<size_t> (k)];
    for (int k = 0; k < n; ++k)
    {
        const int i = order[static_cast<size_t> (k)];
        const auto& s = slots[static_cast<size_t> (i)];
        if (! prot (i) && s.note >= 0 && ! s.keyDown && ! s.sustain)
            return i;
    }
    for (int k = 0; k < n; ++k)
    {
        const int i = order[static_cast<size_t> (k)];
        if (! prot (i) && ! slots[static_cast<size_t> (i)].keyDown)
            return i;
    }
    for (int k = 0; k < n; ++k)
        if (! prot (order[static_cast<size_t> (k)]))
            return order[static_cast<size_t> (k)];
    return top >= 0 ? top : (low >= 0 ? low : 0);
}

int fail = 0;

void expect_eq (const char* name, int got, int want)
{
    if (got != want)
    {
        std::cerr << "FAIL " << name << " got=" << got << " want=" << want << "\n";
        ++fail;
    }
    else
        std::cout << "PASS " << name << "\n";
}

void expect_true (const char* name, bool ok)
{
    if (! ok)
    {
        std::cerr << "FAIL " << name << "\n";
        ++fail;
    }
    else
        std::cout << "PASS " << name << "\n";
}

RefSlot live (int note, uint32_t t, bool keyDown = true, bool sustain = false)
{
    return RefSlot { note, keyDown, sustain, true, t };
}
} // namespace

int main()
{
    using gardenia_core_test::stable_sort_indices_n;

    // --- stable_sort_indices_n directed cases ---
    {
        int empty[1] = { 0 };
        stable_sort_indices_n (empty, 0, [] (int a, int b) { return a < b; });
        expect_true ("sort zero active", true);
    }
    {
        int one[1] = { 7 };
        stable_sort_indices_n (one, 1, [] (int a, int b) { return a < b; });
        expect_eq ("sort one active", one[0], 7);
    }
    {
        int eight[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
        stable_sort_indices_n (eight, 8, [] (int a, int b) { return a < b; });
        bool ok = true;
        for (int i = 0; i < 8; ++i)
            ok = ok && (eight[i] == i);
        expect_true ("sort all eight", ok);
    }
    {
        // Equal keys: preserve input (voice-index) order — historical std::sort
        // was unstable; unique noteOnTime counters make production paths identical.
        int idx[4] = { 3, 1, 2, 0 };
        const uint32_t t[8] = { 5, 5, 5, 5 };
        stable_sort_indices_n (idx, 4, [&] (int a, int b) { return t[a] < t[b]; });
        expect_true ("sort equal timestamps stable",
                     idx[0] == 3 && idx[1] == 1 && idx[2] == 2 && idx[3] == 0);
    }

    // --- steal priority directed cases (independent reference) ---
    {
        std::array<RefSlot, 8> s {};
        expect_eq ("steal zero active falls back", ref_steal (s, 60), 0);
    }
    {
        std::array<RefSlot, 8> s {};
        s[2] = live (60, 1);
        // Same note preference returns that voice when capacity forces steal.
        // With a free slot the allocator would not call steal; here we only
        // exercise the ordering helper with one live voice.
        expect_eq ("steal one active same note", ref_steal (s, 60), 2);
        expect_eq ("steal one active other note", ref_steal (s, 64), 2);
    }
    {
        std::array<RefSlot, 8> s {};
        for (int i = 0; i < 8; ++i)
            s[static_cast<size_t> (i)] = live (48 + i, static_cast<uint32_t> (i + 1));
        // Protect lowest (48@0) and highest (55@7); oldest unprotected is slot 1.
        expect_eq ("steal all eight oldest unprotected", ref_steal (s, 70), 1);
    }
    {
        std::array<RefSlot, 8> s {};
        s[0] = live (60, 1);
        s[1] = live (60, 2); // repeated note, newer
        s[2] = live (64, 3);
        // Prefer matching midi note; oldest matching wins (slot 0).
        expect_eq ("steal repeated note oldest match", ref_steal (s, 60), 0);
    }
    {
        std::array<RefSlot, 8> s {};
        s[0] = live (60, 10);
        s[1] = live (62, 10); // equal timestamp; index order after stable sort
        s[2] = live (64, 10);
        // low=0 (60), top=2 (64) protected; unprotected oldest-in-stable-order is 1.
        expect_eq ("steal equal timestamps deterministic", ref_steal (s, 70), 1);
    }
    {
        std::array<RefSlot, 8> s {};
        for (int i = 0; i < 8; ++i)
            s[static_cast<size_t> (i)] = live (40 + i, static_cast<uint32_t> (i + 1));
        // Oldest-note priority among unprotected held keys.
        expect_eq ("steal oldest-note priority", ref_steal (s, 90), 1);
    }
    {
        std::array<RefSlot, 8> s {};
        for (int i = 0; i < 8; ++i)
            s[static_cast<size_t> (i)] = live (40 + i, static_cast<uint32_t> (i + 1), true, true);
        // All sustain-held; still protect low/top, steal oldest unprotected.
        expect_eq ("steal sustain-held voices", ref_steal (s, 90), 1);
    }
    {
        std::array<RefSlot, 8> s {};
        for (int i = 0; i < 7; ++i)
            s[static_cast<size_t> (i)] = live (40 + i, static_cast<uint32_t> (i + 1));
        // Released (not keyDown, not sustain) middle voice is preferred over held.
        s[3] = live (43, 4, false, false);
        expect_eq ("steal released before held", ref_steal (s, 90), 3);
    }
    {
        // Capacity steal: eight held keys; new note forces oldest unprotected.
        std::array<RefSlot, 8> s {};
        for (int i = 0; i < 8; ++i)
            s[static_cast<size_t> (i)] = live (50 + i, static_cast<uint32_t> (100 + i));
        expect_eq ("steal at capacity", ref_steal (s, 72), 1);
    }
    {
        // Deterministic event ordering: earlier noteOnTime always precedes later.
        std::array<RefSlot, 8> s {};
        s[4] = live (70, 30);
        s[1] = live (60, 10);
        s[6] = live (80, 20);
        s[2] = live (65, 40);
        // Active: 1@10, 6@20, 4@30, 2@40. Protect low=1(60), top=6(80).
        // Unprotected oldest is 4.
        expect_eq ("steal deterministic event order", ref_steal (s, 90), 4);
    }

    std::cout << "VoiceOrderingTests fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
