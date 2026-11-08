#include "simcoaster/application_startup.hpp"
#include "application_movie_internal.hpp"
#include "application_startup_image.hpp"
#include "simcoaster/save_schema.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace simcoaster {
namespace {
std::string lower(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return value;
}

std::optional<std::filesystem::path> child(const std::filesystem::path& parent, std::string_view name, bool directory) {
    const auto expected = parent / name;
    const auto correct = [directory](const std::filesystem::path& path) {
        return directory ? std::filesystem::is_directory(path) : std::filesystem::is_regular_file(path);
    };
    if (correct(expected)) {
        return expected;
    }
    std::optional<std::filesystem::path> found;
    if (std::filesystem::is_directory(parent)) {
        for (const auto& entry : std::filesystem::directory_iterator(parent)) {
            if (lower(entry.path().filename().native_encoded_string()) == lower(std::string(name)) && correct(entry.path())) {
                if (found) {
                    throw std::runtime_error(std::format("Ambiguous original startup path {}", expected.native_encoded_string()));
                }
                found = entry.path();
            }
        }
    }
    return found;
}

std::filesystem::path required_child(const std::filesystem::path& parent, std::string_view name, bool directory) {
    const auto result = child(parent, name, directory);
    if (!result) {
        throw std::runtime_error(std::format("Missing original startup path {}", (parent / name).native_encoded_string()));
    }
    return *result;
}

void overlay(TextureImage& target, const TextureImage& source, unsigned x, unsigned y) {
    if (x >= target.width || y >= target.height) {
        return;
    }
    const auto width = std::min(source.width, target.width - x);
    const auto height = std::min(source.height, target.height - y);
    for (unsigned row = 0; row < height; ++row) {
        std::copy_n(source.pixels.begin() + std::size_t(row) * source.width, width, target.pixels.begin() + std::size_t(row + y) * target.width + x);
    }
}

void colored_quad(UiDrawList& draw, std::array<float, 4> bounds, RgbaPixel color) {
    if (draw.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4 || draw.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6) {
        throw std::runtime_error("Startup draw overflow");
    }
    const auto vertex = static_cast<std::uint32_t>(draw.vertices.size());
    const auto index = static_cast<std::uint32_t>(draw.indices.size());
    draw.vertices.insert(draw.vertices.end(), { { { bounds[0], bounds[1], 0 }, { 0, 0 }, color }, { { bounds[2], bounds[1], 0 }, { 1, 0 }, color }, { { bounds[2], bounds[3], 0 }, { 1, 1 }, color }, { { bounds[0], bounds[3], 0 }, { 0, 1 }, color } });
    draw.indices.insert(draw.indices.end(), { vertex, vertex + 1, vertex + 2, vertex, vertex + 2, vertex + 3 });
    UiDrawCommand command;
    command.first_index = index;
    command.index_count = 6;
    command.clip = { 0, 0, draw.canvas_width, draw.canvas_height };
    draw.commands.push_back(std::move(command));
}
} //namespace

struct ApplicationStartupPresentation::State {
    std::filesystem::path directory;
    std::string language;
    std::uint32_t width {}, height {}, duration = 500;
    ApplicationStartupStage stage = ApplicationStartupStage::clear;
    std::optional<std::pair<ApplicationStartupStage, std::uint32_t>> pending;
    TextureImagePtr image;
    std::chrono::milliseconds age {};
    std::uint64_t pumps {};

    State(const std::filesystem::path& root, std::string name, unsigned w, unsigned h) : language(name.empty() ? "english" : lower(std::move(name))), width(w), height(h) {
        if (!width || !height || width > 16384 || height > 16384) {
            throw std::invalid_argument("Invalid startup viewport");
        }
        unsigned resolution = 640;
        if (w == 400 || w == 512 || w == 640 || w == 800 || w == 1024 || w == 1280) {
            resolution = w;
        }
        directory = required_child(required_child(required_child(root, "data", true), "init", true), std::format("{}", resolution), true);
    }

    void load(ApplicationStartupStage next, std::uint32_t ticks) {
        TextureImagePtr new_image;
        if (next != ApplicationStartupStage::clear) {
            const auto filename = next == ApplicationStartupStage::splash ? "splash_" + language + ".png" : "welcome.png";
            auto composed = load_startup_png(required_child(directory, filename, false));
            if (next == ApplicationStartupStage::legal || next == ApplicationStartupStage::loading) {
                const auto add = [&](std::string_view file, unsigned x, unsigned y) {
                    //6F8260 simply leaves an absent overlay unchanged. American has no
                    //bull_american.png in the shipped set; do not invent an alias.
                    if (const auto path = child(directory, file, false)) {
                        overlay(composed, load_startup_png(*path), x, y);
                    }
                };
                if (next == ApplicationStartupStage::legal) {
                    add("legal_" + language + ".png", width / 2, static_cast<unsigned>(double(height) * .0020833334 * 384.0));
                }
                add("welcome_" + language + ".png", 0, 0);
                add("bull_" + language + ".png", 0, static_cast<unsigned>(double(height) * .0020833334 * 336.0));
            }
            new_image = std::make_shared<TextureImage>(std::move(composed));
        }
        stage = next;
        duration = ticks ? ticks : 500;
        image = std::move(new_image);
        age = {};
        pumps = 0;
        pending.reset();
    }
};

ApplicationStartupPresentation::ApplicationStartupPresentation(const std::filesystem::path& root, std::string language, std::uint32_t width, std::uint32_t height) : state_(std::make_unique<State>(root, std::move(language), width, height)) {}

ApplicationStartupPresentation::~ApplicationStartupPresentation() = default;

void ApplicationStartupPresentation::request(ApplicationStartupStage stage, std::uint32_t duration) {
    if (state_->stage == ApplicationStartupStage::legal && state_->age < std::chrono::seconds(3)) {
        state_->pending = std::pair { stage, duration };
    } else {
        state_->load(stage, duration);
    }
}

void ApplicationStartupPresentation::advance(std::chrono::milliseconds delta) {
    if (delta.count() < 0) {
        throw std::invalid_argument("Negative startup duration");
    }
    state_->age += delta;
    if (state_->stage == ApplicationStartupStage::splash && state_->age >= std::chrono::seconds(3)) {
        state_->load(ApplicationStartupStage::legal, state_->duration);
    } else if (state_->pending && state_->age >= std::chrono::seconds(3)) {
        const auto [stage, duration] = *state_->pending;
        state_->load(stage, duration);
    }
    if (state_->pumps < std::numeric_limits<std::uint64_t>::max()) {
        ++state_->pumps;
    }
}

void ApplicationStartupPresentation::append(UiDrawList& draw) const {
    if (state_->image) {
        application_movie_detail::append_full_viewport(draw, state_->image);
    } else {
        colored_quad(draw, { 0, 0, draw.canvas_width, draw.canvas_height }, { 0, 0, 0, 255 });
    }
    if (state_->stage != ApplicationStartupStage::loading) {
        return;
    }
    const float x = draw.canvas_width / 640.0f, y = draw.canvas_height / 480.0f;
    const auto quad = [&](float l, float t, float r, float b, RgbaPixel color) {
        colored_quad(draw, { l * x, t * y, r * x, b * y }, color);
    };
    //6F9380: frame370,420..610,430; outline and offset shadow are
    //geometric renderer primitives, not text or fabricated bitmap artwork.
    quad(366, 416, 614, 434, { 64, 48, 32, 64 });
    quad(365, 414, 615, 416, { 32, 32, 32, 208 });
    quad(365, 434, 615, 436, { 32, 32, 32, 208 });
    quad(364, 415, 366, 435, { 32, 32, 32, 208 });
    quad(614, 415, 616, 435, { 32, 32, 32, 208 });
    const auto count = static_cast<unsigned>(std::ceil(std::min(240.0, double(state_->pumps) * 480.0 / state_->duration)));
    float red = 255.0f, green = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        //6F969A compares the live x87 loop counter with the half-width. The
        //decompiler loses that register and shows an undefined condition; actual
        //disassembly increases green in the first half, then decreases red.
        if (i < 120) {
            green = std::min(255.0f, green + 255.0f / 120.0f);
        } else {
            red = std::max(0.0f, red - 255.0f / 120.0f);
        }
        const auto left = 370.0f + static_cast<float>(i);
        quad(left + 2, 422, left + 3, 432, { 32, 32, 32, 255 });
        quad(left, 420, left + 1, 430, { static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green), 0, 255 });
    }
}

ApplicationStartupStage ApplicationStartupPresentation::stage() const {
    return state_->stage;
}

bool ApplicationStartupPresentation::transition_pending() const {
    return state_->pending.has_value();
}

TextureImagePtr ApplicationStartupPresentation::image() const {
    return state_->image;
}

std::uint64_t ApplicationStartupPresentation::pump_count() const {
    return state_->pumps;
}

std::array<std::optional<std::filesystem::path>, 2> application_startup_movie_paths(const std::filesystem::path& root, std::string_view language, unsigned day) {
    if (!day || day > 31) {
        throw std::invalid_argument("Invalid movie calendar day");
    }
    const auto data = child(root, "data", true);
    if (!data) {
        return {};
    }
    const auto movies = child(*data, "movies", true);
    if (!movies) {
        return {};
    }
    constexpr std::array<std::string_view, 8> names { "bub.tgq", "buc.tgq", "grav.tgq", "jug.tgq", "mir.tgq", "plan.tgq", "roc.tgq", "roll.tgq" };
    return { child(*movies, lower(std::string(language)) == "american" ? "bfsim.tgq" : "bf.tgq", false), child(*movies, names[day % names.size()], false) };
}

bool application_startup_movies_enabled(const std::filesystem::path& root) {
    //497290 defaults flags to790037.45B640/45AE20 accept version>=4
    //followed by six complete DWORDs; a missing/failed read retains defaults.
    struct Dialog {
        [[=save::Field { "version", 0 }]] std::uint32_t version {};
        [[=save::Field { "graphics_flags", 1 }]] std::uint32_t graphics_flags {};
        [[=save::Field { "remaining_fields", 2 }]] std::array<std::uint32_t, 5> remaining {};
    };

    std::uint32_t flags = 790037;
    if (const auto path = child(root, "dialog.tcf", false)) {
        std::ifstream input { *path, std::ios::binary };
        std::array<std::byte, save::record_width<Dialog>> bytes {};
        if (input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            Dialog dialog;
            save::detail::decode(std::span<const std::byte, bytes.size()> { bytes }, dialog);
            if (dialog.version >= 4) {
                flags = dialog.graphics_flags;
            }
        }
    }
    return (flags & 0x200u) != 0;
}
} //namespace simcoaster
