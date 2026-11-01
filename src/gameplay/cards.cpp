#include "gameplay/cards.h"

#include "core/data.h"

namespace wobo {

const char* card_category_name(CardCategory c) {
    switch (c) {
        case CardCategory::Ability: return "New ability";
        case CardCategory::Upgrade: return "Upgrade";
        case CardCategory::Energy: return "Energy";
        case CardCategory::Passive: return "Passive";
        case CardCategory::Special: return "One-shot";
    }
    return "";
}

std::vector<CardDef> parse_cards(const Config& cfg, std::string* error) {
    std::vector<CardDef> out;
    for (const std::string& section : cfg.sections()) {
        if (section.rfind("card.", 0) != 0) continue;
        const std::string p = section + ".";
        CardDef c;
        c.id = section.substr(5);
        c.name = cfg.get_string(p + "name", c.id);
        c.description = cfg.get_string(p + "description");
        const std::string cat = cfg.get_string(p + "category", "upgrade");
        if (cat == "ability") c.category = CardCategory::Ability;
        else if (cat == "upgrade") c.category = CardCategory::Upgrade;
        else if (cat == "energy") c.category = CardCategory::Energy;
        else if (cat == "passive") c.category = CardCategory::Passive;
        else if (cat == "special") c.category = CardCategory::Special;
        else if (error) *error = "card " + c.id + ": unknown category " + cat;
        c.weight = cfg.get_float(p + "weight", 1.0f);
        c.max_stack = cfg.get_int(p + "max_stack", 1);
        c.requires_action = cfg.get_string(p + "requires");
        c.min_day = cfg.get_int(p + "min_day", 1);
        c.effects = cfg.get_all(p + "effect");
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<CardDef> load_cards(std::string* error) {
    const auto text = data::read("cards/cards.cfg");
    if (!text) {
        if (error) *error = "missing cards/cards.cfg";
        return {};
    }
    return parse_cards(Config::parse(*text), error);
}

void CardDraftSystem::reset(std::vector<CardDef> cards, std::uint64_t seed) {
    cards_ = std::move(cards);
    taken_.clear();
    rng_.reseed(seed);
}

int CardDraftSystem::taken(const std::string& id) const {
    const auto it = taken_.find(id);
    return it == taken_.end() ? 0 : it->second;
}

const CardDef* CardDraftSystem::find(const std::string& id) const {
    for (const CardDef& c : cards_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

std::vector<const CardDef*> CardDraftSystem::draw_from(std::vector<const CardDef*> eligible, int count) {
    std::vector<const CardDef*> offer;
    std::map<CardCategory, int> offered;
    while (static_cast<int>(offer.size()) < count && !eligible.empty()) {
        float total = 0.0f;
        std::vector<float> w(eligible.size());
        for (std::size_t i = 0; i < eligible.size(); ++i) {
            const int same = offered[eligible[i]->category];
            w[i] = eligible[i]->weight * (same > 0 ? 0.2f : 1.0f);
            total += w[i];
        }
        float r = rng_.uniform() * total;
        std::size_t pick = eligible.size() - 1;
        for (std::size_t i = 0; i < eligible.size(); ++i) {
            if (r < w[i]) {
                pick = i;
                break;
            }
            r -= w[i];
        }
        offer.push_back(eligible[pick]);
        ++offered[eligible[pick]->category];
        eligible.erase(eligible.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    return offer;
}

}  // namespace wobo
