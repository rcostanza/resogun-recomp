// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace VideoCore {

// resogun: a GPU-written range of a page that BufferCache::TryMergeWrite let a CPU write into
// without downloading, with the bytes the guest held there at the fault. An aligned 8-byte word
// still equal to `bytes` is the GPU's (its copy is newer); one with any byte that differs was
// written by the guest since (the guest's copy is newer). By word, not byte: a write rarely
// changes every byte, and the ones it left as they were (a pointer's zero high bytes over a
// zero) would take the GPU's (two bytes of a float in a shader header's code address: crash at
// eboot 0x69d459, 0x3f3d0010981e4000).
struct MergedRange {
    std::uint64_t address;
    std::vector<std::uint8_t> bytes;
};

// resogun: splits [addr, end) into the runs to transfer, leaving out bytes of merged ranges:
// uploads leave out the bytes the guest has not changed (SkipUnchanged: the GPU's are newer),
// downloads the bytes it has changed (SkipChanged: a download would put back the GPU's stale
// copy over the guest's newer write). Feed ranges in ascending address order, then Finish.
class MergedRangeSplitter {
public:
    enum class Skip { Unchanged, Changed };

    MergedRangeSplitter(std::uint64_t addr, std::uint64_t size, Skip skip)
        : cursor{addr}, end{addr + size}, skip{skip} {}

    /// Emits the bytes before `range` not emitted yet, then those of `range` not skipped.
    template <typename Emit>
    void Add(const MergedRange& range, Emit&& emit) {
        const auto* now = std::bit_cast<const std::uint8_t*>(range.address);
        const std::uint64_t range_end = range.address + range.bytes.size();
        std::uint64_t at = range.address;
        while (at < range_end) {
            if (!Skipped(now, range, at)) {
                at = WordEnd(at, range_end);
                continue;
            }
            const std::uint64_t first = at;
            while (at < range_end && Skipped(now, range, at)) {
                at = WordEnd(at, range_end);
            }
            const std::uint64_t skip_start = std::max(first, cursor);
            const std::uint64_t skip_end = std::min(at, end);
            if (skip_start >= skip_end) {
                continue;
            }
            if (skip_start > cursor) {
                emit(cursor, skip_start - cursor);
            }
            cursor = skip_end;
        }
    }

    /// Emits what is left after the last range.
    template <typename Emit>
    void Finish(Emit&& emit) {
        if (cursor < end) {
            emit(cursor, end - cursor);
        }
        cursor = end;
    }

private:
    static constexpr std::uint64_t Word = 8;

    static std::uint64_t WordEnd(std::uint64_t at, std::uint64_t range_end) {
        return std::min((at | (Word - 1)) + 1, range_end);
    }

    /// The part of the aligned word at `at` inside `range`: skipped by this transfer?
    bool Skipped(const std::uint8_t* now, const MergedRange& range, std::uint64_t at) const {
        const std::uint64_t offset = at - range.address;
        const std::uint64_t count = WordEnd(at, range.address + range.bytes.size()) - at;
        const bool unchanged = std::equal(now + offset, now + offset + count, range.bytes.data() + offset);
        return unchanged == (skip == Skip::Unchanged);
    }

    std::uint64_t cursor;
    std::uint64_t end;
    Skip skip;
};

} // namespace VideoCore
