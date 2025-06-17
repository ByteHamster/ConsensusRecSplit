#pragma once

#include <array>
#include <cstddef>
#include <cmath>

#include "UnalignedBitVector.h"

namespace consensus {

// sage: print(0, [N(log((2**(2**i))/binomial(2**i, (2**i)/2), 2)) for i in [1..20]], sep=', ')
constexpr std::array<double, 21> optimalBitsForSplit = {0, 1.00000000000000, 1.41503749927884, 1.87071698305503,
            2.34827556689194, 2.83701728740494, 3.33138336299656, 3.82856579982622, 4.32715694302912, 4.82645250522622,
            5.32610028514914, 5.82592417496365, 6.32583611985253, 6.82579209229467, 7.32577007851546, 7.82575907162581,
            8.32575356818099, 8.82575081645857, 9.32574944059737, 9.82574875266676, 10.3257484087015};

static constexpr size_t intLog2(size_t x) {
    return std::bit_width(x) - 1;
}

/**
 * Calculates the storage positions of splits in the splitting tree.
 * The storage has to be in the same order as the search for consensus to work.
 */
template <size_t n, double overhead>
class SplittingTreeStorageLevelwise {
    private:
        static constexpr size_t logn = intLog2(n);

        static constexpr size_t microBitsForSplitOnLevel(size_t level) {
            // MicroBits instead of double to avoid rounding inconsistencies and for much faster evaluation
            double bits = optimalBitsForSplit[logn - level];
            // "Textbook" Consensus would just add the overhead here.
            // Instead, give more overhead to larger levels (where each individual trial is more expensive).
            double size = 1ul << (logn - level);
            bits += overhead / 3.4 * std::pow(size, 0.75);
            return std::ceil(1024.0 * 1024.0 * bits);
        }

        static constexpr std::array<size_t, logn> fillMicroBitsForSplitLookup() {
            std::array<size_t, logn> array;
            for (size_t level = 0; level < logn; level++) {
                array[level] = microBitsForSplitOnLevel(level);
            }
            return array;
        }

        static constexpr std::array<size_t, logn> microBitsForSplitOnLevelLookup = fillMicroBitsForSplitLookup();

    public:
        static size_t seedStartPosition(size_t level, size_t index) {
            return (microBitsForSplitOnLevelLookup[level] * index) / (1024 * 1024);
        }
};

template <size_t k, double overhead, size_t level>
struct SplittingTaskIteratorLevelwise {
    static constexpr size_t logk = intLog2(k);
    static constexpr size_t taskSize = 1ul << (logk - level);
    size_t idx;
    UnalignedBitVector &unalignedBitVector;
    size_t seedEndPos = 0;
    size_t seedWidth = 0;
    uint64_t seedMask = 0;
    uint64_t seed = 0;
    size_t fromKey = 0;
    uint64_t maxSeed = 0;

    explicit SplittingTaskIteratorLevelwise(size_t currentTask, UnalignedBitVector &unalignedBitVector)
            : idx(currentTask), unalignedBitVector(unalignedBitVector) {
        recalculatePositions();
        readSeed();
    }

    void recalculatePositions() {
        size_t seedStartPos = SplittingTreeStorageLevelwise<k, overhead>::seedStartPosition(level, idx);
        seedEndPos = SplittingTreeStorageLevelwise<k, overhead>::seedStartPosition(level, idx + 1);
        seedWidth = seedEndPos - seedStartPos;
        seedMask = (1ul << seedWidth) - 1;
        fromKey = idx * taskSize;
    }

    void readSeed() {
        seed = unalignedBitVector.readAt(seedEndPos);
        maxSeed = seed | seedMask;
    }

    void next() {
        idx++;
        recalculatePositions();
        seed <<= seedWidth;
        maxSeed = seed | seedMask;
    }

    void prev() {
        idx--;
        recalculatePositions();
        readSeed();
    }

    void writeSeed() {
        unalignedBitVector.writeTo(seedEndPos, seed);
    }

    bool isFirst() {
        return idx == 0;
    }
};

} // namespace consensus