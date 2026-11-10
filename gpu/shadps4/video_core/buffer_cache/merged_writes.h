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
// without downloading, with the bytes the guest held there at the fault. Guest bytes still
// equal to `bytes` are the GPU's (its copy is newer); bytes that differ were written by the
// guest since (the guest's copy is newer).
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
        const std::size_t count = range.bytes.size();
        std::size_t i = 0;
        while (i < count) {
            if (!Skipped(now[i], range.bytes[i])) {
                ++i;
                continue;
            }
            const std::size_t first = i;
            while (i < count && Skipped(now[i], range.bytes[i])) {
                ++i;
            }
            const std::uint64_t skip_start = std::max(range.address + first, cursor);
            const std::uint64_t skip_end = std::min<std::uint64_t>(range.address + i, end);
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
    bool Skipped(std::uint8_t now, std::uint8_t saved) const {
        return (now == saved) == (skip == Skip::Unchanged);
    }

    std::uint64_t cursor;
    std::uint64_t end;
    Skip skip;
};

} // namespace VideoCore
