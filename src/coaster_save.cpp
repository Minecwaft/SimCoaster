#include "simcoaster/save_stream.hpp"
#include "simcoaster/coaster_save.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>

namespace simcoaster {
namespace {
using save::read;
using save::write;

template <class T>
bool read_values(Stream& stream, std::vector<T>& values, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        T value {};
        if (!read(stream, value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

template <class T>
bool write_values(Stream& stream, const std::vector<T>& values) {
    for (const auto& value : values) {
        if (!write(stream, value)) {
            return false;
        }
    }
    return true;
}
} //namespace

bool read_coaster_save_section(Stream& stream, CoasterSaveSection& result, const CoasterLimitsResolver& resolve) {
    CoasterSaveSection section;
    std::uint32_t count {};
    if (!read(stream, count) || !read(stream, section.registered_record_count) || !read(stream, section.available_id_holes) || !read(stream, section.next_map_id)) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        CoasterRecordSave record;
        std::uint32_t root_count {};
        if (!read(stream, record.header)) {
            return false;
        }
        const auto limits = resolve(record.header.mesh_instance_id);
        if (!limits) {
            return false;
        }
        if (!read(stream, root_count) || !read_values(stream, record.root_peers, root_count)) {
            return false;
        }
        for (std::uint32_t j = 0; j < record.header.geometry_count; ++j) {
            CoasterGeometrySave geometry;
            if (!read(stream, geometry.header) || !read_values(stream, geometry.peers, geometry.header.peer_count)) {
                return false;
            }
            record.geometry.push_back(std::move(geometry));
        }
        for (std::uint32_t j = 0; j < limits->trains; ++j) {
            CoasterTrainSave train;
            if (!read(stream, train.header)) {
                return false;
            }
            for (std::uint32_t k = 0; k < limits->cars_per_train; ++k) {
                CoasterCarSave car;
                if (!read(stream, car.header) || !read_values(stream, car.persons_a, car.header.person_count_a) || !read_values(stream, car.persons_b, car.header.person_count_b)) {
                    return false;
                }
                train.cars.push_back(std::move(car));
            }
            record.trains.push_back(std::move(train));
        }
        section.records.push_back(std::move(record));
    }
    result = std::move(section);
    return true;
}

bool write_coaster_save_section(Stream& stream, const CoasterSaveSection& section, const CoasterLimitsResolver& resolve) {
    if (section.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    for (const auto& record : section.records) {
        const auto limits = resolve(record.header.mesh_instance_id);
        if (!limits || record.trains.size() != limits->trains || record.geometry.size() != record.header.geometry_count || record.root_peers.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        for (const auto& train : record.trains) {
            if (train.cars.size() != limits->cars_per_train) {
                return false;
            }
        }
        for (const auto& geometry : record.geometry) {
            if (geometry.peers.size() != geometry.header.peer_count) {
                return false;
            }
        }
        for (const auto& train : record.trains) {
            for (const auto& car : train.cars) {
                if (car.persons_a.size() != car.header.person_count_a || car.persons_b.size() != car.header.person_count_b) {
                    return false;
                }
            }
        }
    }
    if (!write(stream, static_cast<std::uint32_t>(section.records.size())) || !write(stream, section.registered_record_count) || !write(stream, section.available_id_holes) || !write(stream, section.next_map_id)) {
        return false;
    }
    for (const auto& record : section.records) {
        if (!write(stream, record.header) || !write(stream, static_cast<std::uint32_t>(record.root_peers.size())) || !write_values(stream, record.root_peers)) {
            return false;
        }
        for (const auto& geometry : record.geometry) {
            if (!write(stream, geometry.header) || !write_values(stream, geometry.peers)) {
                return false;
            }
        }
        for (const auto& train : record.trains) {
            if (!write(stream, train.header)) {
                return false;
            }
            for (const auto& car : train.cars) {
                if (!write(stream, car.header) || !write_values(stream, car.persons_a) || !write_values(stream, car.persons_b)) {
                    return false;
                }
            }
        }
    }
    return true;
}
} //namespace simcoaster
