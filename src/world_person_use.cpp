#include "world_person_use_internal.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/sprite_catalog.hpp"

namespace simcoaster {
using namespace person_use;

namespace {
std::uint32_t member_variant(VisitorWorld& c, std::uint32_t category) {
    const auto* catalog = c.resources.variants.catalog;
    if (!catalog)
        throw std::logic_error("purchase needs sprite metadata");
    const auto first = catalog->variants.at(category).first_loaded_resource_index;
    const auto count =
        catalog->resources.at(static_cast<std::size_t>(first)).descriptor.nonzero_frame_stride_entry_count();
    if (!count)
        throw std::domain_error("purchase sprite has no frame variants");
    return (static_cast<std::uint32_t>(next_world_prng_value(person_detail::primary_world(c))) >> 2) % count;
}

std::uint32_t category_variant(VisitorWorld& c, std::uint32_t category) {
    const auto count = get_resource_category_appearance_variant_count(c, category);
    if (!count)
        throw std::domain_error("purchase sprite category is empty");
    return (static_cast<std::uint32_t>(next_world_prng_value(person_detail::primary_world(c))) >> 2)
         % static_cast<std::uint32_t>(count);
}

void give_appearance(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object) {
    const auto& balance = c.visitors.needs_balance.PeepInfo;
    const auto quality = static_cast<std::uint8_t>(object.quality_of_goods);
    const auto poor_quality = [&](bool signed_threshold = false) {
        const auto threshold = static_cast<std::uint32_t>(balance.ComplainAboutThisQuality) + random(c) % 10u;
        return signed_threshold ? static_cast<std::int32_t>(quality) < fixed::word(threshold) : quality < threshold;
    };
    match (static_cast<PersonAppearanceEffect>(get_park_object_balance(c, object).UsageInfo.AppearanceEffect)) {
        case PersonAppearanceEffect::umbrella => {
            p.umbrella_state = PersonUmbrellaState::creation_pending;
            p.umbrella_type = member_variant(c, 19);
            if (!p.umbrella_script) {
                set_mobile_entity_purpose_speed(p.mobile, PurposeSpeedMode::normal, p.umbrella_state);
                p.umbrella_script = world_operations::allocate_resource_variant(c,
                                                                                { ResourceProgramFamily::umbrella, 0 },
                                                                                { 19, 0, p.umbrella_type, 0, 0, 0 });
                p.umbrella_state = PersonUmbrellaState::active;
            }
            ++analyser(c, true).umbrella_sales_count;
            if (poor_quality())
                short_history(object, 9);
            p.bought_score = 33.f;
        }
        case PersonAppearanceEffect::costume => {
            if (p.mobile.sprite_category == 6) {
                p.mobile.sprite_category = 0;
                person_detail::base_world(c).saved.random_seed = p.base.header.id;
                p.mobile.sprite_id = category_variant(c, 0);
            } else {
                p.mobile.sprite_category = 6;
                p.mobile.sprite_id = category_variant(c, 6);
                ++analyser(c, true).costumes_sold_ever;
                (void)random(c);
                if (poor_quality())
                    short_history(object, 9);
                p.bought_score = 33.f;
            }
        }
        case PersonAppearanceEffect::balloon => {
            p.balloon_colour = member_variant(c, 14);
            p.balloon_script = world_operations::allocate_resource_variant(c,
                                                                           { ResourceProgramFamily::balloon_idle, 0 },
                                                                           { 14, 0, p.balloon_colour, 0, 0, 0 });
            p.remaining_balloon_life = c.visitors.balloon_life_maximum * quality / 100u;
            if (p.remaining_balloon_life < c.visitors.balloon_life_minimum)
                p.remaining_balloon_life = c.visitors.balloon_life_minimum;
            else if (p.remaining_balloon_life > c.visitors.balloon_life_maximum)
                p.remaining_balloon_life = c.visitors.balloon_life_maximum;
            ++analyser(c, true).balloons_sold_ever;
            if (poor_quality(true)) {
                short_history(object, 9);
                adjust(p.bought_score, 33.0);
            } else
                adjust(p.bought_score, -33.0);
        }
        case _ => ;
    }
}

void choose_photo_booth(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object, float aggregate) {
    if (!object.photo_booth) {
        p.photo_booth = 0;
        return;
    }
    auto& booth = person_detail::park_object(c, object.photo_booth);
    const auto entry = get_back_of_queue(c, &booth);
    const auto distance = person_detail::squared_cell_distance(person_detail::cell_id(p.base), entry);
    const auto distance_score = std::min(100u, static_cast<std::uint32_t>(fixed::word(100u * distance) / 450));
    const auto& policy = c.visitors.needs_balance.PeepInfo;
    const auto weighted_distance =
        fixed::word(static_cast<std::uint32_t>(policy.DistanceWeightForPhoto) * (100u - distance_score));
    const auto score = integer(static_cast<double>(weighted_distance)
                               + static_cast<double>(policy.SatisfactionWeightForPhoto) * aggregate);
    if (score > policy.PhotoThreshold)
        p.photo_booth = object.photo_booth;
    //With a linked booth but an insufficient score, native retains the old ID.
}

void use_relief(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object, float& aggregate) {
    const auto& policy = c.visitors.needs_balance.PeepInfo;
    if (const auto owner = object.booby_trap_owner) {
        const auto* culprit = owner < c.entities.size() ? c.entities[owner].payload : nullptr;
        //Native saves can carry an orphaned owner. Preserve real pranks, while
        //preventing their activation from dereferencing a missing/wrong actor.
        if (!culprit || culprit->retirement_pending || culprit->kind != ThingKind::person)
            object.booby_trap_owner = 0;
    }
    if (object.booby_trap_owner) {
        apply_toilet_use_wear(c, object, 2000);
        WorldEvent event{ PersonActionEvent{ object.booby_trap_owner, PersonActionCode::toilet_booby_trap } };
        dispatch_world_event_to_subscribers(c,
                                            get_world_event_subscription_registry(c),
                                            &event,
                                            deliver_world_record_event,
                                            nullptr);
        object.booby_trap_owner = 0;
        adjust(p.happiness, -static_cast<double>(static_cast<std::uint8_t>(policy.BigHappinessChange)));
        p.needs.sickness = 95.f;
        (void)create_runtime_template_at_mobile_entity_resource_position(c, p.mobile, 38, 0, 0);
    } else
        apply_toilet_use_wear(c, object, byte(p.needs.toilet_need));
    if (p.toilet_booby_flag) {
        object.booby_trap_owner = p.base.header.id;
        p.toilet_booby_flag = 0;
        adjust(p.happiness, 1.0);
        thought(c, p, MobileThoughtCode::naughty);
    }
    std::int32_t happiness;
    if (byte(p.needs.sickness) > 90) {
        p.needs.sickness = 0;
        happiness = -50;
    } else
        happiness = park_object_requires_staff_service(c, object) ? -20 : byte(p.needs.toilet_need) >> 1;
    adjust(p.happiness, happiness);
    aggregate = static_cast<float>(static_cast<double>(aggregate) + happiness);
    p.needs.toilet_need = 0;
    set_mobile_entity_purpose_speed(p.mobile, PurposeSpeedMode::boost_25, p.umbrella_state);
}

void record_satisfaction(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object, float aggregate) {
    const auto& policy = c.visitors.needs_balance.PeepInfo;
    const bool can_animate =
        p.umbrella_state == PersonUmbrellaState::none || p.umbrella_state == PersonUmbrellaState::inactive;
    int animation = -1;
    //Comparison order is observable when thresholds are not monotonic.
    if (!(static_cast<double>(policy.HappyDiffForVGood) >= aggregate)) {
        short_history(object, 5);
        if (can_animate)
            match (p.generation) {
                case PersonGeneration::child => animation = 5;
                case PersonGeneration::parent =>
                    animation = 7 - static_cast<int>(world_operations::get_person_sprite_gender_flag(c, p));
                case PersonGeneration::grandparent =>
                    animation = 9 - static_cast<int>(world_operations::get_person_sprite_gender_flag(c, p));
                case _ => ;
            } thought(c, p, MobileThoughtCode::sideshow_win);
    } else if (static_cast<double>(policy.HappyDiffForVPoor) > aggregate) {
        short_history(object, 8);
        if (can_animate)
            animation = 10;
        thought(c, p, MobileThoughtCode::sideshow_loss);
    } else if (static_cast<double>(policy.HappyDiffForGood) < aggregate) {
        short_history(object, 6);
        thought(c, p, MobileThoughtCode::sideshow_win);
    } else if (static_cast<double>(policy.HappyDiffForPoor) > aggregate) {
        short_history(object, 7);
        if (can_animate)
            animation = 10;
        thought(c, p, MobileThoughtCode::sideshow_loss);
    }
    if (animation >= 0)
        start_person_temporary_appearance_animation(c, p, static_cast<std::uint32_t>(animation));
}
} //namespace

std::int32_t
apply_person_ride_excitement_effects(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object) {
    if (!get_park_object_excitement(c, object, false))
        return 0;
    const auto excitement = get_park_object_excitement(c, object, false);
    const auto preferred = static_cast<std::uint8_t>(p.preferred_excitement);
    const auto difference = std::abs(int(preferred) - int(excitement));
    const auto& policy = c.visitors.needs_balance.PeepInfo;
    std::int32_t happiness = 0;
    if (difference < policy.PerfectExcitementMatch)
        happiness = policy.PerfectRide;
    else if (difference < policy.GoodExcitementMatch)
        happiness = policy.GoodRide;
    else if (difference < policy.OKExcitementMatch) {
        happiness = policy.OKRide;
        if (preferred < excitement)
            short_history(object, 9);
    }
    if (difference < policy.PerfectExcitementMatch
        || difference < policy.GoodExcitementMatch
        || difference < policy.OKExcitementMatch)
        adjust(p.rides_score, happiness);
    if (!policy.RideVomitDivisor)
        throw std::domain_error("ride sickness divisor is zero");
    const auto before = byte(p.needs.sickness);
    const auto sickness =
        static_cast<std::int32_t>(excitement) / policy.RideVomitDivisor * ((100 - int(byte(p.needs.hunger))) / 20);
    adjust(p.needs.sickness, sickness);
    const auto after = byte(p.needs.sickness);
    if (after > before)
        adjust(p.rides_score, -static_cast<double>(after - before));
    return happiness;
}

void apply_park_object_use_effects(VisitorWorld& c, PersonRuntimeView& p, ParkObjectRuntimeView& object) {
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition || !definition->balance)
        throw std::logic_error("object-use definition");
    const auto& use = definition->balance->UsageInfo;
    const auto& policy = c.visitors.needs_balance.PeepInfo;
    p.won_last_thang = 1;
    if (definition->which_ui_type == ObjectUiCategory::sideshows) {
        record_park_object_use_expense(c, object, fixed::word(object.cost_of_goods));
        p.cash = fixed::add(p.cash, fixed::word(object.cost_of_goods));
        adjust(p.won_score, 50.0);
    } else if (definition->which_ui_type == ObjectUiCategory::shops)
        record_park_object_use_expense(c, object, calculate_current_shop_goods_cost(c, object));
    const auto excitement = apply_person_ride_excitement_effects(c, p, object);
    adjust(p.happiness, excitement);
    adjust(p.happiness, use.HappinessEffect);
    float aggregate = static_cast<float>(static_cast<double>(use.HappinessEffect) + excitement);
    adjust(p.needs.sickness, use.VomitEffect);
    if (definition->which_ui_type == ObjectUiCategory::shops) {
        adjust(p.needs.thirst, -static_cast<double>(use.ThirstEffect));
        adjust(p.needs.hunger, -static_cast<double>(use.HungerEffect));
        if (use.ThirstEffect > use.HungerEffect)
            sprite_effect(c, p, 131);
        else if (use.ThirstEffect < use.HungerEffect)
            sprite_effect(c, p, 132);
        const auto portion = fixed::word(object.portion_size);
        adjust(p.needs.sickness, portion);
        adjust(p.happiness, portion);
        aggregate = static_cast<float>(static_cast<double>(aggregate) + portion);
        adjust(p.needs.litter_meter, use.LitterEffect);
        adjust(p.needs.litter_meter, portion);
        adjust(p.needs.toilet_need, use.ToiletEffect);
    }
    const auto amount = static_cast<std::uint8_t>(object.amount_of_special_ingredient);
    const auto nourishment = [&](std::int32_t effect, float& score) {
        if (!effect)
            return;
        if (static_cast<std::uint32_t>(amount) + static_cast<std::uint32_t>(effect) + (random(c) & 7u) >= 30u)
            adjust(score, 35.0);
        else {
            const auto change = static_cast<std::uint8_t>(policy.SmallHappinessChange);
            adjust(p.happiness, -static_cast<double>(change));
            aggregate = static_cast<float>(static_cast<double>(aggregate) - change);
            short_history(object, 10);
            adjust(score, -30.0);
        }
    };
    nourishment(use.HungerEffect, p.food_score);
    nourishment(use.ThirstEffect, p.drink_score);
    const auto scaled = [&](std::int32_t effect) {
        return fixed::word(static_cast<std::uint32_t>(amount) * static_cast<std::uint32_t>(effect)) / 100;
    };
    const auto ingredient_happiness = scaled(use.HappinessEffect);
    adjust(p.happiness, ingredient_happiness);
    aggregate = static_cast<float>(static_cast<double>(aggregate) + ingredient_happiness);
    match (static_cast<Ingredient>(use.SpecialIngredient)) {
        case Ingredient::fat  => adjust(p.needs.toilet_need, scaled(fixed::add(use.FatEffect, 1)));
        case Ingredient::salt => adjust(p.needs.thirst, scaled(fixed::add(use.SaltEffect, 1)));
        case Ingredient::ice  => adjust(p.needs.thirst, scaled(fixed::add(use.ThirstEffect, use.IceEffect)));
        case Ingredient::sugar =>
            p.mobile.adjustor_speed =
                static_cast<std::uint16_t>(p.mobile.adjustor_speed + scaled(fixed::add(use.SugarEffect, 1)));
        case _ => ;
    } give_appearance(c, p, object);
    if (has(object, ParkObjectCapabilities::provides_relief))
        use_relief(c, p, object, aggregate);
    if (get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows) {
        ++p.number_of_sideshows_won;
        const auto prize = integer(
            std::log2(static_cast<double>(fixed::word(object.cost_of_goods)) / fixed::word(object.price_per_use))
            * static_cast<std::uint8_t>(policy.MediumHappinessChange));
        adjust(p.happiness, prize);
        aggregate = static_cast<float>(static_cast<double>(aggregate) + prize);
    }
    choose_photo_booth(c, p, object, aggregate);
    record_satisfaction(c, p, object, aggregate);
}
} //namespace simcoaster
