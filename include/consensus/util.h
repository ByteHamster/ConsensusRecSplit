#pragma once

#include <cstdint>
#include <cstddef>
#include <bit>
#include <numeric>

namespace consensus {

static constexpr size_t intLog2(size_t x) {
    return std::bit_width(x) - 1;
}

double constexpr sqrtNewtonRaphson(double x, double curr, double prev) {
    return curr == prev ? curr : sqrtNewtonRaphson(x, 0.5 * (curr + x / curr), curr);
}

double constexpr constexpr_sqrt(double x) {
    // Source: https://stackoverflow.com/a/34134071
    return x >= 0 && x < std::numeric_limits<double>::infinity()
            ? sqrtNewtonRaphson(x, x, 0) : std::numeric_limits<double>::quiet_NaN();
}

}