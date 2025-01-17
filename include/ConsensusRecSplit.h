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
 * <code>n</code> is the size of each RecSplit base case and must be a power of 2.
 */
template <size_t n, double overhead>
class ConsensusRecSplit {
    public:
        static_assert(1ul << intLog2(n) == n, "n must be a power of 2");
        static_assert(overhead > 0);
        static constexpr size_t ROOT_SEED_BITS = 64;
        static constexpr size_t logn = intLog2(n);
        size_t numKeys = 0;
        UnalignedBitVector unalignedBitVector;
        BumpedKPerfectHashFunction<n> *bucketingPhf = nullptr;

        explicit ConsensusRecSplit(std::span<const std::string> keys)
                : numKeys(keys.size()),
                  unalignedBitVector(ROOT_SEED_BITS + (numKeys / n) * SplittingTreeStorage<n, overhead>::totalSize()) {
            std::vector<uint64_t> hashedKeys;
            hashedKeys.reserve(keys.size());
            for (const std::string &key : keys) {
                hashedKeys.push_back(bytehamster::util::MurmurHash64(key));
            }
            startSearch(hashedKeys);
        }

        explicit ConsensusRecSplit(std::span<const uint64_t> keys)
                : numKeys(keys.size()),
                  unalignedBitVector(ROOT_SEED_BITS + (numKeys / n) * SplittingTreeStorage<n, overhead>::totalSize()) {
            startSearch(keys);
        }

        ~ConsensusRecSplit() {
            delete bucketingPhf;
        }

        [[nodiscard]] size_t getBits() const {
            return unalignedBitVector.bitSize() + bucketingPhf->getBits();
        }

        [[nodiscard]] size_t operator()(const std::string &key) const {
            return this->operator()(bytehamster::util::MurmurHash64(key));
        }

        [[nodiscard]] size_t operator()(uint64_t key) const {
            size_t nbuckets = numKeys / n;
            size_t bucket = bucketingPhf->operator()(key);
            if (bucket >= nbuckets) {
                return bucket; // Fallback if numKeys does not divide n
            }
            SplittingTaskIterator<n, overhead> task(0, 0, bucket, numKeys / n);
            for (size_t level = 0; level < logn; level++) {
                task.setLevel(level);
                if (toLeft(key, readSeed(task))) {
                    task.index = 2 * task.index;
                } else {
                    task.index = 2 * task.index + 1;
                }
            }
            return bucket * n + task.index;
        }

    private:
        void startSearch(std::span<const uint64_t> keys) {
            std::cout << "Tree space per bucket: " << SplittingTreeStorage<n, overhead>::totalSize() << std::endl;

            bucketingPhf = new BumpedKPerfectHashFunction<n>(keys);
            size_t nbuckets = keys.size() / n;
            std::vector<size_t> counters(nbuckets);
            std::vector<uint64_t> modifiableKeys(keys.size());
            for (uint64_t key : keys) {
                size_t bucket = bucketingPhf->operator()(key);
                if (bucket >= nbuckets) {
                    continue; // No need to handle this key
                }
                modifiableKeys.at(bucket * n + counters.at(bucket)) = key;
                counters.at(bucket)++;
            }
            #ifndef NDEBUG
                for (size_t counter : counters) {
                    assert(counter == n);
                }
            #endif

            for (size_t rootSeed = 0; rootSeed < (1ul << (ROOT_SEED_BITS - 1)); rootSeed++) {
                unalignedBitVector.writeTo(ROOT_SEED_BITS, rootSeed);
                if (construct(modifiableKeys)) {
                    return;
                }
            }
            throw std::logic_error("Unable to construct");
        }

        bool construct(std::span<uint64_t> keys) {
            SplittingTaskIterator<n, overhead> task(0, 0, 0, numKeys / n);
            uint64_t seed = readSeed(task);
            while (true) { // Basically "while (!task.isEnd())"
                size_t keysBegin = task.bucket * n + task.index * task.taskSizeThisLevel;
                std::span<uint64_t> keysThisTask = keys.subspan(keysBegin, task.taskSizeThisLevel);
                bool success = false;
                uint64_t maxSeed = seed | task.seedMask;
                for (; seed <= maxSeed; seed++) {
                    if (isSeedSuccessful(keysThisTask, seed)) {
                        success = true;
                        break;
                    }
                }
                if (success) {
                    if (task.taskSizeThisLevel > 2) { // No need to partition last layer
                        std::partition(keysThisTask.begin(), keysThisTask.end(),
                                       [&](uint64_t key) { return toLeft(key, seed); });
                    }
                    writeSeed(task, seed);
                    task.next();
                    if (task.isEnd()) {
                        return true;
                    }
                    seed = readSeed(task);
                } else {
                    seed--; // Was incremented beyond max seed, set back to max
                    do {
                        seed &= ~task.seedMask; // Reset seed to 0
                        writeSeed(task, seed);
                        if (task.isFirst()) {
                            return false; // Can't backtrack further, fail
                        }
                        task.previous();
                        seed = readSeed(task);
                    } while ((seed & task.seedMask) == task.seedMask); // Backtrack all tasks that are at their max seed
                    seed++; // Start backtracked task with its next seed candidate
                }
            }
            throw std::logic_error("Should never arrive here, function returns from within the loop");
        }

        bool isSeedSuccessful(std::span<uint64_t> keys, uint64_t seed) {
            size_t numToLeft = 0;
            for (uint64_t key : keys) {
                numToLeft += toLeft(key, seed);
            }
            return numToLeft == (keys.size() / 2);
        }

        [[nodiscard]] static bool toLeft(uint64_t key, uint64_t seed) {
            return bytehamster::util::remix(key + seed) % 2;
        }

        [[nodiscard]] uint64_t readSeed(SplittingTaskIterator<n, overhead> task) const {
            return unalignedBitVector.readAt(task.endPosition + ROOT_SEED_BITS);
        }

        void writeSeed(SplittingTaskIterator<n, overhead> task, uint64_t seed) {
            unalignedBitVector.writeTo(task.endPosition + ROOT_SEED_BITS, seed);
        }
};
} // namespace consensus
