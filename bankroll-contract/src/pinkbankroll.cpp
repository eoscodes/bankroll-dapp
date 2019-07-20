#include <pinkbankroll.hpp>
#include <bankrollmanagement.hpp>

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




ACTION pinkbankroll::announcebet(name creator, uint64_t creator_id, name bettor, asset amount, uint32_t lower_bound, uint32_t upper_bound, uint32_t muliplier, uint64_t random_seed) {
  require_auth(creator);
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id != rolls_by_creator_and_id.end(),
  "No bet with the specified creator_id has been announced");
  
  check(amount.is_valid(),
  "amount is invalid");
  check(amount.symbol == symbol("WAX", 8),
  "amount must be in WAX");
  
  check(lower_bound >= 1,
  "lower_bound needs to be at least 1");
  check(lower_bound <= upper_bound,
  "lower_bound can't be greater than upper_bound");
  check(upper_bound <= itr_creator_and_id->upper_range_limit,
  "upper_bound can't be greater than the upper_range_limit of the roll");
  
  
  double odds = (double)(upper_bound - lower_bound + 1) / (double)(itr_creator_and_id->upper_range_limit);
  double ev = odds * muliplier / (double)1000;
  check(ev <= 0.99,
  "the bet cant have an EV greater than 0.99 * amount");
  
  rolls_by_creator_and_id.modify(itr_creator_and_id, creator, [&](auto& r) {
    r.total_amount += amount;
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
    b.random_seed = random_seed;
  });
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logannounce"_n,
    std::make_tuple(roll_id, bet_id, creator, creator_id, bettor, amount, lower_bound, upper_bound, muliplier, random_seed)
  ).send();
}




ACTION pinkbankroll::payoutbet(name from, asset amount) {
  check(has_auth(from) || has_auth(_self),
  "transaction doesn't have the required permission");
  
  check(amount.is_valid(),
  "amount is invalid");
  check(amount.symbol == symbol("WAX", 8),
  "amount must be in WAX");
  
  auto payout_itr = payoutsTable.find(from.value);
  check(payout_itr != payoutsTable.end(),
  "the account has no outstanding payouts");
  
  check(payout_itr->outstanding_payout <= amount,
  "the account doesn't have that many outstanding payouts");
  
  if (payout_itr->outstanding_payout == amount) {
    payoutsTable.erase(payout_itr);
  } else {
    payoutsTable.modify(payout_itr, _self, [&](auto& p) {
      p.outstanding_payout -= amount;
    });
  }
  
  action(
    permission_level{_self, "active"_n},
    "eosio.token"_n,
    "transfer"_n,
    std::make_tuple(_self, from, amount, std::string("bet payout"))
  ).send();
  
}




ACTION pinkbankroll::withdraw(name from, uint64_t weight_to_withdraw) {
  require_auth(from);
  
  auto investor_itr = investorsTable.find(from.value);
  check(investor_itr != investorsTable.end(),
  "the account doesn't have anything invested");
  
  check(weight_to_withdraw <= investor_itr->bankroll_weight,
  "the account doesn't have that much bankroll_weight");
  
  statsStruct stats = statsTable.get();
  uint64_t amount_to_withdraw = (uint64_t)((double)stats.bankroll.amount * (double)weight_to_withdraw) / (double)stats.total_bankroll_weight;
  asset wax_to_withdraw = asset(amount_to_withdraw, symbol("WAX", 8));
  
  if (investor_itr->bankroll_weight == weight_to_withdraw) {
    investorsTable.erase(investor_itr);
  } else {
    investorsTable.modify(investor_itr, _self, [&](auto& i) {
      i.bankroll_weight -= weight_to_withdraw;
    });
  }
  
  stats.total_bankroll_weight -= weight_to_withdraw;
  stats.bankroll -= wax_to_withdraw;
  statsTable.set(stats, _self);
  
  action(
    permission_level{_self, "active"_n},
    "eosio.token"_n,
    "transfer"_n,
    std::make_tuple(_self, from, wax_to_withdraw, std::string("bankroll withdraw"))
  ).send();
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logwithdraw"_n,
    std::make_tuple(from, weight_to_withdraw, wax_to_withdraw)
  ).send();
}




ACTION pinkbankroll::receiverand(uint64_t assoc_id, checksum256 random_value) {
  require_auth("orng.wax"_n);
  const auto random_array = random_value.get_array();
  //The first 128 bits of the random_value. The rest is not needed
  uint128_t random_number = random_array[0];
  
  auto rolls_itr = rollsTable.find(assoc_id);
  check(rolls_itr != rollsTable.end(),
  "no bet with this id exists");
  check(rolls_itr->paid,
  "this roll has not been paid for yet");
  
  uint32_t result = (random_number % rolls_itr->upper_range_limit) + 1;
  print("result: ", result);
  
  rollBets_t betsTable(_self, assoc_id);
  
  asset totalPaidOut = asset(0, symbol("WAX", 8));
  
  auto bet_itr = betsTable.begin();
  while(bet_itr != betsTable.end()) {
    if (bet_itr->lower_bound <= result && result <= bet_itr->upper_bound) {
      //This bet won
      asset amount_won = bet_itr->amount * bet_itr->muliplier / 1000;
      totalPaidOut += amount_won;
      
      //Updating payouts table
      auto payouts_itr = payoutsTable.find(bet_itr->bettor.value);
      if (payouts_itr != payoutsTable.end()) {
        payoutsTable.modify(payouts_itr, _self, [&](auto& p) {
          p.outstanding_payout += amount_won;
        });
      } else {
        payoutsTable.emplace(_self, [&](auto& p){
          p.bettor = bet_itr->bettor;
          p.outstanding_payout = amount_won;
        });
      }
      
      eosio::transaction t;
      t.actions.emplace_back(
        permission_level(_self, "active"_n),
        _self,
        "payoutbet"_n,
        std::make_tuple(bet_itr->bettor, amount_won)
      );
      
      uint64_t deferred_id = (assoc_id << 8) + bet_itr->bet_id;
      t.send(deferred_id, _self);
    }
    
    //Removing bet table entry
    //erase returns iterator poiting to next entry
    bet_itr = betsTable.erase(bet_itr);
  }
  
  //Removing roll table entry
  rollsTable.erase(rolls_itr);
  
  statsStruct stats = statsTable.get();
  stats.bankroll -= totalPaidOut;
  statsTable.set(stats, _self);
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "loggetrand"_n,
    std::make_tuple(assoc_id, result, totalPaidOut, random_value)
  ).send();
}




void pinkbankroll::receivetransfer(name from, name to, asset quantity, std::string memo) {
  if (to != _self) {
    return;
  }
  
  check(quantity.symbol == symbol("WAX", 8),
  "amount must be in WAX");
  
  if (memo.compare("deposit") == 0) {
    
    statsStruct stats = statsTable.get();
    
    uint64_t added_bankroll_weight;
    if (stats.bankroll.amount == 0) {
      added_bankroll_weight = 1000000;
    } else {
      added_bankroll_weight = (int) ((double)quantity.amount / (double)stats.bankroll.amount * (double)stats.total_bankroll_weight);
    }
    
    stats.bankroll += quantity;
    stats.total_bankroll_weight += added_bankroll_weight;
    statsTable.set(stats, _self);
    
    
    auto investor_itr = investorsTable.find(from.value);
    if (investor_itr != investorsTable.end()) {
      investorsTable.modify(investor_itr, _self, [&](auto& i) {
        i.bankroll_weight += added_bankroll_weight;
      });
    } else {
      investorsTable.emplace(_self, [&](auto& i){
        i.investor = from;
        i.bankroll_weight = added_bankroll_weight;
      });
    }
    
  
  } else if (memo.rfind("startroll ") == 0) {
    
    int64_t firstWhitespace = memo.rfind(" ");
    std::string idstring = memo.substr(firstWhitespace);
    uint64_t parsed_creator_id = std::strtoull(idstring.c_str(), 0, 10);
    
    uint128_t creator_and_id = uint128_t{from.value} << 64 | parsed_creator_id;
    auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
    auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
    
    check(itr_creator_and_id != rolls_by_creator_and_id.end(),
    "No bet with the specified creator_id has been announced");
    
    check(quantity == itr_creator_and_id->total_amount,
    "quantity needs to be equal to the total_amount of the roll");
    
    uint32_t max_range = itr_creator_and_id->upper_range_limit;
    
    ChainedRange firstRange = ChainedRange(1, max_range, 0);
    rollBets_t betsTable(_self, itr_creator_and_id->roll_id);
    
    uint64_t signing_value = itr_creator_and_id->roll_id;
    
    asset totalBetsCollected = asset(0, symbol("WAX", 8));
    asset totalRake = asset(0, symbol("WAX", 8));
    asset totalDevFee = asset(0, symbol("WAX", 8));
    
    for (auto it = betsTable.begin(); it != betsTable.end(); it++) {
      double ev = (double)it->muliplier / (double)1000 * (double)(it->upper_bound - it->lower_bound + 1) / (double)max_range;
      double edge = (double)1 - ev;
      totalBetsCollected.amount += (int)((double)it->amount.amount * ((double)1.007 - edge));
      totalRake.amount += (int)((double)it->amount.amount * (edge - (double)0.01));
      totalDevFee.amount += (int)((double)it->amount.amount * (double)0.003);
      
      uint64_t payout = it->amount.amount * it->muliplier / 1000;
      
      firstRange.insertBet(it->lower_bound, it->upper_bound, payout);
      
      signing_value = signing_value xor it->random_seed;
    }
    
    statsStruct stats = statsTable.get();
    asset required_bankroll = getRequiredBankroll(firstRange, totalBetsCollected.amount, max_range);
    check(stats.bankroll >= required_bankroll,
    "the current bankroll is too small to accept this roll");
    
    rolls_by_creator_and_id.modify(itr_creator_and_id, _self, [&](auto &r) {
      r.paid = true;
    });
    
    stats.bankroll -= totalRake;
    stats.bankroll -= totalDevFee;
    statsTable.set(stats, _self);
    
    
    action(
      permission_level{_self, "active"_n},
      "orng.wax"_n,
      "requestrand"_n,
      std::make_tuple(itr_creator_and_id->roll_id, signing_value, _self)
    ).send();
  
    action(
      permission_level{_self, "active"_n},
      "eosio.token"_n,
      "transfer"_n,
      std::make_tuple(_self, itr_creator_and_id->rake_recipient, totalRake, std::string("pinkbankroll rake"))
    ).send();
    
    //TODO Insert real dev account
    action(
      permission_level{_self, "active"_n},
      "eosio.token"_n,
      "transfer"_n,
      std::make_tuple(_self, "eosio"_n, totalDevFee, std::string("pinkbankroll devfee"))
    ).send();
    
    action(
    permission_level{_self, "active"_n},
    _self,
    "logstartroll"_n,
    std::make_tuple(itr_creator_and_id->roll_id, from, parsed_creator_id)
  ).send();
    
    
  } else {
    check(false, "invalid memo");
  }
}


//Only for external logging
  
ACTION pinkbankroll::logannounce(uint64_t roll_id, name creator, uint64_t creator_id, uint32_t upper_range_limit, name rake_recipient) {
  require_auth(_self);
}

ACTION pinkbankroll::logbet(uint64_t roll_id, uint64_t bet_id, name creator, uint64_t creator_id, name bettor, asset amount, uint32_t lower_bound, uint32_t upper_bound, uint32_t muliplier, uint64_t random_seed) {
  require_auth(_self);
}

ACTION pinkbankroll::logstartroll(uint64_t roll_id, name creator, uint64_t creator_id) {
  require_auth(_self);
}

ACTION pinkbankroll::loggetrand(uint64_t roll_id, uint32_t result, asset paid_out, checksum256 random_value) {
  require_auth(_self);
}

ACTION pinkbankroll::logwithdraw(name investor, uint64_t weight_to_withdraw, asset amount) {
  require_auth(_self);
}