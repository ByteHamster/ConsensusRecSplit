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

// sage: print(0, [N((2**(2**i))/binomial(2**i, (2**i)/2)) for i in [1..10]], sep=', ')
constexpr std::array<double, 11> optimalBitsForSplit = {0, 2.00000000000000, 2.66666666666667, 3.65714285714286,
            5.09215229215229, 7.14541243975702, 10.0657541618008, 14.2073521795263, 20.0726187457350, 28.3731122826281, 40.1158451045879};

static constexpr size_t intLog2(size_t x) {
    return std::bit_width(x) - 1;
}

template <size_t n, double overhead>
struct SplittingTask {
    static constexpr size_t logn = intLog2(n);

    static constexpr std::array<double, logn> fillFractionalBits() {
        std::array<double, logn> array;
        for (size_t level = 0; level < logn; level++) {
            array[level] = optimalBitsForSplit[logn - level] + overhead;
        }
        return array;
    }

    static constexpr std::array<double, logn> fractionalBitsForSplitOnLevel = fillFractionalBits();

    size_t level;
    size_t index;
    size_t taskSizeThisLevel = 0;
    size_t tasksThisLevel = 0;
    uint64_t seedMask = 0;
    uint64_t currentSeed = 0;
    uint64_t lastSeed = 0;
    UnalignedBitVector &storage;

    SplittingTask(size_t level, size_t index, UnalignedBitVector &storage)
            : level(level), index(index), storage(storage) {
        updateProperties();
    }

    void updateProperties() {
        taskSizeThisLevel = 1ul << (logn - level);
        tasksThisLevel = n / taskSizeThisLevel;
        SplittingTask nextTask = *this;
        nextTask.next();
        size_t startPosition = seedStartPosition();
        size_t seedWidth = nextTask.seedStartPosition() - startPosition;
        seedMask = ((1ul << seedWidth) - 1);
        currentSeed = storage.readAt(startPosition);
        lastSeed = currentSeed | seedMask;
    }

    size_t seedStartPosition() {
        // Warning: Trying to lazily determine the position of an adjacent task using floating point
        // calculations can lead to different results than calling this method with the next task.
        double bits = 0;
        for (size_t l = 0; l < level; l++) {
            bits += fractionalBitsForSplitOnLevel[l] * (1ul << l);
        }
        return bits + fractionalBitsForSplitOnLevel[level] * index;
    }

    void next() {
        index++;
        if (index == tasksThisLevel) {
            index = 0;
            level++;
            updateProperties();
        }
    }

    void previous() {
        if (index == 0) {
            level--;
            updateProperties();
            index = tasksThisLevel;
        } else {
            index--;
        }
    }

    bool isEnd() {
        return level >= logn;
    }

    bool isFirst() {
        return level + index == 0;
    }

    void setSeed(uint64_t seed) {
        currentSeed = seed;
        storage.writeTo(seedStartPosition(), currentSeed);
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
        static constexpr size_t logn = intLog2(n);
        UnalignedBitVector unalignedBitVector;

        explicit Consensus(std::span<const uint64_t> keys)
                : unalignedBitVector(n * 1.5f) { // TODO use a proper size
            if (keys.size() != n) {
                throw std::logic_error("Wrong input size");
            }
            std::vector<uint64_t> modifiableKeys(keys.begin(), keys.end());

            for (size_t rootSeed = 0; rootSeed < 1000; rootSeed++) {
                if (construct(modifiableKeys)) {
                    return;
                }
            }
        }

        [[nodiscard]] size_t getBits() const {
            return unalignedBitVector.bitSize();
        }

        [[nodiscard]] size_t operator()(const std::string &key) const {
            return this->operator()(bytehamster::util::MurmurHash64(key));
        }

        [[nodiscard]] size_t operator()(uint64_t key) {
            SplittingTask<n, overhead> task(0, 0, unalignedBitVector);
            for (size_t level = 0; level < logn; level++) {
                task.setLevel(level);
                if (!toLeft(key, task.currentSeed)) {
                    task.index += n / (1ul << level);
                }
            }
            return task.index;
        }

    private:
        bool construct(std::span<uint64_t> keys) {
            SplittingTask<n, overhead> task(0, 0, unalignedBitVector);
            while (!task.isEnd()) {
                std::span<uint64_t> keysThisTask = keys.subspan(task.index * task.taskSizeThisLevel, task.taskSizeThisLevel);
                bool success = false;
                uint64_t seed;
                for (seed = task.currentSeed; seed < task.lastSeed; seed++) {
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
                    task.resetSeed();
                    if (task.isFirst()) {
                        return false;
                    }
                    task.previous();
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
