#pragma once

#include <vector>
#include <cstdint>

class UnalignedBitVector {
        std::vector<uint64_t> bits;
    public:
        explicit UnalignedBitVector(size_t size) : bits(size / 64) {
        }

        /**
         * Read a full 64-bit word at the unaligned bit position.
         * The bit position refers to the right-most bit to read.
         */
        uint64_t readAt(size_t bitPosition) const {
            return 0;
        }

        /**
         * Write a full 64-bit word at the unaligned bit position
         * The bit position refers to the right-most bit to write.
         */
        void writeTo(size_t bitPosition, uint64_t value) {

        }

        size_t bitSize() const {
            return 0;
        }
};