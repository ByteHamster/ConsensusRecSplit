#pragma once

#include <vector>
#include <span>
#include <map>
#include <bytehamster/util/EliasFano.h>
#include <bytehamster/util/MurmurHash64.h>
#include <tlx/math/integer_log2.hpp>
#include <bytehamster/util/Function.h>
#include <bytehamster/util/IntVector.h>

namespace consensus {
/**
 * If the number of input keys is not a multiple of k,
 * this generates a minimal 1-perfect hash function on the remaining keys.
 * This is useful for Consensus, but might need an unexpectedly high amount of space for other uses.
 */
template <size_t k>
class BumpedKPerfectHashFunction {
        static constexpr double OVERLOAD_FACTOR = 0.97;
        static constexpr size_t THRESHOLD_TRIMMING = 10; // Thresholds smaller than 1-1/t are not represented
        static constexpr size_t THRESHOLD_BITS = tlx::integer_log2_floor(k) - 1;
        static constexpr size_t THRESHOLD_RANGE = 1ul << THRESHOLD_BITS;

        struct LayerInfo {
            uint32_t base;
            uint32_t expectedThreshold;
        };

        struct KeyInfo {
            uint64_t mhc;
            uint32_t bucket;
            uint32_t threshold;
        };

        size_t N;
        bytehamster::util::IntVector<THRESHOLD_BITS> thresholds;
        std::vector<LayerInfo> layerInfo;
        std::unordered_map<uint64_t, size_t> fallbackPhf;
        pasta::BitVector freePositionsBv;
        pasta::FlatRankSelect<pasta::OptimizedFor::ONE_QUERIES> *freePositionsRankSelect = nullptr;
    public:
        explicit BumpedKPerfectHashFunction(std::span<uint64_t> keys)
                : N(keys.size()), thresholds(N / k) {
            size_t nbuckets = N / k;
            size_t keysInEndBucket = N - nbuckets * k;
            size_t bucketsThisLayer = std::max(1ul, (size_t) std::ceil(OVERLOAD_FACTOR * nbuckets));
            std::vector<size_t> freePositions;
            std::vector<KeyInfo> hashes;
            hashes.reserve(keys.size());
            for (uint64_t key : keys) {
                uint64_t mhc = key;
                uint32_t bucket = ::bytehamster::util::fastrange32(mhc & 0xffffffff, bucketsThisLayer);
                uint32_t threshold = mhc >> 32;
                hashes.emplace_back(mhc, bucket, threshold);
            }
            std::vector<KeyInfo> allHashes = hashes;
            layerInfo.push_back(LayerInfo{ 0, 0 });
            for (size_t layer = 0; layer < 2; layer++) {
                const size_t layerBase = layerInfo.back().base;
                if (layer != 0) {
                    assert(layer == 1);
                    bucketsThisLayer = nbuckets - layerBase;
                    if (bucketsThisLayer == 0) {
                        // Just keep 1 layer in this data structure
                        break;
                    }
                    // Rehash
                    for (auto & hash : hashes) {
                        hash.mhc = ::bytehamster::util::remix(hash.mhc);
                        hash.bucket = ::bytehamster::util::fastrange32(hash.mhc & 0xffffffff, bucketsThisLayer);
                        hash.threshold = hash.mhc >> 32;
                    }
                }
                double scaling = std::min(1.0, (double(bucketsThisLayer * k) / hashes.size()) / OVERLOAD_FACTOR);
                layerInfo.at(layer).expectedThreshold = std::numeric_limits<uint32_t>::max() * scaling;
                layerInfo.push_back(LayerInfo{ 0, 0 });
                layerInfo.back().base = layerBase + bucketsThisLayer;
                ips2ra::sort(hashes.begin(), hashes.end(), [] (const KeyInfo &t) { return uint64_t(t.bucket) << 32 | t.threshold; });
                std::vector<KeyInfo> bumpedKeys;
                size_t bucketStart = 0;
                size_t previousBucket = 0;
                for (size_t i = 0; i < hashes.size(); i++) {
                    size_t bucket = hashes.at(i).bucket;
                    while (bucket != previousBucket) {
                        flushBucket(layer, bucketStart, i, previousBucket, hashes, bumpedKeys, freePositions);
                        previousBucket++;
                        bucketStart = i;
                    }
                }
                // Last bucket
                while (previousBucket < bucketsThisLayer) {
                    flushBucket(layer, bucketStart, hashes.size(), previousBucket, hashes, bumpedKeys, freePositions);
                    previousBucket++;
                    bucketStart = hashes.size();
                }
                hashes = bumpedKeys;
                //std::cout<<"Bumped in layer "<<layer<<": "<<hashes.size()<<std::endl;
            }

            for (size_t i = 0; i < hashes.size(); i++) {
                fallbackPhf.insert(std::make_pair(hashes.at(i).mhc, i));
            }
            size_t additionalFreePositions = hashes.size() - freePositions.size();
            size_t nbucketsHandled = layerInfo.back().base;
            {
                size_t i = 0;
                for (; i < additionalFreePositions - keysInEndBucket; i++) {
                    freePositions.push_back(nbucketsHandled + i/k);
                }
                for (; i < additionalFreePositions; i++) {
                    freePositions.push_back(nbuckets + i);
                }
            }
            if (!freePositions.empty()) {
                freePositionsBv.resize(freePositions.size() + freePositions.back() + 1, false);
                for (size_t i = 0; i < freePositions.size(); i++) {
                    freePositionsBv[i + freePositions.at(i)] = true;
                }
                freePositionsRankSelect = new pasta::FlatRankSelect<pasta::OptimizedFor::ONE_QUERIES>(freePositionsBv);
            }
        }

        uint32_t compact_threshold(uint32_t threshold, size_t layer) const {
            size_t expected = layerInfo.at(layer).expectedThreshold;
            size_t interpolationRange = expected / THRESHOLD_TRIMMING;
            size_t minThreshold = expected - interpolationRange;
            assert(minThreshold > 0);
            // Threshold 0 is reserved as a safeguard for bumping all
            if (threshold < minThreshold) {
                return 1;
            }
            return std::min(THRESHOLD_RANGE - 1, 1 + (THRESHOLD_RANGE - 1) * (threshold - minThreshold) / interpolationRange);
        }

        void flushBucket(size_t layer, size_t bucketStart, size_t i, size_t bucketIdx,
                         std::vector<KeyInfo> &hashes, std::vector<KeyInfo> &bumpedKeys,
                         std::vector<size_t> &freePositions) {
            size_t bucketSize = i - bucketStart;
            size_t layerBase = layerInfo.at(layer).base;
            if (bucketSize <= k) {
                size_t threshold = THRESHOLD_RANGE - 1;
                thresholds.set(layerBase + bucketIdx, threshold);
                for (size_t b = bucketSize; b < k; b++) {
                    freePositions.push_back(layerBase + bucketIdx);
                }
            } else {
                size_t lastThreshold = compact_threshold(hashes.at(bucketStart + k - 1).threshold, layer);
                size_t firstBumpedThreshold = compact_threshold(hashes.at(bucketStart + k).threshold, layer);
                size_t threshold = lastThreshold;
                if (firstBumpedThreshold == lastThreshold) {
                    // Needs to bump more
                    threshold--;
                }
                thresholds.set(layerBase + bucketIdx, threshold);
                for (size_t l = 0; l < bucketSize; l++) {
                    if (compact_threshold(hashes.at(bucketStart + l).threshold, layer) > threshold) {
                        bumpedKeys.push_back(hashes.at(bucketStart + l));
                        if (l < k) {
                            freePositions.push_back(layerBase + bucketIdx);
                        }
                    }
                }
            }
        }

        /** Estimate for the space usage of this structure, in bits */
        [[nodiscard]] size_t getBits() const {
            return 8 * sizeof(*this)
                   + fallbackPhf.size() * 4
                   + layerInfo.size() * sizeof(LayerInfo) * 8
                   + freePositionsBv.space_usage() - 8 * sizeof(freePositionsBv) // Already in sizeof(*this)
                   + ((freePositionsRankSelect == nullptr) ? 0 : 8 * freePositionsRankSelect->space_usage())
                   + 8 * thresholds.dataSizeBytes();
        }

        void printBits() const {
            std::cout << "Overall: " << 1.0f*getBits()/N << std::endl;
            std::cout << "This: " << 8.0f*sizeof(*this)/N << std::endl;
            std::cout << "Thresholds: " << 1.0f*THRESHOLD_BITS/k << std::endl;
            std::cout << "Fallback PHF keys: " << fallbackPhf.size() << std::endl;
            std::cout << "PHF: " << 1.0f*fallbackPhf.size() * 4 / N << std::endl;
            if (freePositionsBv.size() > 0) {
                std::cout << "Fano: " << 1.0f*(freePositionsBv.space_usage() + 8 * freePositionsRankSelect->space_usage()) / N << std::endl;
                std::cout << "Fano size: " << freePositionsBv.size() << std::endl;
            }
        }

        size_t operator() (const std::string &key) const {
            return operator()(::bytehamster::util::MurmurHash64(key));
        }

        inline size_t operator()(uint64_t mhc) const {
            for (size_t layer = 0; layer < layerInfo.size() - 1; layer++) {
                if (layer != 0) {
                    mhc = ::bytehamster::util::remix(mhc);
                }
                size_t base = layerInfo.at(layer).base;
                size_t layerSize = layerInfo.at(layer + 1).base - base;
                uint32_t bucket = ::bytehamster::util::fastrange32(mhc & 0xffffffff, layerSize);
                uint32_t threshold = mhc >> 32;
                uint64_t storedThreshold = thresholds.at(base + bucket);
                if (compact_threshold(threshold, layer) <= storedThreshold) {
                    return base + bucket;
                }
            }
            size_t phf = fallbackPhf.at(mhc);
            size_t bucket = freePositionsRankSelect->select1(phf + 1) - phf;
            size_t nbuckets = layerInfo.back().base;
            if (bucket >= nbuckets) { // Last half-filled bucket
                return bucket - nbuckets + k * nbuckets;
            }
            return bucket;
        }
};
} // namespace consensus
