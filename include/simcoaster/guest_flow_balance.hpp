#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct FirstHourParameters {
    [[=sam::Field{ "Minutes", sam::Kind::nonnegative_integer }]] std::int32_t Minutes;
};

struct GuestFlowBalance {
    [[=sam::Group{ "FirstHour" }]] FirstHourParameters FirstHour;
};

struct AdvisorNewTutorialParameters {
    [[=sam::Field{ "ADVISORFEATURESNOPHOTOKIOSKS", sam::Kind::integer }]] std::int32_t ADVISORFEATURESNOPHOTOKIOSKS;
    [[=sam::Field{ "ADVISORNOTRIGHTCLICKED", sam::Kind::integer }]] std::int32_t ADVISORNOTRIGHTCLICKED;
    [[=sam::Field{ "ADVISORSTUTBUYSHOPS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUYSHOPS;
    [[=sam::Field{ "ADVISORSTUTBUILDRIDE", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDRIDE;
    [[=sam::Field{ "ADVISORSTUTBUILDCOASTER", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDCOASTER;
    [[=sam::Field{ "ADVISORSTUTHIREMECHANIC", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREMECHANIC;
    [[=sam::Field{ "ADVISORSTUTHIREHANDYMAN", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREHANDYMAN;
    [[=sam::Field{ "ADVISORSTUTHIREENTERTAINER", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREENTERTAINER;
    [[=sam::Field{ "ADVISORSTUTBUILDSIDESHOW", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDSIDESHOW;
    [[=sam::Field{ "ADVISORSTUTWELLDONE1", sam::Kind::integer }]] std::int32_t ADVISORSTUTWELLDONE1;
    [[=sam::Field{ "ADVISORSTUTVISITORINFO", sam::Kind::integer }]] std::int32_t ADVISORSTUTVISITORINFO;
    [[=sam::Field{ "ADVISORSTUTREADEMAILS", sam::Kind::integer }]] std::int32_t ADVISORSTUTREADEMAILS;
    [[=sam::Field{ "ADVISORSTUTARROWKEYS", sam::Kind::integer }]] std::int32_t ADVISORSTUTARROWKEYS;
    [[=sam::Field{ "ADVISORSTUTBUILDSTAFFROOM", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDSTAFFROOM;
    [[=sam::Field{ "ADVISORSTUTBUILDTOILETS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDTOILETS;
    [[=sam::Field{ "ADVISORSTUTBUILDPLANTS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDPLANTS;
    [[=sam::Field{ "ADVISORSTUTHIREGARDENER", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREGARDENER;
    [[=sam::Field{ "ADVISORSTUTRESEARCHEXPANSION", sam::Kind::integer }]] std::int32_t ADVISORSTUTRESEARCHEXPANSION;
    [[=sam::Field{ "ADVISORSTUTENTRYPRICE", sam::Kind::integer }]] std::int32_t ADVISORSTUTENTRYPRICE;
    [[=sam::Field{ "ADVISORSTUTWELLDONE1NUMVIS", sam::Kind::integer }]] std::int32_t ADVISORSTUTWELLDONE1NUMVIS;
    [[=sam::Field{ "ADVISORSTUTVISITORINFONUMVIS", sam::Kind::integer }]] std::int32_t ADVISORSTUTVISITORINFONUMVIS;
    [[=sam::Field{ "ADVISORSTUTENTRYPRICEMINRIDES", sam::Kind::integer }]] std::int32_t ADVISORSTUTENTRYPRICEMINRIDES;
    [[=sam::Field{ "ADVISORSTUTBUILDPATHS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDPATHS;
};

//These are the guest-flow projections of World/Advisor SAM storage. The full
//advisor behavior loader and scoring/queue backend retain their own contract.
struct VisitorAdvisorBalance {
    [[=sam::Group{ "New" }]] AdvisorNewTutorialParameters New;
};

std::expected<void, sam::Error> load_guest_flow_balance(GuestFlowBalance&, std::string_view, bool preserve = false);
std::expected<void, sam::Error>
load_visitor_advisor_balance(VisitorAdvisorBalance&, std::string_view, bool preserve = false);
} //namespace simcoaster
