#include "world/map_loader.h"

#include <algorithm>
#include <map>

#include <cstdlib>

#include "core/config.h"
#include "core/data.h"
#include "world/props.h"

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
        m.look = cfg.get_string(p + "look");
        out.push_back(std::move(m));
    }
    return out;
}

namespace {

// A file split at its section headers: the config part (everything but the
// raw sections, which a Config would mangle: '#' is a wall in a grid) and
// the raw sections' lines.
struct Sections {
    std::string config;
    std::map<std::string, std::vector<std::string>> raw;
};

bool is_raw(const std::string& name) { return name == "grid" || name == "props" || name == "movers"; }

Sections split_sections(std::string_view text) {
    Sections out;
    std::string current;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (!line.empty() && line.front() == '[' && line.back() == ']') {
            current = std::string(trim(line.substr(1, line.size() - 2)));
            if (!is_raw(current)) out.config += std::string(line) + "\n";
            continue;
        }
        if (is_raw(current)) {
            if (!line.empty()) out.raw[current].emplace_back(line);
        } else {
            out.config += std::string(line) + "\n";
        }
    }
    return out;
}

// `kind x z key=value key="a value" ...`
bool parse_placement(const std::string& line, std::string* kind, Vec2* pos,
                     std::map<std::string, std::string>* params) {
    std::vector<std::string> tokens;
    std::string tok;
    bool quoted = false;
    for (const char c : line) {
        if (c == '"') {
            quoted = !quoted;
        } else if (c == ' ' && !quoted) {
            if (!tok.empty()) tokens.push_back(tok);
            tok.clear();
        } else {
            tok += c;
        }
    }
    if (!tok.empty()) tokens.push_back(tok);
    if (tokens.size() < 3) return false;
    *kind = tokens[0];
    char* end = nullptr;
    pos->x = std::strtof(tokens[1].c_str(), &end);
    if (*end) return false;
    pos->z = std::strtof(tokens[2].c_str(), &end);
    if (*end) return false;
    for (std::size_t i = 3; i < tokens.size(); ++i) {
        const auto eq = tokens[i].find('=');
        if (eq == std::string::npos) return false;
        (*params)[tokens[i].substr(0, eq)] = tokens[i].substr(eq + 1);
    }
    return true;
}

}  // namespace

std::optional<Level> parse_level(std::string_view text, const std::vector<Material>& palette,
                                 std::string* error) {
    const Sections sec = split_sections(text);
    const auto grid_it = sec.raw.find("grid");
    if (grid_it == sec.raw.end()) {
        set_error(error, "map has no [grid] section");
        return std::nullopt;
    }
    const Config header = Config::parse(sec.config);

    // Legend: character -> material id. Keys are single characters; '#' cannot
    // be a key because it starts a comment, so the legend spells it "hash".
    std::map<char, std::string> legend;
    for (const auto& [key, value] : header.section("legend")) {
        if (key == "hash") legend['#'] = value;
        else if (key.size() == 1) legend[key[0]] = value;
    }

    // The palette, then this map's own materials (replacing any with the
    // same id), so indices of shared materials are stable across maps.
    std::vector<Material> materials = palette;
    std::string mat_error;
    for (Material& m : parse_materials(sec.config, &mat_error)) {
        const auto same = std::find_if(materials.begin(), materials.end(),
                                       [&](const Material& o) { return o.id == m.id; });
        if (same != materials.end()) *same = std::move(m);
        else materials.push_back(std::move(m));
    }
    if (!mat_error.empty()) {
        set_error(error, mat_error);
        return std::nullopt;
    }
    if (materials.size() > 250) {
        set_error(error, "too many materials");
        return std::nullopt;
    }
    std::map<std::string, std::uint8_t> by_id;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        by_id[materials[i].id] = static_cast<std::uint8_t>(i);
    }

    const std::vector<std::string>& rows = grid_it->second;
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

    const auto material_of = [&](const std::string& id) -> int {
        const auto it = by_id.find(id);
        return it == by_id.end() ? -1 : it->second;
    };
    std::vector<Prop> props;
    std::vector<Shape> shapes;
    if (const auto it = sec.raw.find("props"); it != sec.raw.end()) {
        for (const std::string& line : it->second) {
            Prop p;
            if (!parse_placement(line, &p.kind, &p.position, &p.params)) {
                set_error(error, "bad prop line: " + line);
                return std::nullopt;
            }
            const PropKind* kind = find_prop_kind(p.kind);
            if (!kind) {
                set_error(error, "unknown prop kind '" + p.kind + "'");
                return std::nullopt;
            }
            p.yaw = p.get_float("yaw", 0.0f) * kDegToRad;
            const int mat = material_of(p.get("mat", kind->material));
            const int chair = material_of(p.get("chair_mat", "chair"));
            if (mat < 0 || chair < 0) {
                set_error(error, "prop " + p.kind + ": unknown material");
                return std::nullopt;
            }
            p.material = static_cast<std::uint8_t>(mat);
            for (const Shape& sh : prop_footprint(p, static_cast<std::uint8_t>(chair))) shapes.push_back(sh);
            props.push_back(std::move(p));
        }
    }

    Level level;
    level.map = Map2D(header.get_string("name", "Unnamed"), w, h, header.get_float("cell_size", 0.5f),
                      std::move(materials), std::move(cells), std::move(shapes), std::move(props));
    level.map.set_description(header.get_string("description"));
    if (level.map.free_cells().empty()) {
        set_error(error, "map has no free cells");
        return std::nullopt;
    }
    if (const auto it = sec.raw.find("movers"); it != sec.raw.end()) {
        for (const std::string& line : it->second) {
            MoverSpec m;
            if (!parse_placement(line, &m.kind, &m.position, &m.params)) {
                set_error(error, "bad mover line: " + line);
                return std::nullopt;
            }
            level.movers.push_back(std::move(m));
        }
    }
    return level;
}

std::optional<Map2D> parse_map(std::string_view text, const std::vector<Material>& palette,
                               std::string* error) {
    auto level = parse_level(text, palette, error);
    if (!level) return std::nullopt;
    return std::move(level->map);
}

std::optional<Level> load_level(const std::string& id, std::string* error) {
    const auto palette_text = data::read("maps/materials.cfg");
    if (!palette_text) {
        set_error(error, "missing maps/materials.cfg");
        return std::nullopt;
    }
    const auto palette = parse_materials(*palette_text, error);
    if (palette.empty()) return std::nullopt;
    const auto text = data::read("maps/" + id + "/map.txt");
    if (!text) {
        set_error(error, "missing map " + id);
        return std::nullopt;
    }
    return parse_level(*text, palette, error);
}

std::optional<Map2D> load_map(const std::string& id, std::string* error) {
    auto level = load_level(id, error);
    if (!level) return std::nullopt;
    return std::move(level->map);
}

std::vector<MapInfo> list_maps() {
    std::vector<MapInfo> out;
    const std::string suffix = "/map.txt";
    for (const std::string& path : data::list("maps/")) {
        if (path.size() <= 5 + suffix.size() || path.substr(path.size() - suffix.size()) != suffix) continue;
        const auto text = data::read(path);
        if (!text) continue;
        const Config header = Config::parse(split_sections(*text).config);
        MapInfo info;
        info.id = path.substr(5, path.size() - 5 - suffix.size());
        info.name = header.get_string("name", info.id);
        info.description = header.get_string("description");
        info.difficulty = header.get_string("difficulty");
        info.order = header.get_int("order", 100);
        const auto focus = split(header.get_string("focus"), ',');
        if (focus.size() == 2) {
            info.focus = {std::strtof(focus[0].c_str(), nullptr), std::strtof(focus[1].c_str(), nullptr)};
            info.has_focus = true;
        }
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const MapInfo& a, const MapInfo& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

}  // namespace wobo
