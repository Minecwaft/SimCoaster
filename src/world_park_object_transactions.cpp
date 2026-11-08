#include "world_person_use_internal.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_grid.hpp"

namespace simcoaster {
using namespace person_use;

std::int32_t calculate_shop_goods_cost(AttractionWorld& c,
                                       const ParkObjectRuntimeView& object,
                                       float quality,
                                       float ingredient,
                                       float portion) {
    const auto quality_adjustment =
        std::clamp(static_cast<float>((static_cast<double>(quality) - 50.0) * static_cast<double>(.005f)), -.5f, .5f);
    const auto ingredient_adjustment =
        std::clamp(static_cast<float>((static_cast<double>(ingredient) - 50.0) * static_cast<double>(.005f)),
                   -.5f,
                   .5f);
    const auto portion_factor =
        portion >= 0 ? static_cast<float>(static_cast<double>(portion) * static_cast<double>(.01f)) : 1.f;
    const auto kind = static_cast<Ingredient>(get_park_object_balance(c, object).UsageInfo.SpecialIngredient);
    const double signed_ingredient = kind == Ingredient::fat || kind == Ingredient::ice
                                       ? -static_cast<double>(ingredient_adjustment)
                                       : ingredient_adjustment;
    const auto cost =
        integer((signed_ingredient + quality_adjustment + 1.0) * fixed::word(object.cost_of_goods) * portion_factor);
    return cost ? cost : 1;
}

std::int32_t calculate_current_shop_goods_cost(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    const auto shop = static_cast<Shop>(get_park_object_balance(c, object).UsageInfo.ShopType);
    const bool fixed_portion = shop == Shop::gift
                            || shop == Shop::costume
                            || shop == Shop::balloon
                            || shop == Shop::photo
                            || shop == Shop::umbrella;
    return calculate_shop_goods_cost(c,
                                     object,
                                     static_cast<float>(static_cast<std::uint8_t>(object.quality_of_goods)),
                                     static_cast<float>(static_cast<std::uint8_t>(object.amount_of_special_ingredient)),
                                     fixed_portion ? -1.f : static_cast<float>(fixed::word(object.portion_size)));
}

void record_park_object_use_expense(AttractionWorld& c, ParkObjectRuntimeView& object, std::int32_t amount) {
    if (!object.history)
        return;
    object.history->dword_history_30[1].temporary += static_cast<std::uint32_t>(amount);
    object.history->dword_history_144[1].temporary += static_cast<std::uint32_t>(amount);
    const auto category = get_park_object_definition(c, &object)->which_ui_type;
    if (category == ObjectUiCategory::shops)
        progress(c, ChallengeType::shop_use_profit, fixed::negate(amount));
    else if (category == ObjectUiCategory::sideshows)
        progress(c, ChallengeType::sideshow_use_profit, fixed::negate(amount));
}

void record_park_object_payment_and_challenge_progress(AttractionWorld& c,
                                                       ParkObjectRuntimeView& object,
                                                       std::uint32_t amount) {
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition)
        throw std::logic_error("payment object definition");
    //The native history-owner guard covers bank income and challenge progress too.
    if (!object.history)
        return;
    (void)apply_bank_balance_and_profit_delta(c,
                                              &record<BankRecord>(c, person_detail::base_world(c).saved.bank_account),
                                              fixed::word(amount));
    object.history->dword_history_30[0].temporary += amount;
    object.history->dword_history_144[0].temporary += amount;
    auto& history = analyser(c).long_histories;
    match (definition->which_ui_type) {
        case ObjectUiCategory::rides      => history[10].temporary += amount;
        case ObjectUiCategory::facilities => history[11].temporary += amount;
        case ObjectUiCategory::sideshows  => {
            history[9].temporary += amount;
            progress(c, ChallengeType::sideshow_use_profit, fixed::word(amount));
        }
        case ObjectUiCategory::shops => {
            history[8].temporary += amount;
            progress(c, ChallengeType::shop_use_profit, fixed::word(amount));
            const auto& balance = *definition->balance;
            match (static_cast<Shop>(balance.UsageInfo.ShopType)) {
                case Shop::gift                            => progress(c, ChallengeType::gift_sales, 1);
                case Shop::restaurant                      => progress(c, ChallengeType::restaurant_uses, 1);
                case Shop::costume                         => progress(c, ChallengeType::costume_sales, 1);
                case Shop::balloon                         => progress(c, ChallengeType::balloon_sales, 1);
                case Shop::food or Shop::drinks_and_sweets => {
                    match (static_cast<Ingredient>(balance.UsageInfo.SpecialIngredient)) {
                        case Ingredient::fat  => progress(c, ChallengeType::fat_item_sales, 1);
                        case Ingredient::salt => progress(c, ChallengeType::salt_item_sales, 1);
                        case Ingredient::ice  => {
                            progress(c, ChallengeType::ice_item_sales, 1);
                            progress(c, ChallengeType::ice_or_sugar_revenue, fixed::word(amount));
                        }
                        case Ingredient::sugar => {
                            progress(c, ChallengeType::sugar_item_sales, 1);
                            progress(c, ChallengeType::ice_or_sugar_revenue, fixed::word(amount));
                        }
                        case _ => ;
                    }
                }
                case _ => ;
            }
        }
        case _ => ;
    }
}

void apply_toilet_use_wear(AttractionWorld& c, ParkObjectRuntimeView& object, std::uint32_t amount) {
    const auto previously_dirty = park_object_requires_staff_service(c, object);
    adjust(object.repair_state, static_cast<double>(amount) * static_cast<double>(-.05f));
    if (park_object_requires_staff_service(c, object) && !previously_dirty) {
        const auto cell = person_detail::cell_id(object.base);
        (void)remove_world_re_cell_effect_profile(c.simulation.region_effects,
                                                  RegionEffectProfileId::clean_toilet,
                                                  cell);
        (void)add_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::dirty_toilet, cell);
    }
}

void close_park_object_after_blocked_person_exit(AttractionWorld& c, ParkObjectRuntimeView& object) {
    if (!object.can_load)
        return;
    WorldEvent event{ ParkObjectClosingEvent{ object.base.header.id } };
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
    object.can_load = 0;
    object.person_being_loaded = 0;
    auto* script = world_operations::find_script(c, object.script_id);
    world_operations::set_script_slot(c, script, 6, 1);
    world_operations::select_park_object_mesh_animation_variant(c,
                                                                object.mesh_instance_id,
                                                                ParkObjectMeshVariant::closed);
}
} //namespace simcoaster
