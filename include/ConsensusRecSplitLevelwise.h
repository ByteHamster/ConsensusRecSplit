#pragma once

#include <cstdint>
#include <vector>
#include <fstream>
#include <span>

#include <ips2ra.hpp>
#include <bytehamster/util/MurmurHash64.h>
#include <bytehamster/util/Function.h>

#include "consensus/UnalignedBitVector.h"
#include "consensus/SplittingTreeStorage.h"
#include "consensus/BumpedKPerfectHashFunction.h"

namespace consensus {

// Starting seed at given distance from the root (extracted at random).
static constexpr uint64_t startSeed[] = {0x106393c187cae21a, 0x6453cec3f7376937, 0x643e521ddbd2be98, 0x3740c6412f6572cb, 0x717d47562f1ce470, 0x4cd6eb4c63befb7c, 0x9bfd8c5e18c8da73,
      0x082f20e10092a9a3, 0x2ada2ce68d21defc, 0xe33cb4f3e7c6466b, 0x3980be458c509c59, 0xc466fd9584828e8c, 0x45f0aabe1a61ede6, 0xf6e7b8b33ad9b98d,
      0x4ef95e25f4b4983d, 0x81175195173b92d3, 0x4e50927d8dd15978, 0x1ea2099d1fafae7f, 0x425c8a06fbaaa815, 0xcd4216006c74052a};

/**
 * Perfect hash function using the consensus idea: Combined search and encoding of successful seeds.
 * Level-wise construction: Faster construction but has more cache faults when querying.
 * <code>k</code> is the size of each RecSplit base case and must be a power of 2.
 */
template <size_t k, double overhead>
class ConsensusRecSplitLevelwise {
    public:
        static_assert(1ul << intLog2(k) == k, "k must be a power of 2");
        static_assert(overhead > 0);
        static constexpr size_t ROOT_SEED_BITS = 64;
        static constexpr size_t logk = intLog2(k);
        size_t numKeys = 0;
        std::array<UnalignedBitVector, logk> unalignedBitVectors;
        BumpedKPerfectHashFunction<k> *bucketingPhf = nullptr;

        explicit ConsensusRecSplitLevelwise(std::span<const std::string> keys) : numKeys(keys.size()) {
            std::vector<uint64_t> hashedKeys;
            hashedKeys.reserve(keys.size());
            for (const std::string &key : keys) {
                hashedKeys.push_back(bytehamster::util::MurmurHash64(key));
            }
            startSearch(hashedKeys);
        }

        explicit ConsensusRecSplitLevelwise(std::span<const uint64_t> keys) : numKeys(keys.size()) {
            startSearch(keys);
        }

        ~ConsensusRecSplitLevelwise() {
            delete bucketingPhf;
        }

        [[nodiscard]] size_t getBits() const {
            size_t bits = 0;
            for (const UnalignedBitVector &v : unalignedBitVectors) {
                bits += v.bitSize();
            }
            return bits + bucketingPhf->getBits();
        }

        [[nodiscard]] size_t operator()(const std::string &key) const {
            return this->operator()(bytehamster::util::MurmurHash64(key));
        }

        [[nodiscard]] size_t operator()(uint64_t key) const {
            size_t nbuckets = numKeys / k;
            size_t bucket = bucketingPhf->operator()(key);
            if (bucket >= nbuckets) {
                return bucket; // Fallback if numKeys does not divide n
            }
            size_t taskIdx = bucket;
            for (size_t level = 0; level < logk; level++) {
                size_t seedEndPos = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, taskIdx + 1);
                uint64_t seed = unalignedBitVectors.at(level).readAt(seedEndPos + ROOT_SEED_BITS);
                if (toLeft(key, seed + startSeed[level])) {
                    taskIdx = 2 * taskIdx;
                } else {
                    taskIdx = 2 * taskIdx + 1;
                }
            }
            return taskIdx;
        }

    private:
        void startSearch(std::span<const uint64_t> keys) {
            bucketingPhf = new BumpedKPerfectHashFunction<k>(keys);
            size_t nbuckets = keys.size() / k;
            std::vector<size_t> counters(nbuckets);
            std::vector<uint64_t> modifiableKeys(nbuckets * k); // Note that this is possibly fewer than n
            for (uint64_t key : keys) {
                size_t bucket = bucketingPhf->operator()(key);
                if (bucket >= nbuckets) {
                    continue; // No need to handle this key
                }
                modifiableKeys.at(bucket * k + counters.at(bucket)) = key;
                counters.at(bucket)++;
            }
            #ifndef NDEBUG
                for (size_t counter : counters) {
                    assert(counter == k);
                }
            #endif

            for (size_t level = 0; level < logk; level++) {
                findSeedsForLevel(modifiableKeys, level);

                size_t taskSize = 1ul << (logk - level);
                assert(modifiableKeys.size() % taskSize == 0);
                size_t numTasks = modifiableKeys.size() / taskSize;
                if (taskSize > 2) {
                    for (size_t task = 0; task < numTasks; task++) {
                        size_t seedEndPos = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, task + 1);
                        uint64_t seed = unalignedBitVectors.at(level).readAt(seedEndPos + ROOT_SEED_BITS) + startSeed[level];
                        std::partition(modifiableKeys.begin() + task * taskSize,
                                       modifiableKeys.begin() + (task + 1) * taskSize,
                                       [&](uint64_t key) { return toLeft(key, seed); });
                    }
                }
            }
        }

        struct TaskInfo {
            size_t seedStartPos;
            size_t seedEndPos;
            uint64_t seedMask;

            TaskInfo(size_t level, size_t currentTask) {
                seedStartPos = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, currentTask);
                seedEndPos = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, currentTask + 1);
                seedMask = (1ul << (seedEndPos - seedStartPos)) - 1;
            }
        };

        void findSeedsForLevel(const std::vector<uint64_t> &keys, size_t level) {
            size_t taskSize = 1ul << (logk - level);
            size_t numTasks = keys.size() / taskSize;

            size_t bitsThisLevel = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, numTasks);
            UnalignedBitVector &unalignedBitVector = unalignedBitVectors.at(level);
            unalignedBitVector.clearAndResize(ROOT_SEED_BITS + bitsThisLevel);

            size_t currentTask = 0;
            while (true) {
                size_t fromKey = currentTask * taskSize;
                size_t toKey = (currentTask + 1) * taskSize;
                TaskInfo taskInfo(level, currentTask);
                uint64_t taskSeed = unalignedBitVector.readAt(taskInfo.seedEndPos + ROOT_SEED_BITS);
                uint64_t taskMaxSeed = taskSeed | taskInfo.seedMask;

                while (true) { // Test all seeds of this task
                    if (isSeedSuccessful(keys, fromKey, toKey, taskSeed + startSeed[level])) {
                        unalignedBitVector.writeTo(taskInfo.seedEndPos + ROOT_SEED_BITS, taskSeed);
                        currentTask++;
                        if (currentTask == numTasks) {
                            return; // Success
                        }
                        break; // Next task
                    } else if (taskSeed != taskMaxSeed) {
                        taskSeed++;
                    } else { // Backtrack
                        while ((taskSeed & taskInfo.seedMask) == taskInfo.seedMask) {
                            taskSeed &= ~taskInfo.seedMask;
                            unalignedBitVector.writeTo(taskInfo.seedEndPos + ROOT_SEED_BITS, taskSeed);
                            if (currentTask == 0) {
                                uint64_t rootSeed = unalignedBitVector.readAt(ROOT_SEED_BITS);
                                unalignedBitVector.writeTo(ROOT_SEED_BITS, rootSeed + 1);
                                taskSeed = unalignedBitVector.readAt(taskInfo.seedEndPos + ROOT_SEED_BITS);
                                break;
                            }
                            currentTask--;
                            taskInfo = TaskInfo(level, currentTask);
                            taskSeed = unalignedBitVector.readAt(taskInfo.seedEndPos + ROOT_SEED_BITS);
                        }
                        taskSeed++;
                        unalignedBitVector.writeTo(taskInfo.seedEndPos + ROOT_SEED_BITS, taskSeed);
                        break; // Next task
                    }
                }

            }
        }

        bool isSeedSuccessful(const std::vector<uint64_t> &keys, size_t from, size_t to, uint64_t seed) {
            size_t numToLeft = 0;
            for (size_t i = from; i < to; i++) {
                numToLeft += toLeft(keys[i], seed);
            }
            return numToLeft == (to - from) / 2;
        }

        [[nodiscard]] static bool toLeft(uint64_t key, uint64_t seed) {
            return bytehamster::util::remix(key + seed) % 2;
        }
};
} // namespace consensus
