#pragma once
#include "mesh_error.hpp"
#include "mesh_types.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster::mesh_transform {
inline std::array<float, 3> point(const MeshMatrix& matrix, const std::array<float, 3>& source) {
    std::array<float, 3> result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = static_cast<float>(static_cast<double>(source[0]) * matrix[axis] + static_cast<double>(source[2]) * matrix[axis + 8] + static_cast<double>(source[1]) * matrix[axis + 4] + matrix[axis + 12]);
    }
    return result;
}

//Full pivoted inverse: object hierarchies may contain scale/shear and cannot
//be inverted by transposing their rotation. Singular input is explicit.
inline MeshMatrix inverse(const MeshMatrix& source) {
    std::array<std::array<double, 8>, 4> work {};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            work[row][column] = source[row * 4 + column];
            work[row][column + 4] = row == column ? 1 : 0;
        }
    }
    for (std::size_t column = 0; column < 4; ++column) {
        auto pivot = column;
        for (auto row = column + 1; row < 4; ++row) {
            if (std::abs(work[row][column]) > std::abs(work[pivot][column])) {
                pivot = row;
            }
        }
        if (!std::isfinite(work[pivot][column]) || work[pivot][column] == 0) {
            throw MeshError { MeshFailure::configuration, column, {}, {} };
        }
        std::swap(work[pivot], work[column]);
        const auto divisor = work[column][column];
        for (auto& value : work[column]) {
            value /= divisor;
        }
        for (std::size_t row = 0; row < 4; ++row) {
            if (row != column) {
                const auto factor = work[row][column];
                for (std::size_t index = 0; index < 8; ++index) {
                    work[row][index] -= factor * work[column][index];
                }
            }
        }
    }
    MeshMatrix result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            auto& value = result[row * 4 + column];
            value = static_cast<float>(work[row][column + 4]);
            if (!std::isfinite(value)) {
                throw MeshError { MeshFailure::configuration, row * 4 + column, {}, {} };
            }
        }
    }
    return result;
}
} //namespace simcoaster::mesh_transform
