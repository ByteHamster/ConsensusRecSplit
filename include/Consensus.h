#pragma once
#include <cstdint>
#include <vector>
#include <fstream>
#include <span>

#include <ips2ra.hpp>
#include <bytehamster/util/MurmurHash64.h>
#include <bytehamster/util/Function.h>

namespace consensus {
class Consensus {
    private:
        double overhead;
        size_t bucketSize;
        size_t numBuckets;
        std::vector<size_t> bucketSizePrefix;
        std::vector<size_t> bucketEncoding;
    public:
        explicit Consensus(std::span<const std::string> keys, double overhead = 0.01, size_t bucketSize_ = 0)
                : overhead(overhead),
                  bucketSize(bucketSize_ == 0 ? 100.0 / overhead : bucketSize_),
                  numBuckets(keys.size() / bucketSize) {
            std::vector<uint64_t> hashes;
            hashes.reserve(keys.size());
            for (const std::string &key : keys) {
                hashes.push_back(bytehamster::util::MurmurHash64(key));
            }
            construct(hashes);
        }

        explicit Consensus(std::span<const uint64_t> keys, double overhead = 0.01, size_t bucketSize_ = 0)
                : overhead(overhead),
                  bucketSize(bucketSize_ == 0 ? 100.0 / overhead : bucketSize_),
                  numBuckets(keys.size() / bucketSize) {
            std::vector<uint64_t> modifiableKeys(keys.begin(), keys.end());
            construct(modifiableKeys);
        }

        void construct(std::vector<uint64_t> &keys) {
            ips2ra::sort(keys.begin(), keys.end());
            bucketSizePrefix.resize(numBuckets + 1);
            for (uint64_t key : keys) {
                bucketSizePrefix.at(bucketOf(key))++;
            }
            size_t prefix = 0;
            for (size_t i = 0; i < bucketSizePrefix.size(); i++) {
                size_t prefixBefore = prefix;
                prefix += bucketSizePrefix.at(i);
                bucketSizePrefix.at(i) = prefixBefore;
            }
            bucketSizePrefix.at(numBuckets) = prefix + 1;

            
        }

        size_t bucketOf(uint64_t key) const {
            return bytehamster::util::fastrange64(key, numBuckets);
        }

        [[nodiscard]] size_t getBits() const {
            return 8 * (sizeof(*this));
        }

        [[nodiscard]] size_t operator()(const std::string &key) const {
            return this->operator()(bytehamster::util::MurmurHash64(key));
        }

        [[nodiscard]] size_t operator()(uint64_t key) const {
            size_t bucket = bucketOf(key);
            return bucket;
        }
};
} // namespace consensus
