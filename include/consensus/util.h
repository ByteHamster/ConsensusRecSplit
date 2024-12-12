#pragma once

#include <cstdint>
#include <cstddef>
#include <bit>
#include <numeric>

namespace consensus {

static constexpr size_t intLog2(size_t x) {
    return std::bit_width(x) - 1;
}

}