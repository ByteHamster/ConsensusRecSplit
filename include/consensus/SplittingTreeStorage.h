#pragma once

#include <array>
#include <cstddef>
#include <cmath>

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
class SplittingTreeStorage {
    private:
        static constexpr size_t logn = intLog2(n);

        static constexpr std::array<size_t, logn> fillMicroBitsForSplit() {
            // MicroBits instead of double to avoid rounding inconsistencies and for much faster evaluation
            std::array<size_t, logn> array;
            for (size_t level = 0; level < logn; level++) {
                double size = 1ul << (logn - level);
                double bits = optimalBitsForSplit[logn - level] + overhead / 3.4 * std::pow(size, 0.75);
                // "Textbook" Consensus would just add the overhead here.
                // Instead, give more overhead to larger levels (where each individual trial is more expensive).
                array[level] = std::ceil(1024.0 * 1024.0 * bits);
            }
            return array;
        }

        static constexpr std::array<size_t, logn> microBitsForSplitOnLevel = fillMicroBitsForSplit();

        static constexpr std::array<size_t, logn + 1> fillMicroBitsLevelSize() {
            std::array<size_t, logn + 1> array;
            size_t microBits = 0;
            for (size_t level = 0; level < logn; level++) {
                array[level] = microBits;
                microBits += microBitsForSplitOnLevel[level] * (1ul << level);
            }
            array[logn] = microBits;
            return array;
        }

        static constexpr std::array<size_t, logn + 1> microBitsLevelSize = fillMicroBitsLevelSize();

    public:
        static size_t seedStartPosition(size_t level, size_t index) {
            return (microBitsLevelSize[level] + microBitsForSplitOnLevel[level] * index + 1024 * 1024 - 1) / (1024 * 1024);
        }

        static constexpr size_t totalSize() {
            return (microBitsLevelSize[logn] + 1024 * 1024 - 1) / (1024 * 1024);
        }
};

/**
 * Represents a splitting task.
 * Calculates the order in which to search tasks (and their storage location).
 * The storage has to be in the same order as the search for consensus to work.
 */
template <size_t n, double overhead>
struct SplittingTask {
    static constexpr size_t logn = intLog2(n);

    size_t level;
    size_t index;
    size_t taskSizeThisLevel = 0;
    size_t tasksThisLevel = 0;
    size_t endPosition = 0;
    size_t seedWidth = 0;
    uint64_t seedMask = 0;

    SplittingTask(size_t level, size_t index) : level(level), index(index) {
        updateProperties();
    }

    void updateProperties() {
        taskSizeThisLevel = 1ul << (logn - level);
        tasksThisLevel = n / taskSizeThisLevel;
        size_t startPosition = SplittingTreeStorage<n, overhead>::seedStartPosition(level, index);
        if (index + 1 < tasksThisLevel) {
            endPosition = SplittingTreeStorage<n, overhead>::seedStartPosition(level, index + 1);
        } else {
            endPosition = SplittingTreeStorage<n, overhead>::seedStartPosition(level + 1, 0);
        }
        seedWidth = endPosition - startPosition;
        seedMask = ((1ul << seedWidth) - 1);
    }

    void next() {
        index++;
        if (index == tasksThisLevel) {
            index = 0;
            level++;
        }
        updateProperties();
    }

    void previous() {
        if (index == 0) {
            level--;
            index = n / (1ul << (logn - level)) - 1;
        } else {
            index--;
        }
        updateProperties();
    }

    bool isEnd() {
        return level >= logn;
    }

    bool isFirst() {
        return level + index == 0;
    }

    void setLevel(size_t level_) {
        level = level_;
        updateProperties();
    }
};

} // namespace consensus