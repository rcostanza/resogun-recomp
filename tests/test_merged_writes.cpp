// The byte runs BufferCache moves for pages TryMergeWrite left without a download
// (video_core/buffer_cache/merged_writes.h): uploads leave out GPU bytes the guest has not
// changed, downloads leave out the guest's writes since the merge.
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
#include "video_core/buffer_cache/merged_writes.h"

using VideoCore::MergedRange;
using VideoCore::MergedRangeSplitter;
using Runs = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

static std::uint64_t Addr(const std::uint8_t* p) {
    return std::bit_cast<std::uint64_t>(p);
}

static Runs Split(std::uint64_t addr, std::uint64_t size, MergedRangeSplitter::Skip skip,
                  const std::vector<MergedRange>& ranges) {
    Runs runs;
    const auto emit = [&](std::uint64_t start, std::uint64_t bytes) {
        runs.emplace_back(start, bytes);
    };
    MergedRangeSplitter splitter{addr, size, skip};
    for (const MergedRange& range : ranges) {
        splitter.Add(range, emit);
    }
    splitter.Finish(emit);
    return runs;
}

// The crash: a page held 64 GPU-written bytes (stale floats of a freed buffer). A CPU write
// next to them was merged. The game then reused the memory for a shader header, writing 16
// bytes over the GPU range without a fault. A download of the page must keep those 16 bytes
// and refresh only the 48 the guest left alone.
static void TestDownloadKeepsGuestWrites() {
    alignas(4096) static std::uint8_t page[4096];
    std::memset(page, 0xAA, sizeof(page));                  // the guest's stale copy
    MergedRange range{Addr(page + 256), std::vector<std::uint8_t>(page + 256, page + 320)};
    std::memset(page + 256, 0x11, 16);                      // the shader header, written since

    std::uint8_t gpu[4096];
    std::memset(gpu, 0x3F, sizeof(gpu));                    // the GPU's floats
    const auto write = [&](std::uint64_t start, std::uint64_t bytes) {
        std::memcpy(std::bit_cast<std::uint8_t*>(start), gpu + (start - Addr(page)), bytes);
    };
    MergedRangeSplitter splitter{Addr(page), sizeof(page), MergedRangeSplitter::Skip::Changed};
    splitter.Add(range, write);
    splitter.Finish(write);

    for (int i = 256; i < 272; ++i) {
        assert(page[i] == 0x11);
    }
    for (int i = 272; i < 320; ++i) {
        assert(page[i] == 0x3F);
    }
    assert(page[0] == 0x3F && page[4095] == 0x3F);
}

static void TestUploadSkipsGpuBytes() {
    alignas(64) static std::uint8_t page[256];
    std::memset(page, 0, sizeof(page));
    MergedRange range{Addr(page + 64), std::vector<std::uint8_t>(page + 64, page + 128)};
    page[80] = 1;                                           // the guest changed one GPU byte
    const std::uint64_t base = Addr(page);
    const Runs runs = Split(base, sizeof(page), MergedRangeSplitter::Skip::Unchanged, {range});
    assert((runs == Runs{{base, 64}, {base + 80, 1}, {base + 128, 128}}));
}

static void TestDownloadRunsAndClipping() {
    alignas(64) static std::uint8_t page[256];
    std::memset(page, 0, sizeof(page));
    std::vector<MergedRange> ranges{
        {Addr(page + 16), std::vector<std::uint8_t>(page + 16, page + 48)},
        {Addr(page + 96), std::vector<std::uint8_t>(page + 96, page + 112)},
    };
    page[20] = page[21] = 7;                                // guest writes in the first range
    page[100] = 9;                                          // and in the second
    const std::uint64_t base = Addr(page);
    const Runs all = Split(base, sizeof(page), MergedRangeSplitter::Skip::Changed, ranges);
    assert((all == Runs{{base, 20}, {base + 22, 78}, {base + 101, 155}}));
    // A copy covering only part of the page: the runs stay inside it.
    const Runs part = Split(base + 21, 60, MergedRangeSplitter::Skip::Changed, ranges);
    assert((part == Runs{{base + 22, 59}}));
    // Nothing changed: the whole range goes through.
    page[20] = page[21] = page[100] = 0;
    const Runs none = Split(base, sizeof(page), MergedRangeSplitter::Skip::Changed, ranges);
    assert((none == Runs{{base, 256}}));
}

int main() {
    TestDownloadKeepsGuestWrites();
    TestUploadSkipsGpuBytes();
    TestDownloadRunsAndClipping();
}
