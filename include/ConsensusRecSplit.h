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
/**
 * Perfect hash function using the consensus idea: Combined search and encoding of successful seeds.
 * <code>k</code> is the size of each RecSplit base case and must be a power of 2.
 */
template <size_t k, double overhead>
class ConsensusRecSplit {
    public:
        static_assert(1ul << intLog2(k) == k, "k must be a power of 2");
        static_assert(overhead > 0);
        static constexpr size_t logk = intLog2(k);
        size_t numKeys = 0;
        std::array<UnalignedBitVector, logk> unalignedBitVectors;
        BumpedKPerfectHashFunction<k> *bucketingPhf = nullptr;

        explicit ConsensusRecSplit(std::span<const std::string> keys) : numKeys(keys.size()) {
            std::vector<uint64_t> hashedKeys;
            hashedKeys.reserve(keys.size());
            for (const std::string &key : keys) {
                hashedKeys.push_back(bytehamster::util::MurmurHash64(key));
            }
            startSearch(hashedKeys);
        }

        explicit ConsensusRecSplit(std::span<const uint64_t> keys) : numKeys(keys.size()) {
            startSearch(keys);
        }

        ~ConsensusRecSplit() {
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
                uint64_t seed = unalignedBitVectors.at(level).readAt(seedEndPos);
                if (toLeft(key, seed)) {
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

            constructLevel<0>(modifiableKeys);
        }

        template <size_t level>
        void constructLevel(std::vector<uint64_t> &keys) {
            constexpr size_t taskSize = 1ul << (logk - level);

            auto beginConstruction = std::chrono::high_resolution_clock::now();
            findSeedsForLevel<level>(keys);

            if constexpr (taskSize > 2) {
                assert(keys.size() % taskSize == 0);
                size_t numTasks = keys.size() / taskSize;
                for (size_t task = 0; task < numTasks; task++) {
                    size_t seedEndPos = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, task + 1);
                    uint64_t seed = unalignedBitVectors.at(level).readAt(seedEndPos);
                    std::partition(keys.begin() + task * taskSize,
                                   keys.begin() + (task + 1) * taskSize,
                                   [&](uint64_t key) { return toLeft(key, seed); });
                }
            }
            unsigned long constructionDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - beginConstruction).count();
            size_t numTasks = keys.size() / taskSize;
            size_t bitsThisLevel = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, numTasks);
            std::cout<<"Level "<<level<<" ("<<taskSize<<" keys each): "<<constructionDurationMs<<" ms, "
                        <<(1000*constructionDurationMs/bitsThisLevel)<<" us per output bit"<<std::endl;

            if constexpr (level + 1 < logk) {
                constructLevel<level + 1>(keys);
            }
        }

        template <size_t level>
        void findSeedsForLevel(const std::vector<uint64_t> &keys) {
            static_assert(level < logk);
            constexpr size_t taskSize = 1ul << (logk - level);
            size_t numTasks = keys.size() / taskSize;

            size_t bitsThisLevel = SplittingTreeStorage<k, overhead>::seedStartPositionLevelwise(level, numTasks);
            UnalignedBitVector &unalignedBitVector = unalignedBitVectors.at(level);
            unalignedBitVector.clearAndResize(bitsThisLevel);

            SplittingTaskIteratorLevelwise<k, overhead, level> task(0, unalignedBitVector);
            while (true) {
                if (isSeedSuccessful<taskSize>(keys, task.fromKey, task.seed)) {
                    task.writeSeed();
                    if (task.idx + 1 == numTasks) [[unlikely]] {
                        return; // Success
                    }
                    task.next();
                } else if (task.seed != task.maxSeed) {
                    task.seed++;
                } else { // Backtrack
                    while (task.seed == task.maxSeed && !task.isFirst()) {
                        task.prev();
                    }
                    if (task.isFirst() && task.seed == task.maxSeed) [[unlikely]] {
                        // Clear task seed and increment root seed
                        task.seed &= ~task.seedMask;
                        task.writeSeed();
                        uint64_t rootSeed = unalignedBitVector.readRootSeed();
                        unalignedBitVector.writeRootSeed(rootSeed + 1);
                        task.readSeed();
                    } else {
                        task.seed++;
                    }
                }
            }
        }

        template <size_t n>
        bool isSeedSuccessful(const std::vector<uint64_t> &keys, size_t from, uint64_t seed) {
            size_t numToLeft = 0;
            for (size_t i = 0; i < n; i++) {
                numToLeft += toLeft(keys[from + i], seed);
            }
            return numToLeft == n / 2;
        }

        [[nodiscard]] static bool toLeft(uint64_t key, uint64_t seed) {
            return bytehamster::util::remix(key + seed) % 2;
        }
};
} // namespace consensus
