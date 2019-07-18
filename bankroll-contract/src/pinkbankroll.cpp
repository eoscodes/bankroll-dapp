#include <pinkbankroll.hpp>

//Only needs to be called once after contract creation
ACTION pinkbankroll::init() {
  require_auth(_self);
  statsTable.get_or_create(_self, statsStruct{});
}


ACTION pinkbankroll::announceroll(name creator, uint64_t creator_id, uint32_t upper_range_limit, name rake_recipient) {
  require_auth(creator);
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id == rolls_by_creator_and_id.end(),
  "creator already created a roll with this creator_id");
  
  uint64_t roll_id = rollsTable.available_primary_key();
  rollsTable.emplace(creator, [&](rollStruct &r) {
    r.roll_id = roll_id;
    r.creator = creator;
    r.creator_id = creator_id;
    r.upper_range_limit = upper_range_limit;
    r.rake_recipient = rake_recipient;
    r.total_amount = asset(0, symbol("WAX", 8));
  });
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logannounce"_n,
    std::make_tuple(roll_id, creator, creator_id, upper_range_limit, rake_recipient)
  ).send();
}


ACTION pinkbankroll::announcebet(name creator, uint64_t creator_id, name bettor, asset amount, uint32_t lower_bound, uint32_t upper_bound, uint32_t muliplier) {
  require_auth(creator);
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id != rolls_by_creator_and_id.end(),
  "No bet with the specified creator_id has been announced");
  
  check(amount.symbol == symbol("WAX", 8),
  "amount must be in WAX");
  
  check(lower_bound >= 1,
  "lower_bound needs to be at least 1");
  check(lower_bound <= upper_bound,
  "lower_bound can't be greater than upper_bound");
  check(upper_bound <= itr_creator_and_id->upper_range_limit,
  "upper_bound can't be greater than the upper_range_limit of the roll");
  
  
  double odds = (double)(upper_bound - lower_bound + 1) / (double)(itr_creator_and_id->upper_range_limit);
  double ev = odds * muliplier / double{1000};
  check(ev <= 0.99,
  "the bet cant have an EV greater than 0.99 * amount");
  
  rolls_by_creator_and_id.modify(itr_creator_and_id, creator, [&](auto& r) {
    r.total_amount.amount += amount.amount;
  });
  
  uint64_t roll_id = itr_creator_and_id->roll_id;
  
  rollBets_t betsTable(_self, roll_id);
  uint64_t bet_id = betsTable.available_primary_key();
  betsTable.emplace(creator, [&](betStruct &b) {
    b.bet_id = bet_id;
    b.bettor = bettor;
    b.amount = amount;
    b.lower_bound = lower_bound;
    b.upper_bound = upper_bound;
    b.muliplier = muliplier;
  });
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logannounce"_n,
    std::make_tuple(roll_id, bet_id, creator, creator_id, bettor, amount, lower_bound, upper_bound, muliplier)
  ).send();
}


ACTION pinkbankroll::withdraw(name from, asset amount) {
  require_auth(from);
  
}


ACTION pinkbankroll::receiverand(uint64_t assoc_id, checksum256 random_value) {
  require_auth("orng.wax"_n);
  
}


void pinkbankroll::receivetransfer(name from, name to, asset quantity, std::string memo) {
  require_auth("eosio.token"_n);
  statsStruct stats = statsTable.get_or_create(_self, statsStruct{});
  stats.bankroll = quantity;
  statsTable.set(stats, _self);
}


//Only for external logging
  
ACTION pinkbankroll::logannounce(uint64_t roll_id, name creator, uint64_t creator_id, uint32_t upper_range_limit, name rake_recipient) {
  require_auth(_self);
}

ACTION pinkbankroll::logbet(uint64_t roll_id, uint64_t bet_id, name creator, uint64_t creator_id, name bettor, asset amount, uint32_t lower_bound, uint32_t upper_bound, uint32_t muliplier) {
  require_auth(_self);
}