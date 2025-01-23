#pragma once

#include <vector>
#include <cstdint>
#include <iomanip>

namespace consensus {
/**
 * A bit vector where we can read/write any 64-bit slice without it having to be byte-aligned.
 */
class UnalignedBitVector {
        std::vector<uint64_t> bits;
    public:
        explicit UnalignedBitVector() : bits(0) {
        }

        explicit UnalignedBitVector(size_t size) : bits((size + 64 + 63) / 64) {
        }

        void clearAndResize(size_t size) {
            bits.clear();
            bits.resize((size + 64 + 63) / 64);
        }

        /**
         * Read a full 64-bit word at the unaligned bit position.
         * The bit position refers to the right-most bit to read.
         * The smallest position that can be read is 64.
         */
        [[nodiscard]] inline uint64_t readAt(size_t bitPosition) const {
            assert(bitPosition >= 64);
            assert(bitPosition / 64 <= bits.size());
            if (bitPosition % 64 == 0) {
                return bits[(bitPosition / 64)];
            } else {
                return (bits[(bitPosition / 64)] << (bitPosition % 64))
                       | (bits[(bitPosition / 64) + 1] >> (64 - (bitPosition % 64)));
            }
        }

        /**
         * Write a full 64-bit word at the unaligned bit position
         * The bit position refers to the right-most bit to write.
         */
        void inline writeTo(size_t bitPosition, uint64_t value) {
            assert(bitPosition >= 64);
            assert(bitPosition / 64 <= bits.size());
            if (bitPosition % 64 == 0) {
                bits[(bitPosition / 64)] = value;
            } else {
                bits[(bitPosition / 64)] &= ~(~0ul >> (bitPosition % 64));
                bits[(bitPosition / 64)] |= value >> (bitPosition % 64);
                bits[(bitPosition / 64) + 1] &= ~(~0ul << (64 - (bitPosition % 64)));
                bits[(bitPosition / 64) + 1] |= value << (64 - (bitPosition % 64));
            }
        }

        [[nodiscard]] inline uint64_t readRootSeed() const {
            return bits[0];
        }

        void inline writeRootSeed(uint64_t value) {
            bits[0] = value;
        }
        [[nodiscard]] size_t bitSize() const {
            return bits.size() * 64;
        }

        void print() const {
            for (uint64_t val : bits) {
                std::cout << std::setfill('0') << std::setw(16) << std::right << std::hex << val << " ";
            }
            std::cout << std::dec << std::endl;
        }
};
} // namespace consensus