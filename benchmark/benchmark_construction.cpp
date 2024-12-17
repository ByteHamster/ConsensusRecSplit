#include <chrono>
#include <iostream>
#include <csignal>

#include <bytehamster/util/XorShift64.h>
#include <tlx/cmdline_parser.hpp>

#include "BenchmarkData.h"
#include "ConsensusRecSplit.h"

#define DO_NOT_OPTIMIZE(value) asm volatile("" : : "r,m"(value) : "memory")

size_t numObjects = 1e6;
size_t numQueries = 1e6;
double spaceOverhead = 0.01;

template <size_t n, double overhead>
void construct() {
    auto time = std::chrono::system_clock::now();
    long seed = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    bytehamster::util::XorShift64 prng(seed);
    //#define STRING_KEYS
    #ifdef STRING_KEYS
        std::vector<std::string> keys = generateInputData(numObjects);
    #else
        std::cout<<"Generating input data (Seed: "<<seed<<")"<<std::endl;
        std::vector<uint64_t> keys;
        for (size_t i = 0; i < numObjects; i++) {
            keys.push_back(prng());
        }
    #endif

    std::cout<<"Constructing"<<std::endl;
    sleep(1);
    auto beginConstruction = std::chrono::high_resolution_clock::now();
    consensus::ConsensusRecSplit<n, overhead> hashFunc(keys);
    unsigned long constructionDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - beginConstruction).count();

    std::cout<<"Testing"<<std::endl;
    std::vector<bool> taken(keys.size(), false);
    for (size_t i = 0; i < keys.size(); i++) {
        size_t hash = hashFunc(keys.at(i));
        if (taken[hash]) {
            std::cerr << "Collision by key " << i << "!" << std::endl;
            exit(1);
        } else if (hash > numObjects) {
            std::cerr << "Out of range!" << std::endl;
            exit(1);
        }
        taken[hash] = true;
    }

    std::cout<<"Preparing query plan"<<std::endl;
    #ifdef STRING_KEYS
        std::vector<std::string> queryPlan;
    #else
        std::vector<uint64_t> queryPlan;
    #endif
    queryPlan.reserve(numQueries);
    for (size_t i = 0; i < numQueries; i++) {
        queryPlan.push_back(keys[prng(numObjects)]);
    }

    std::cout<<"Querying"<<std::endl;
    sleep(1);
    auto beginQueries = std::chrono::high_resolution_clock::now();
    for (const auto &key : queryPlan) {
        size_t retrieved = hashFunc(key);
        DO_NOT_OPTIMIZE(retrieved);
    }
    auto queryDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - beginQueries).count();

    std::cout << "RESULT"
              << " method=Consensus"
              << " overhead=" << overhead
              << " N=" << numObjects
              << " numQueries=" << numQueries
              << " queryTimeMilliseconds=" << queryDurationMs
              << " constructionTimeMilliseconds=" << constructionDurationMs
              << " bitsPerElement=" << (double) hashFunc.getBits() / numObjects
              << std::endl;
}

template <size_t k, double overhead>
void dispatchBucketSize(size_t param) {
    if constexpr (k <= 4) {
        std::cerr << "The parameter " << param << " for k was not compiled into this binary." << std::endl;
    } else if (k == param) {
        construct<k, overhead>();
    } else {
        dispatchBucketSize<k / 2, overhead>(param);
    }
}

int main(int argc, const char* const* argv) {
    size_t bucketSize = 8192;

    tlx::CmdlineParser cmd;
    cmd.add_bytes('n', "numObjects", numObjects, "Number of objects to construct with");
    cmd.add_bytes('k', "bucketSize", bucketSize, "Bucket size of the initial partitioning");
    cmd.add_bytes('q', "numQueries", numQueries, "Number of queries to measure");
    cmd.add_double('e', "overhead", spaceOverhead, "Overhead parameter");

    if (!cmd.process(argc, argv)) {
        return 1;
    }

    if (spaceOverhead == 0.5) {
        dispatchBucketSize<1ul << 15, 0.5>(bucketSize);
    } else if (spaceOverhead == 0.3) {
        dispatchBucketSize<1ul << 15, 0.3>(bucketSize);
    } else if (spaceOverhead == 0.1) {
        dispatchBucketSize<1ul << 15, 0.1>(bucketSize);
    } else if (spaceOverhead == 0.03) {
        dispatchBucketSize<1ul << 15, 0.03>(bucketSize);
    } else if (spaceOverhead == 0.01) {
        dispatchBucketSize<1ul << 15, 0.01>(bucketSize);
    } else if (spaceOverhead == 0.001) {
        dispatchBucketSize<1ul << 15, 0.001>(bucketSize);
    } else {
        std::cerr << "The parameter " << spaceOverhead << " for spaceOverhead was not compiled into this binary." << std::endl;
    }
    return 0;
}
