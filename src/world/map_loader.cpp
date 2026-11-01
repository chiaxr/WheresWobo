#include "world/map_loader.h"

#include <algorithm>
#include <map>

#include "core/config.h"
#include "core/data.h"

namespace wobo {

namespace {

Pattern parse_pattern(const std::string& s) {
    if (s == "stripes") return Pattern::Stripes;
    if (s == "dots") return Pattern::Dots;
    if (s == "checker") return Pattern::Checker;
    if (s == "chevron") return Pattern::Chevron;
    if (s == "grid") return Pattern::Grid;
    return Pattern::None;
}

bool parse_rgb(const std::string& s, Rgb* out) {
    const auto parts = split(s, ',');
    if (parts.size() != 3) return false;
    int v[3];
    for (int i = 0; i < 3; ++i) {
        v[i] = std::atoi(parts[i].c_str());
        if (v[i] < 0 || v[i] > 255) return false;
    }
    *out = {static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]),
            static_cast<std::uint8_t>(v[2])};
    return true;
}

void set_error(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
}

}  // namespace

std::vector<Material> parse_materials(std::string_view text, std::string* error) {
    const Config cfg = Config::parse(text);
    std::vector<Material> out;
    for (const std::string& section : cfg.sections()) {
        if (section.rfind("material.", 0) != 0) continue;
        Material m;
        m.id = section.substr(9);
        const std::string p = section + ".";
        m.name = cfg.get_string(p + "name", m.id);
        if (!parse_rgb(cfg.get_string(p + "rgb"), &m.rgb)) {
            set_error(error, "material " + m.id + ": bad rgb");
            return {};
        }
        m.solid = cfg.get_bool(p + "solid", false);
        m.height = cfg.get_float(p + "height", 1.0f);
        m.pattern = parse_pattern(cfg.get_string(p + "pattern", "none"));
        out.push_back(std::move(m));
    }
    return out;
}

std::optional<Map2D> parse_map(std::string_view text, const std::vector<Material>& palette,
                               std::string* error) {
    const auto grid_pos = text.find("[grid]");
    if (grid_pos == std::string_view::npos) {
        set_error(error, "map has no [grid] section");
        return std::nullopt;
    }
    const Config header = Config::parse(text.substr(0, grid_pos));

    // Legend: character -> material id. Keys are single characters; '#' cannot
    // be a key because it starts a comment, so the legend spells it "hash".
    std::map<char, std::string> legend;
    for (const auto& [key, value] : header.section("legend")) {
        if (key == "hash") legend['#'] = value;
        else if (key.size() == 1) legend[key[0]] = value;
    }

    // Collect the materials this map uses, in palette order so indices are
    // stable across maps that share a palette.
    std::vector<Material> materials = palette;
    std::map<std::string, std::uint8_t> by_id;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        by_id[materials[i].id] = static_cast<std::uint8_t>(i);
    }

    // Grid rows: everything after the [grid] line, blank lines ignored.
    std::vector<std::string> rows;
    std::string_view rest = text.substr(grid_pos + 6);
    std::size_t start = 0;
    while (start < rest.size()) {
        std::size_t end = rest.find('\n', start);
        if (end == std::string_view::npos) end = rest.size();
        std::string_view line = rest.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (!line.empty()) rows.emplace_back(line);
        start = end + 1;
    }
    if (rows.empty()) {
        set_error(error, "map grid is empty");
        return std::nullopt;
    }
    std::size_t width = 0;
    for (const auto& r : rows) width = std::max(width, r.size());
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(rows.size());

    std::vector<std::uint8_t> cells(static_cast<std::size_t>(w * h), 0);
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            // Short rows are padded with the first legend entry for walls.
            const char c = x < static_cast<int>(rows[z].size()) ? rows[z][x] : '#';
            const auto it = legend.find(c);
            if (it == legend.end()) {
                set_error(error, std::string("unknown map character '") + c + "' in row " +
                                     std::to_string(z));
                return std::nullopt;
            }
            const auto mat = by_id.find(it->second);
            if (mat == by_id.end()) {
                set_error(error, "unknown material '" + it->second + "'");
                return std::nullopt;
            }
            cells[static_cast<std::size_t>(z * w + x)] = mat->second;
        }
    }

    Map2D map(header.get_string("name", "Unnamed"), w, h, header.get_float("cell_size", 0.5f),
              std::move(materials), std::move(cells));
    map.set_description(header.get_string("description"));
    if (map.free_cells().empty()) {
        set_error(error, "map has no free cells");
        return std::nullopt;
    }
    return map;
}

std::optional<Map2D> load_map(const std::string& id, std::string* error) {
    const auto palette_text = data::read("maps/materials.cfg");
    if (!palette_text) {
        set_error(error, "missing maps/materials.cfg");
        return std::nullopt;
    }
    const auto palette = parse_materials(*palette_text, error);
    if (palette.empty()) return std::nullopt;
    const auto text = data::read("maps/" + id + ".txt");
    if (!text) {
        set_error(error, "missing map " + id);
        return std::nullopt;
    }
    return parse_map(*text, palette, error);
}

std::vector<MapInfo> list_maps() {
    std::vector<MapInfo> out;
    for (const std::string& path : data::list("maps/")) {
        if (path.size() < 4 || path.substr(path.size() - 4) != ".txt") continue;
        const auto text = data::read(path);
        if (!text) continue;
        const auto grid = text->find("[grid]");
        const Config header = Config::parse(std::string_view(*text).substr(0, grid));
        MapInfo info;
        info.id = path.substr(5, path.size() - 9);
        info.name = header.get_string("name", info.id);
        info.description = header.get_string("description");
        info.difficulty = header.get_string("difficulty");
        info.order = header.get_int("order", 100);
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const MapInfo& a, const MapInfo& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

}  // namespace wobo
