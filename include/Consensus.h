#pragma once
#include <cstdint>
#include <vector>
#include <fstream>
#include <span>

#include <ips2ra.hpp>
#include <bytehamster/util/MurmurHash64.h>
#include <bytehamster/util/Function.h>

#include "consensus/UnalignedBitVector.h"

namespace consensus {

// sage: print(0, [N(log((2**(2**i))/binomial(2**i, (2**i)/2), 2)) for i in [1..20]], sep=', ')
constexpr std::array<double, 21> optimalBitsForSplit = {0, 1.00000000000000, 1.41503749927884, 1.87071698305503,
            2.34827556689194, 2.83701728740494, 3.33138336299656, 3.82856579982622, 4.32715694302912, 4.82645250522622,
            5.32610028514914, 5.82592417496365, 6.32583611985253, 6.82579209229467, 7.32577007851546, 7.82575907162581,
            8.32575356818099, 8.82575081645857, 9.32574944059737, 9.82574875266676, 10.3257484087015};

static constexpr size_t intLog2(size_t x) {
    return std::bit_width(x) - 1;
}

template <size_t n, double overhead>
struct SplittingTreeStorage {
    static constexpr size_t logn = intLog2(n);

    static constexpr std::array<double, logn> fillFractionalBits() {
        std::array<double, logn> array;
        for (size_t level = 0; level < logn; level++) {
            array[level] = optimalBitsForSplit[logn - level] + overhead;
        }
        return array;
    }

    static constexpr std::array<double, logn> fractionalBitsForSplitOnLevel = fillFractionalBits();

    static size_t seedStartPosition(size_t level, size_t index) {
        // Warning: Trying to lazily determine the position of an adjacent task using floating point
        // calculations can lead to different results than calling this method with the next task.
        double bits = 0;
        for (size_t l = 0; l < level; l++) {
            bits += fractionalBitsForSplitOnLevel[l] * (1ul << l);
        }
        return std::ceil(bits + fractionalBitsForSplitOnLevel[level] * index);
    }

    static size_t totalSize() {
        // Warning: Trying to lazily determine the position of an adjacent task using floating point
        // calculations can lead to different results than calling this method with the next task.
        double bits = 0;
        for (size_t l = 0; l < logn; l++) {
            bits += fractionalBitsForSplitOnLevel[l] * (1ul << l);
        }
        return std::ceil(bits);
    }
};

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
    uint64_t currentSeed = 0;
    uint64_t maxSeed = 0;
    UnalignedBitVector &storage;
    size_t positionOffset;

    SplittingTask(size_t level, size_t index, UnalignedBitVector &storage, size_t positionOffset)
            : level(level), index(index), storage(storage), positionOffset(positionOffset) {
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
        currentSeed = storage.readAt(endPosition + positionOffset);
        maxSeed = currentSeed | seedMask;
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

    void setSeed(uint64_t seed) {
        currentSeed = seed;
        storage.writeTo(endPosition + positionOffset, currentSeed);
    }

    void resetSeed() {
        setSeed(currentSeed & ~seedMask);
    }

    void setLevel(size_t level_) {
        level = level_;
        updateProperties();
    }
};

template <size_t n, double overhead>
class Consensus {
    public:
        static_assert(1ul << intLog2(n) == n, "n must be a power of 2");
        static_assert(overhead > 0);
        static constexpr size_t ROOT_SEED_BITS = 64;
        static constexpr size_t logn = intLog2(n);
        UnalignedBitVector unalignedBitVector;

        explicit Consensus(std::span<const uint64_t> keys)
                : unalignedBitVector(ROOT_SEED_BITS + SplittingTreeStorage<n, overhead>::totalSize()) {
            if (keys.size() != n) {
                throw std::logic_error("Wrong input size");
            }
            std::cout << "Tree space: " << SplittingTreeStorage<n, overhead>::totalSize() << std::endl;
            std::vector<uint64_t> modifiableKeys(keys.begin(), keys.end());

            for (size_t rootSeed = 0; rootSeed < (1ul << (ROOT_SEED_BITS - 1)); rootSeed++) {
                unalignedBitVector.writeTo(ROOT_SEED_BITS, rootSeed);
                if (construct(modifiableKeys, ROOT_SEED_BITS)) {
                    return;
                }
            }
            throw std::logic_error("Unable to construct");
        }

        [[nodiscard]] size_t getBits() const {
            return unalignedBitVector.bitSize();
        }

        [[nodiscard]] size_t operator()(const std::string &key) const {
            return this->operator()(bytehamster::util::MurmurHash64(key));
        }

        [[nodiscard]] size_t operator()(uint64_t key) {
            SplittingTask<n, overhead> task(0, 0, unalignedBitVector, ROOT_SEED_BITS);
            for (size_t level = 0; level < logn; level++) {
                task.setLevel(level);
                if (toLeft(key, task.currentSeed)) {
                    task.index = 2 * task.index;
                } else {
                    task.index = 2 * task.index + 1;
                }
            }
            return task.index;
        }

    private:
        bool construct(std::span<uint64_t> keys, size_t storagePositionOffset) {
            SplittingTask<n, overhead> task(0, 0, unalignedBitVector, storagePositionOffset);
            while (!task.isEnd()) {
                std::span<uint64_t> keysThisTask = keys.subspan(task.index * task.taskSizeThisLevel, task.taskSizeThisLevel);
                bool success = false;
                uint64_t seed;
                for (seed = task.currentSeed; seed <= task.maxSeed; seed++) {
                    if (isSeedSuccessful(keysThisTask, seed)) {
                        success = true;
                        break;
                    }
                }
                if (success) {
                    std::partition(keysThisTask.begin(), keysThisTask.end(),
                                   [&](uint64_t key) { return toLeft(key, seed); });
                    task.setSeed(seed);
                    task.next();
                } else {
                    do {
                        task.resetSeed();
                        if (task.isFirst()) {
                            return false;
                        }
                        task.previous();
                    } while (task.currentSeed == task.maxSeed);
                    task.currentSeed++;
                }
            }
            return true;
        }

        bool isSeedSuccessful(std::span<uint64_t> keys, uint64_t seed) {
            size_t numToLeft = std::accumulate(keys.begin(), keys.end(), 0,
                       [&](size_t sum, uint64_t key) { return sum + toLeft(key, seed); });
            return numToLeft == (keys.size() / 2);
        }

        [[nodiscard]] bool toLeft(uint64_t key, uint64_t seed) const {
            return bytehamster::util::remix(key + seed) % 2;
        }
};
} // namespace consensus
