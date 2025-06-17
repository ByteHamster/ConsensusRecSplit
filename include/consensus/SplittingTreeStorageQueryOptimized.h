#pragma once

#include <array>
#include <cstddef>
#include <cmath>

#include "UnalignedBitVector.h"
#include "SplittingTreeStorageLevelwise.h"

namespace consensus {

/**
 * Calculates the storage positions of splits in the splitting tree.
 * The storage has to be in the same order as the search for consensus to work.
 */
template <size_t n, double overhead>
class SplittingTreeStorageQueryOptimized {
    private:
        static constexpr size_t logn = intLog2(n);
        static constexpr auto microBitsForSplitOnLevelLookup = SplittingTreeStorageLevelwise<n, overhead>::microBitsForSplitOnLevelLookup;

        static constexpr std::array<size_t, logn + 1> fillMicroBitsLevelSize() {
            std::array<size_t, logn + 1> array;
            size_t microBits = 0;
            for (size_t level = 0; level < logn; level++) {
                array[level] = microBits;
                microBits += microBitsForSplitOnLevelLookup[level] * (1ul << level);
            }
            array[logn] = microBits;
            return array;
        }

        static constexpr std::array<size_t, logn + 1> microBitsLevelSize = fillMicroBitsLevelSize();

    public:
        static size_t seedStartPosition(size_t level, size_t index) {
            return (microBitsLevelSize[level] + microBitsForSplitOnLevelLookup[level] * index) / (1024 * 1024);
        }

        static constexpr size_t totalSize() {
            return microBitsLevelSize[logn] / (1024 * 1024);
        }
};

/**
 * Represents a splitting task.
 * Calculates the order in which to search tasks (and their storage location).
 * The storage has to be in the same order as the search for consensus to work.
 */
template <size_t n, double overhead>
struct SplittingTaskIteratorQueryOptimized {
    static constexpr size_t logn = intLog2(n);
    using TreeStorage = SplittingTreeStorageQueryOptimized<n, overhead>;

    size_t level;
    size_t index;
    size_t bucket;
    const size_t nbuckets;
    size_t taskSizeThisLevel = 0;
    size_t tasksThisLevel = 0;
    size_t endPosition = 0;
    size_t seedWidth = 0;
    uint64_t seedMask = 0;

    SplittingTaskIteratorQueryOptimized(size_t level, size_t index, size_t bucket, size_t nbuckets)
            : level(level), index(index), bucket(bucket), nbuckets(nbuckets) {
        updateProperties();
    }

    void updateProperties() {
        taskSizeThisLevel = 1ul << (logn - level);
        tasksThisLevel = n / taskSizeThisLevel;
        size_t startPosition = bucket * TreeStorage::totalSize() + TreeStorage::seedStartPosition(level, index);
        if (index + 1 < tasksThisLevel) {
            endPosition = bucket * TreeStorage::totalSize() + TreeStorage::seedStartPosition(level, index + 1);
        } else {
            endPosition = bucket * TreeStorage::totalSize() + TreeStorage::seedStartPosition(level + 1, 0);
        }
        seedWidth = endPosition - startPosition;
        seedMask = ((1ul << seedWidth) - 1);
    }

    void next() {
        index++;
        if (index == tasksThisLevel) {
            index = 0;
            level++;
            if (level == logn) {
                level = 0;
                bucket++;
            }
        }
        updateProperties();
    }

    void previous() {
        if (index == 0) {
            if (level == 0) {
                level = logn - 1;
                bucket--;
            } else {
                level--;
            }
            index = n / (1ul << (logn - level)) - 1;
        } else {
            index--;
        }
        updateProperties();
    }

    bool isEnd() {
        return bucket >= nbuckets;
    }

    bool isFirst() {
        return level + index + bucket == 0;
    }

    void setLevel(size_t level_) {
        level = level_;
        updateProperties();
    }
};

} // namespace consensus