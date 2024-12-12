#pragma once

#include <vector>
#include <cstdint>
#include <iomanip>

namespace consensus {
class UnalignedBitVector {
        std::vector<uint64_t> bits;
    public:
        explicit UnalignedBitVector(size_t size) : bits((size + 63) / 64) {
        }

        /**
         * Read a full 64-bit word at the unaligned bit position.
         * The bit position refers to the right-most bit to read.
         */
        [[nodiscard]] uint64_t readAt(size_t bitPosition) const {
            assert(bitPosition >= 64);
            if (bitPosition % 64 == 0) {
                return bits[(bitPosition / 64) - 1];
            } else {
                return (bits[(bitPosition / 64) - 1] << (bitPosition % 64))
                       | (bits[(bitPosition / 64)] >> (64 - (bitPosition % 64)));
            }
        }

        /**
         * Write a full 64-bit word at the unaligned bit position
         * The bit position refers to the right-most bit to write.
         */
        void writeTo(size_t bitPosition, uint64_t value) {
            assert(bitPosition >= 64);
            if (bitPosition % 64 == 0) {
                bits[(bitPosition / 64) - 1] = value;
            } else {
                bits[(bitPosition / 64) - 1] &= ~(~0ul >> (bitPosition % 64));
                bits[(bitPosition / 64) - 1] |= value >> (bitPosition % 64);
                bits[(bitPosition / 64)] &= ~(~0ul << (64 - (bitPosition % 64)));
                bits[(bitPosition / 64)] |= value << (64 - (bitPosition % 64));
            }
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