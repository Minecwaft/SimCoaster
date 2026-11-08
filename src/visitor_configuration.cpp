#include "simcoaster/guest_flow_balance.hpp"
#include "simcoaster/person_balance.hpp"
#include "simcoaster/person_environment.hpp"

namespace simcoaster {
std::expected<void, sam::Error>
load_guest_flow_balance(GuestFlowBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<GuestFlowBalance, true>(balance, text, preserve);
}

std::expected<void, sam::Error>
load_visitor_advisor_balance(VisitorAdvisorBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<VisitorAdvisorBalance, true>(balance, text, preserve);
}

std::expected<void, sam::Error> load_person_balance(PersonBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<PersonBalance, true>(balance, text, preserve);
}

std::expected<void, sam::Error>
load_person_creation_balance(PersonCreationBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<PersonCreationBalance, true>(balance, text, preserve);
}

std::expected<void, sam::Error>
load_person_environment_balance(PersonEnvironmentBalance& balance, std::string_view text, bool preserve) {
    return sam::parse_record<PersonEnvironmentBalance, true>(balance, text, preserve);
}

std::expected<void, sam::Error> load_arrival_balance(ArrivalParameters& balance, std::string_view text, bool preserve) {
    ArrivalBalanceFile file{ balance };
    const auto result = sam::parse_record<ArrivalBalanceFile, true>(file, text, preserve);
    balance = file.Arrival;
    return result;
}
} //namespace simcoaster
