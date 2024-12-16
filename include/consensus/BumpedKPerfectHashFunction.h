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

template <size_t THRESHOLD_RANGE, size_t k>
constexpr std::array<uint32_t, THRESHOLD_RANGE> _fill_mapping() {
    std::array<uint32_t, THRESHOLD_RANGE> array;
    if (THRESHOLD_RANGE == 1) {
        return array;
    } else if (THRESHOLD_RANGE == 2) {
        array.at(0) = 0;
        array.at(1) = std::numeric_limits<uint32_t>::max();
        return array;
    }
    array.at(0) = 0; // Last resort
    array.at(1) = std::numeric_limits<uint32_t>::max() / 3; // Safeguard, so much bumping should never happen in practice
    size_t interpolationRangeSteps = THRESHOLD_RANGE - 3;
    size_t interpolationRange = std::numeric_limits<uint32_t>::max() / 10;
    size_t interpolationRangeStart = std::numeric_limits<uint32_t>::max() - interpolationRange;
    size_t interpolationRangeStep = interpolationRange / interpolationRangeSteps;
    for (size_t i = 0; i < interpolationRangeSteps; i++) {
        array.at(2 + i) = interpolationRangeStart + i * interpolationRangeStep;
    }
    array.at(THRESHOLD_RANGE - 1) = std::numeric_limits<uint32_t>::max(); // Keep all
    return array;
}

template <size_t k>
class BumpedKPerfectHashFunction {
        static constexpr double OVERLOAD_FACTOR = 0.97;
        static constexpr size_t THRESHOLD_BITS = tlx::integer_log2_floor(k) - 1;
        static constexpr size_t THRESHOLD_RANGE = 1ul << THRESHOLD_BITS;
        static constexpr std::array<uint32_t, THRESHOLD_RANGE> THRESHOLD_MAPPING = _fill_mapping<THRESHOLD_RANGE, k>();

        size_t N;
        size_t nbuckets;
        bytehamster::util::IntVector<THRESHOLD_BITS> thresholds;
        std::vector<size_t> layerBases;
        std::unordered_map<uint64_t, size_t> fallbackPhf;
        pasta::BitVector freePositionsBv;
        pasta::FlatRankSelect<pasta::OptimizedFor::ONE_QUERIES> *freePositionsRankSelect = nullptr;
        size_t layers = 2;

        struct KeyInfo {
            uint64_t mhc;
            uint32_t bucket;
            uint32_t threshold;
        };
    public:
        explicit BumpedKPerfectHashFunction(std::span<uint64_t> keys)
                : N(keys.size()), nbuckets(N / k), thresholds(nbuckets) {

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
            layerBases.push_back(0);
            for (size_t layer = 0; layer < 2; layer++) {
                size_t layerBase = layerBases.back();
                if (layer != 0) {
                    assert(layer == 1);
                    bucketsThisLayer = nbuckets - layerBases.back();
                    if (bucketsThisLayer == 0) {
                        layers = 1;
                        break;
                    }
                    // Rehash
                    for (auto & hash : hashes) {
                        hash.mhc = ::bytehamster::util::remix(hash.mhc);
                        hash.bucket = ::bytehamster::util::fastrange32(hash.mhc & 0xffffffff, bucketsThisLayer);
                        hash.threshold = hash.mhc >> 32;
                    }
                }
                layerBases.push_back(layerBase + bucketsThisLayer);
                ips2ra::sort(hashes.begin(), hashes.end(), [] (const KeyInfo &t) { return uint64_t(t.bucket) << 32 | t.threshold; });
                std::vector<KeyInfo> bumpedKeys;
                size_t bucketStart = 0;
                size_t previousBucket = 0;
                for (size_t i = 0; i < hashes.size(); i++) {
                    size_t bucket = hashes.at(i).bucket;
                    while (bucket != previousBucket) {
                        flushBucket(layerBase, bucketStart, i, previousBucket, hashes, bumpedKeys, freePositions);
                        previousBucket++;
                        bucketStart = i;
                    }
                }
                // Last bucket
                while (previousBucket < bucketsThisLayer) {
                    flushBucket(layerBase, bucketStart, hashes.size(), previousBucket, hashes, bumpedKeys, freePositions);
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
            size_t nbucketsHandled = layerBases.back();
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

        uint32_t compact_threshold(uint32_t threshold) {
            // Binary search would be better here, but this doesn't matter much for performance anyway
            for (size_t i = 0; i < THRESHOLD_RANGE; i++) {
                if (threshold <= THRESHOLD_MAPPING[i]) {
                    return i;
                }
            }
            return THRESHOLD_MAPPING.back();
        }

        void flushBucket(size_t layerBase, size_t bucketStart, size_t i, size_t bucketIdx,
                         std::vector<KeyInfo> &hashes, std::vector<KeyInfo> &bumpedKeys,
                         std::vector<size_t> &freePositions) {
            size_t bucketSize = i - bucketStart;
            if (bucketSize <= k) {
                size_t threshold = THRESHOLD_RANGE - 1;
                thresholds.set(layerBase + bucketIdx, threshold);
                for (size_t b = bucketSize; b < k; b++) {
                    freePositions.push_back(layerBase + bucketIdx);
                }
            } else {
                size_t lastThreshold = compact_threshold(hashes.at(bucketStart + k - 1).threshold);
                size_t firstBumpedThreshold = compact_threshold(hashes.at(bucketStart + k).threshold);
                size_t threshold = lastThreshold;
                if (firstBumpedThreshold == lastThreshold) {
                    // Needs to bump more
                    threshold--;
                }
                thresholds.set(layerBase + bucketIdx, threshold);
                uint32_t uncompressedThreshold = THRESHOLD_MAPPING[threshold];
                for (size_t l = 0; l < bucketSize; l++) {
                    if (hashes.at(bucketStart + l).threshold > uncompressedThreshold) {
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
                   + freePositionsBv.size()
                   + ((freePositionsRankSelect == nullptr) ? 0 : 8 * freePositionsRankSelect->space_usage())
                   + 8 * thresholds.dataSizeBytes();
        }

        void printBits() const {
            std::cout << "Thresholds: " << 1.0f*THRESHOLD_BITS/k << std::endl;
            std::cout << "Fallback PHF keys: " << fallbackPhf.size() << std::endl;
            std::cout << "PHF: " << 1.0f*fallbackPhf.size() * 4 / N << std::endl;
            if (freePositionsBv.size() > 0) {
                std::cout << "Fano: " << 1.0f*(freePositionsBv.size() + 8 * freePositionsRankSelect->space_usage()) / N << std::endl;
            }
        }

        size_t operator() (const std::string &key) const {
            return operator()(::bytehamster::util::MurmurHash64(key));
        }

        inline size_t operator()(uint64_t mhc) const {
            for (size_t layer = 0; layer < layers; layer++) {
                if (layer != 0) {
                    mhc = ::bytehamster::util::remix(mhc);
                }
                size_t base = layerBases.at(layer);
                size_t layerSize = layerBases.at(layer + 1) - base;
                uint32_t bucket = ::bytehamster::util::fastrange32(mhc & 0xffffffff, layerSize);
                uint32_t threshold = mhc >> 32;
                uint64_t storedThreshold = thresholds.at(base + bucket);
                if (threshold <= THRESHOLD_MAPPING[storedThreshold]) {
                    return base + bucket;
                }
            }
            size_t phf = fallbackPhf.at(mhc);
            size_t bucket = freePositionsRankSelect->select1(phf + 1) - phf;
            if (bucket >= nbuckets) { // Last half-filled bucket
                return bucket - nbuckets + k * nbuckets;
            }
            return bucket;
        }
};
} // namespace consensus
