#include <chrono>
#include <iostream>
#include <csignal>

#include <bytehamster/util/XorShift64.h>
#include <tlx/cmdline_parser.hpp>

#include "BenchmarkData.h"
#include "Consensus.h"

#define DO_NOT_OPTIMIZE(value) asm volatile("" : : "r,m"(value) : "memory")

size_t numObjects = 1e6;
size_t numQueries = 1e6;
double overhead = 0.1;

template <size_t lineSize, typename offsetType>
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
    consensus::Consensus hashFunc(keys, overhead);
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

template <typename offsetType>
void dispatchLineSize(size_t lineSize) {
    if (lineSize == 1024) {
        construct<1024, offsetType>();
    } else if (lineSize == 512) {
        construct<512, offsetType>();
    } else if (lineSize == 256) {
        construct<256, offsetType>();
    } else if (lineSize == 128) {
        construct<128, offsetType>();
    } else if (lineSize == 64) {
        construct<64, offsetType>();

    } else {
        std::cerr<<"Invalid line size"<<std::endl;
    }
}

int main(int argc, const char* const* argv) {
    size_t lineSize = 256;
    size_t offsetSize = 16;

    tlx::CmdlineParser cmd;
    cmd.add_bytes('n', "numObjects", numObjects, "Number of objects to construct with");
    cmd.add_bytes('q', "numQueries", numQueries, "Number of queries to measure");
    cmd.add_bytes('l', "lineSize", lineSize, "Size of a cache line");
    cmd.add_bytes('o', "offsetSize", offsetSize, "Number of bits for offset");
    cmd.add_double('e', "overhead", overhead, "Overhead parameter");

    if (!cmd.process(argc, argv)) {
        return 1;
    }

    if (offsetSize == 16) {
        dispatchLineSize<uint16_t>(lineSize);
    } else if (offsetSize == 32) {
        dispatchLineSize<uint32_t>(lineSize);
    } else {
        std::cerr<<"Invalid offset size"<<std::endl;
    }

    return 0;
}
