#include <chrono>
#include <iostream>
#include <csignal>
#include <bytehamster/util/XorShift64.h>
#include "consensus/BumpedKPerfectHashFunction.h"

int main() {
    constexpr size_t k = 32768;

    auto time = std::chrono::system_clock::now();
    long seed = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    bytehamster::util::XorShift64 prng(seed);
    std::cout<<"Generating input data (Seed: "<<seed<<")"<<std::endl;
    std::vector<uint64_t> keys;
    for (size_t i = 0; i < 10'000'000; i++) {
        keys.push_back(prng());
    }
    consensus::BumpedKPerfectHashFunction<k> hashFunc(keys);

    std::cout<<"Testing"<<std::endl;
    std::vector<size_t> taken(keys.size() / k, 0);
    size_t perfectFallback = 0;
    for (size_t i = 0; i < keys.size(); i++) {
        size_t hash = hashFunc(keys.at(i));
        if (hash >= keys.size() / k) {
            perfectFallback++;
        } else if (taken[hash] >= k) {
            std::cerr << "Collision by key " << i << "!" << std::endl;
            exit(1);
        } else {
            taken[hash]++;
        }
    }
    if (perfectFallback >= k) {
        std::cerr << "Too many fallback" << std::endl;
        exit(1);
    }
    hashFunc.printBits();
    return 0;
}
