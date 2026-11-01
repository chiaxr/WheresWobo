#pragma once

// Roguelike drafting. Cards are data (data/cards/cards.cfg); each carries
// effects that grant actions, modify RunStats, add passive stacks or fire a
// one-shot. Offers are drawn from a seeded stream, so the same seed and the
// same picks always produce the same drafts.

#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/rng.h"

namespace wobo {

enum class CardCategory { Ability, Upgrade, Energy, Passive, Special };
const char* card_category_name(CardCategory c);

struct CardDef {
    std::string id;
    std::string name;
    std::string description;
    CardCategory category = CardCategory::Upgrade;
    float weight = 1.0f;
    int max_stack = 1;
    std::string requires_action;  // action key that must be owned, or empty
    int min_day = 1;
    std::vector<std::string> effects;
};

std::vector<CardDef> parse_cards(const Config& cfg, std::string* error = nullptr);
std::vector<CardDef> load_cards(std::string* error = nullptr);

class CardDraftSystem {
public:
    void reset(std::vector<CardDef> cards, std::uint64_t seed);

    // Up to `count` distinct eligible cards. `owned` says whether an action
    // key is owned. Categories are spread out: once a category is offered,
    // further cards of it are much less likely in the same offer.
    template <typename OwnedFn>
    std::vector<const CardDef*> draw(int count, int day, OwnedFn owned);

    void record_pick(const CardDef& card) { ++taken_[card.id]; }
    int taken(const std::string& id) const;
    const std::vector<CardDef>& cards() const { return cards_; }
    const CardDef* find(const std::string& id) const;

private:
    std::vector<const CardDef*> draw_from(std::vector<const CardDef*> eligible, int count);

    std::vector<CardDef> cards_;
    std::map<std::string, int> taken_;
    Rng rng_;
};

template <typename OwnedFn>
std::vector<const CardDef*> CardDraftSystem::draw(int count, int day, OwnedFn owned) {
    std::vector<const CardDef*> eligible;
    for (const CardDef& c : cards_) {
        if (taken(c.id) >= c.max_stack) continue;
        if (day < c.min_day) continue;
        if (!c.requires_action.empty() && !owned(c.requires_action)) continue;
        // An ability card is pointless once its action is owned.
        bool grants_owned = false;
        for (const std::string& e : c.effects) {
            if (e.rfind("grant ", 0) == 0 && owned(e.substr(6))) grants_owned = true;
        }
        if (grants_owned) continue;
        eligible.push_back(&c);
    }
    return draw_from(std::move(eligible), count);
}

}  // namespace wobo
