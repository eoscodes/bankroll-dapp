#include <pinkbankroll.hpp>
#include <bankrollmanagement.hpp>

static constexpr symbol CORE_SYMBOL = symbol("WAX", 8);
static constexpr symbol PINK_SYMBOL = symbol("PINK", 4);

//Only needs to be called once after contract creation
ACTION pinkbankroll::init() {
  require_auth(_self);
  statsTable.get_or_create(_self, statsStruct{});
}




/**
 * This is the first action to call when creating a new roll. The initial parameters are set and stored in a table entry.
 * 
 * @param creator - The name of the creator of this roll. Only this account will be able to add bets and start the roll
 * @param creator_id - A unique id that the creator uses to identify this roll. This is different from the internal roll_id
 * @param max_result - The roll will produce a result 1 <= result <= max_result that can be bet Only
 * @param rake_recipient - The name of the account that will receive the rake payment for this roll
 */
ACTION pinkbankroll::announceroll(name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient) {
  check(!isPaused(),
  "the contract is paused, only withdrawals and payouts are currently allowed");
  
  require_auth(creator);
  
  check(max_result != 0,
  "max result can't be 0");
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id == rolls_by_creator_and_id.end(),
  "can't create a roll with a creator_id that is already in use");
  
  //available_primary_key can't be used, because finished rolls are deleted from the table
  statsStruct stats = statsTable.get();
  uint64_t roll_id = stats.current_roll_id++;
  statsTable.set(stats, _self);
  
  rollsTable.emplace(creator, [&](rollStruct &r) {
    r.roll_id = roll_id;
    r.creator = creator;
    r.creator_id = creator_id;
    r.max_result = max_result;
    r.rake_recipient = rake_recipient;
  });
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logannounce"_n,
    std::make_tuple(roll_id, creator, creator_id, max_result, rake_recipient)
  ).send();
}




/**
 * Adds a bet to an existing roll. The bet is not paid for immediately, instead all bets are paid for at once when starting the roll.
 * 
 * @param creator - The name of the creator of the roll to add this bet to. Only the creator can add bets to his own rolls
 * @param creator_id - The unique id of the roll that the creator specified in the announceroll action
 * @param bettor - The name of the bettor. This account will receive the payout if this bet wins
 * @param quantity - The quantity of WAX to be wagered
 * @param lower_bound - The lower bound of the range to bet on
 * @param upper_bound - The upper bound of the range to bet on
 * @param multiplier - The multiplier of this bet x 1000 (multiplier = 2000 -> 2x payout)
 * @param random_seed - The random_seed that will be included in the seed that will later be sent to the rng oracle.
 *                      Users are highly encouraged to send actual (pseudo) random values here to avoid later collisions in the rng oracle
 */
ACTION pinkbankroll::announcebet(name creator, uint64_t creator_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed) {
  check(!isPaused(),
  "the contract is paused, only withdrawals and payouts are currently allowed");
  
  require_auth(creator);
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id != rolls_by_creator_and_id.end(),
  "No bet with the specified creator_id has been announced");
  
  check(!itr_creator_and_id->paid,
  "the roll has already been paid for");
  
  check(quantity.is_valid(),
  "quantity is invalid");
  check(quantity.symbol == CORE_SYMBOL,
  "quantity must be in WAX");
  
  check(lower_bound >= 1,
  "lower_bound needs to be at least 1");
  check(lower_bound <= upper_bound,
  "lower_bound can't be greater than upper_bound");
  check(upper_bound <= itr_creator_and_id->max_result,
  "upper_bound can't be greater than the max_result of the roll");
  
  check(multiplier > 1000,
  "the multiplier has to be greater than 1000 (greater than 1x)");
  
  
  double odds = (double)(upper_bound - lower_bound + 1) / (double)(itr_creator_and_id->max_result);
  check (odds >= 0.005,
  "the odds cant be smaller than 0.005");
  double ev = odds * multiplier / 1000.0;
  check(ev <= 0.99,
  "the bet cant have an EV greater than 0.99 * quantity");
  
  uint64_t roll_id = itr_creator_and_id->roll_id;
  
  rollBets_t betsTable(_self, roll_id);
  uint64_t bet_id = betsTable.available_primary_key();
  betsTable.emplace(creator, [&](betStruct &b) {
    b.bet_id = bet_id;
    b.bettor = bettor;
    b.quantity = quantity;
    b.lower_bound = lower_bound;
    b.upper_bound = upper_bound;
    b.multiplier = multiplier;
    b.random_seed = random_seed;
  });
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logbet"_n,
    std::make_tuple(roll_id, bet_id, bettor, quantity, lower_bound, upper_bound, multiplier, random_seed)
  ).send();
}




/**
 * Pays out a winning bet. Usually, this is called by an automatic deferred action when the result of a roll is handled.
 * However, since this deferred action could theoretically fail, users are also able to withdraw their outstanding bets manually
 * 
 * @param from - The account name to payout a bet to
 * @param quantity - The amount of WAX to payout
 */
ACTION pinkbankroll::payoutbet(name from, asset quantity) {
  
  check(quantity.is_valid(),
  "quantity is invalid");
  check(quantity.symbol == CORE_SYMBOL,
  "quantity must be in WAX");
  
  auto payout_itr = payoutsTable.find(from.value);
  check(payout_itr != payoutsTable.end(),
  "the account has no outstanding payouts");
  
  check(payout_itr->outstanding_payout >= quantity,
  "the account doesn't have that many outstanding payouts");
  
  if (payout_itr->outstanding_payout == quantity) {
    payoutsTable.erase(payout_itr);
  } else {
    payoutsTable.modify(payout_itr, _self, [&](auto& p) {
      p.outstanding_payout -= quantity;
    });
  }
  
  //The bankroll stats do not have to be decreased, because that has already happend
  //when the payout has been added to the payouts table
  action(
    permission_level{_self, "active"_n},
    "eosio.token"_n,
    "transfer"_n,
    std::make_tuple(_self, from, quantity, std::string("bet payout"))
  ).send();
  
}




/**
 * @dev Can be called by the dev account to pause/ unpause the contract.
 * This should hopefully never have to be used, but acts as an emergency stop if it is ever needed
 * Withdrawals and Payouts are still enabled when paused, but no new rolls/ bets will be accepted
 * 
 * @param paused - The bool value to pause/ unpause the contract
 */
ACTION pinkbankroll::setpaused(bool paused) {
  require_auth("pinknetworkx"_n);
  statsStruct stats = statsTable.get();
  stats.paused = paused;
  statsTable.set(stats, _self);
}




/**
 * This is called by the RNG oracle, providing the randomness for calculating the result
 * Note: In the case there are a lot (>100) bets that are all being paid out, this action could theoretically take more than 30ms to execute
 *       If that is the case, the transaction will fail. Depending on the functionality of the oracle, the transaction might be resent.
 *       It is the responsibility of the 3rd party dev using the pinkbankroll contract to ensure that this doesn't happen by enforcing an appropiate max bet limit
 * 
 * @param assoc_id - The assoc_id that was provided to the oracle when requesting the random value. Equal to the roll_id
 * @param random_value - The sha256 hash of the random seed that was provided when starting the roll. Is used as randomness
 */
ACTION pinkbankroll::receiverand(uint64_t assoc_id, checksum256 random_value) {
  require_auth("orng.wax"_n);
  
  auto rolls_itr = rollsTable.find(assoc_id);
  check(rolls_itr != rollsTable.end(),
  "no bet with this id exists");
  check(rolls_itr->paid,
  "this roll has not been paid for yet");
  
  name roll_creator = rolls_itr->creator;
  uint64_t roll_creator_id = rolls_itr->creator_id;
  
  
  const auto random_array = random_value.get_array();
  //The first 128 bits of the random_value. The rest is not needed
  uint128_t random_number = random_array[0];
  
  uint32_t result = (random_number % rolls_itr->max_result) + 1;
  print("Result: ", result, " / ", rolls_itr->max_result);
  
  rollBets_t betsTable(_self, assoc_id);
  
  asset total_rake = asset(0, CORE_SYMBOL);
  asset total_dev_fee = asset(0, CORE_SYMBOL);
  asset bankroll_change = asset(0, CORE_SYMBOL); //Disregarding rake/ fee
  
  auto bet_itr = betsTable.begin();
  while(bet_itr != betsTable.end()) {
    //Calculating the rake/ fee to payouts
    double ev = (double)bet_itr->multiplier / 1000.0 * (double)(bet_itr->upper_bound - bet_itr->lower_bound + 1) / (double)rolls_itr->max_result;
    double edge = 1.0 - ev;
    total_rake.amount += (int64_t)((double)bet_itr->quantity.amount * (edge - 0.01));
    total_dev_fee.amount += (int64_t)((double)bet_itr->quantity.amount * 0.003);
    
    //Calculating the bet outcome
    bankroll_change += bet_itr->quantity;
    
    if (bet_itr->lower_bound <= result && result <= bet_itr->upper_bound) {
      //This bet won
      asset quantity_won = bet_itr->quantity * bet_itr->multiplier / 1000;
      bankroll_change -= quantity_won;
      
      //Updating payouts table
      auto payouts_itr = payoutsTable.find(bet_itr->bettor.value);
      if (payouts_itr != payoutsTable.end()) {
        payoutsTable.modify(payouts_itr, _self, [&](auto& p) {
          p.outstanding_payout += quantity_won;
        });
      } else {
        payoutsTable.emplace(_self, [&](auto& p){
          p.bettor = bet_itr->bettor;
          p.outstanding_payout = quantity_won;
        });
      }
      
      //Deferred transactions have to be used in order to guarantee that no single bet can make the whole payout throw
      //They are however not 100% guaranteed to go through. Therefore, users can also manually withdraw their bets with the payoutbet action
      
      eosio::transaction t;
      t.actions.emplace_back(
        permission_level(_self, "active"_n),
        _self,
        "payoutbet"_n,
        std::make_tuple(bet_itr->bettor, quantity_won)
      );
      
      uint64_t deferred_id = (assoc_id << 16) + bet_itr->bet_id;
      t.send(deferred_id, _self);
      
    }
    
    
    //Removing bet table entry
    //erase returns iterator poiting to next entry
    bet_itr = betsTable.erase(bet_itr);
  }
  
  statsStruct stats = statsTable.get();
  stats.bankroll += bankroll_change;
  statsTable.set(stats, _self);
  
  //Removing roll table entry
  rollsTable.erase(rolls_itr);
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logbrchange"_n,
    std::make_tuple(bankroll_change, std::string("roll result"), stats.bankroll)
  ).send();
  
  transferFromBankroll(rolls_itr->rake_recipient, total_rake, std::string("pinkbankroll rake"));
  transferFromBankroll("pinknetworkx"_n, total_dev_fee, std::string("pinkbankroll devfee"));
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "loggetrand"_n,
    std::make_tuple(assoc_id, result, bankroll_change - total_rake - total_dev_fee, stats.bankroll, random_value)
  ).send();
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "notifyresult"_n,
    std::make_tuple(roll_creator, roll_creator_id, result)
  ).send();
}




/**
 * This is called whenever there is a eosio.token transfer involving pinkbankroll as either sender or recipient
 * It is used to handle deposits to the bankroll, as well as to start rolls_itr
 * 
 * @param from - The account name that sent the transfer
 * @param to - The account name that receives the transfer
 * @param quantity - The quantity of tokens sent. This could theoretically be something else than WAX, therefore has to be checked
 * @param memo - A string of up to 256 characers, used to identify what this transaction is meant for
 */

void pinkbankroll::receivewaxtransfer(name from, name to, asset quantity, std::string memo) {
  if (to != _self) {
    return;
  }
  check(quantity.symbol == CORE_SYMBOL,
  "quantity must be in WAX");
  
  if (memo.compare("deposit") == 0) {
    handleDeposit(from, quantity);
    
  } else if (memo.find("startroll ") == 0) {
    int64_t firstWhitespace = memo.find(" ");
    std::string idstring = memo.substr(firstWhitespace);
    uint64_t parsed_creator_id = std::strtoull(idstring.c_str(), 0, 10);
    
    handleStartRoll(from, parsed_creator_id, quantity);
    
  } else {
    check(false, "invalid memo");
  }
}




/**
 * This is called whenever there is a token.pink transfer involving pinkbankroll as either sender or recipient
 * When PINK is sent to the pinkbankroll account, this is interpreted as a withdrawal
 * The PINK tokens get burned and the sender will get the corresponding part of the current bankroll in WAX
 * 
 * Note: It is checked if there are any active rolls (already paid and waiting for oracle callback) that would not have been accepted if this
 *       withdrawal had gone through before the roll. This is to prevent attackers from first depositing to increase the max bet, then betting this max bet,
 *       and then withdrawing before the bet goes though.
 *       In practice, this should only happen very rarely.
 * 
 * @param from - The account name that sent the transfer
 * @param to - The account name that receives the transfer
 * @param quantity - The quantity of tokens sent. This could theoretically be something else than PINK, therefore has to be checked
 * @param memo - A string of up to 256 characers, used to identify what this transaction is meant for
 */

void pinkbankroll::receivepinktransfer(name from, name to, asset quantity, std::string memo) {
  if (to != _self) {
    return;
  }
  check(quantity.symbol == PINK_SYMBOL,
  "quantity must be in PINK");
  
  statsStruct stats = statsTable.get();
  uint64_t amount_to_withdraw = (uint64_t)((double)stats.bankroll.amount * (double)quantity.amount / (double)get_supply("token.pink"_n, PINK_SYMBOL.code()).amount);
  asset wax_to_withdraw = asset(amount_to_withdraw, CORE_SYMBOL);
  
  auto rolls_by_paid = rollsTable.get_index<"haspaid"_n>();
  
  
  // This goes through all rolls that are already paid and waiting for the oracle callback
  // If any of those rolls need the WAX that would be withdrawn to stay within the bankroll management, this withdrawal will fail
  // This means that the PINK transfer will fail as well, so no funds would be lost
  for (auto roll_itr = rolls_by_paid.find(1); roll_itr != rolls_by_paid.end(); roll_itr++) {
    
    asset total_bets_collected = asset(0, CORE_SYMBOL);  // = total_quantity_bet - (rake + fees)
    
    uint32_t max_range = roll_itr->max_result;
    ChainedRange firstRange = ChainedRange(1, max_range, 0);
    rollBets_t betsTable(_self, roll_itr->roll_id);
    
    for (auto bet_itr = betsTable.begin(); bet_itr != betsTable.end(); bet_itr++) {
      double ev = (double)bet_itr->multiplier / 1000.0 * (double)(bet_itr->upper_bound - bet_itr->lower_bound + 1) / (double)max_range;
      total_bets_collected.amount += (int64_t)((double)bet_itr->quantity.amount * (ev + 0.007));
      
      uint64_t payout = bet_itr->quantity.amount * bet_itr->multiplier / 1000;
      firstRange.insertBet(bet_itr->lower_bound, bet_itr->upper_bound, payout);
    }
    
    asset required_bankroll = getRequiredBankroll(firstRange, total_bets_collected.amount, max_range);
    check(required_bankroll < stats.bankroll - wax_to_withdraw,
    "Can't withdraw because there currently is an active roll that is too big to run without this amount. Try again a second from now.");
  }
  
  
  action(
    permission_level{_self, "active"_n},
    "token.pink"_n,
    "retire"_n,
    std::make_tuple(quantity, std::string("bankroll withdrawal token burn"))
  ).send();
  
  transferFromBankroll(from, wax_to_withdraw, std::string("bankroll withdraw"));
}




/**
 * Private helper function to handle withdraws from the bankroll
 * @param recipient - The name of the account to receive the payment
 * @param quantity - The amount of WAX to send
 * @param memo - The memo to send with the transfer
 */
void pinkbankroll::transferFromBankroll(name recipient, asset quantity, std::string memo) {
  //The eosio.token contract will throw on zero amount transfers
  if (quantity.amount == 0) {
    return;
  }
  statsStruct stats = statsTable.get();
  stats.bankroll -= quantity;
  statsTable.set(stats, _self);
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logbrchange"_n,
    std::make_tuple(-quantity, memo, stats.bankroll)
  ).send();
  
  action(
    permission_level{_self, "active"_n},
    "eosio.token"_n,
    "transfer"_n,
    std::make_tuple(_self, recipient, quantity, memo)
  ).send();
}




/**
 * Private function to handle deposits (as parsed from the receivewaxtransfer action)
 * 
 * @param investor - The account name that sent the deposit
 * @param quantity - The amount of WAX to be invested
 * 
 * The bankroll ownership is handled by the PINK token in the token.pink contract
 * If a user owns 10% of the supply of PINK tokens, that means that he is entitled to 10% of the bankroll
 * When a new deposit is made, instead of inefficiently changing the balance of each token holder, new tokens are issued
 */
void pinkbankroll::handleDeposit(name investor, asset quantity) {
  check(!isPaused(),
  "the contract is paused, only withdrawals and payouts are currently allowed");
  
  statsStruct stats = statsTable.get();
  
  uint64_t added_pink_amount;
  if (stats.bankroll.amount == 0) {
    added_pink_amount = quantity.amount / 1000; //WAX has 8 digits, PINK has 4 digits. Therefore deviding by 1000 means that the pink quantity will be 10x the wax quantity
  } else {
    added_pink_amount = (uint64_t) ((double)quantity.amount / (double)stats.bankroll.amount * (double)get_supply("token.pink"_n, PINK_SYMBOL.code()).amount);
  }
  check(added_pink_amount > 0,
  "The deposit is so small that it would equate to 0 PINK");
  asset added_pink_quantity = asset(added_pink_amount, PINK_SYMBOL);
  
  stats.bankroll += quantity;
  statsTable.set(stats, _self);
  
  action(
    permission_level{_self, "active"_n},
    "token.pink"_n,
    "issue"_n,
    std::make_tuple(_self, added_pink_quantity, std::string("token issue for deposit"))
  ).send();
  
  action(
    permission_level{_self, "active"_n},
    "token.pink"_n,
    "transfer"_n,
    std::make_tuple(_self, investor, added_pink_quantity, std::string("token issue for deposit"))
  ).send();
  
  action(
    permission_level{_self, "active"_n},
    _self,
    "logbrchange"_n,
    std::make_tuple(quantity, std::string("bankroll deposit"), stats.bankroll)
  ).send();
}




/**
 * Private function to handle starting a roll (as parsed from the receivewaxtransfer action)
 * Note: This has a worst case runtime of O(bets^2) and could theoretically take more than 30ms if the roll has a lot of different bets
 *       If this happens, the transaction will fail. This means that the WAX transfer will also fail, so no funds will be lost
 * 
 * @param creator - The acccount name of the creator of the roll, and also the account that sends the transfer
 * @param creator_id - The creator id of the roll to start, as parsed from the transfer memo
 * @param quantity - The amount of WAX that was sent with this transaction. Needs to be equal to the total quantity bet in this roll
 */
void pinkbankroll::handleStartRoll(name creator, uint64_t creator_id, asset quantity) {
  check(!isPaused(),
  "the contract is paused, only withdrawals and payouts are currently allowed");
  
  uint128_t creator_and_id = uint128_t{creator.value} << 64 | creator_id;
  auto rolls_by_creator_and_id = rollsTable.get_index<"creatorandid"_n>();
  auto itr_creator_and_id = rolls_by_creator_and_id.find(creator_and_id);
  
  check(itr_creator_and_id != rolls_by_creator_and_id.end(),
  "no bet with the specified creator_id has been announced");
  check(!itr_creator_and_id->paid,
  "the roll has already been paid for");
  
  asset total_quantity_bet = asset(0, CORE_SYMBOL);
  asset total_bets_collected = asset(0, CORE_SYMBOL);  // = total_quantity_bet - (rake + fees)
  
  uint32_t max_range = itr_creator_and_id->max_result;
  ChainedRange firstRange = ChainedRange(1, max_range, 0);
  rollBets_t betsTable(_self, itr_creator_and_id->roll_id);
  
  uint64_t signing_value = 0;
  uint64_t signing_xor = 0;
  uint64_t bet_number = 0;
  
  for (auto it = betsTable.begin(); it != betsTable.end(); it++) {
    total_quantity_bet += it->quantity;
    double ev = (double)it->multiplier / 1000.0 * (double)(it->upper_bound - it->lower_bound + 1) / (double)max_range;
    total_bets_collected.amount += (int64_t)((double)it->quantity.amount * (ev + 0.007));
    
    uint64_t payout = it->quantity.amount * it->multiplier / 1000;
    firstRange.insertBet(it->lower_bound, it->upper_bound, payout);
    
    //For up to the first 32 bits, the n'th bit of the signing_value will be the first bit of the n'th bet's random seed
    //This prevents an attacker being able to change the signing_value to anything he wants by sending the last bet, by having some bits that are not possible to change
    if (bet_number < 32) {
      signing_value += (it->random_seed & 0x8000000000000000) >> bet_number;
      bet_number++;
    };
    
    signing_xor = signing_xor ^ it->random_seed;
  }
  
  //The remaining bits will be the xor of all random seeds
  signing_value += (signing_xor >> bet_number);
  //To further prevent collisions, the previous signing value is shifted 16 bits to the right
  //And the first 16 bits of the signing_value are the last 16 bits of the roll id
  signing_value = (itr_creator_and_id->roll_id << 48) + (signing_value >> 16);
  
  check(quantity == total_quantity_bet,
  "quantity needs to be equal to the total quantity bet of the roll");
  
  asset required_bankroll = getRequiredBankroll(firstRange, total_bets_collected.amount, max_range);
  statsStruct stats = statsTable.get();
  check(stats.bankroll >= required_bankroll,
  "the current bankroll is too small to accept this roll");
  
  
  //Check if the signing_value was already used.
  //If that is the case, increment the signing_value until a non-used value is found
  while (signvals_table.find(signing_value) != signvals_table.end()) {
    signing_value += 1;
  }
  
  
  
  rolls_by_creator_and_id.modify(itr_creator_and_id, _self, [&](auto &r) {
    r.paid = true;
  });
  
  action(
    permission_level{_self, "active"_n},
    "orng.wax"_n,
    "requestrand"_n,
    std::make_tuple(itr_creator_and_id->roll_id, signing_value, _self)
  ).send();
  
  action(
  permission_level{_self, "active"_n},
  _self,
  "logstartroll"_n,
  std::make_tuple(itr_creator_and_id->roll_id, creator, creator_id)
  ).send();
}




bool pinkbankroll::isPaused() {
  statsStruct stats = statsTable.get();
  return stats.paused;
}



/**
 * This action is used to notify the creator of a roll of the result.
 * require_recipient is used instead of an inline action, in order not to allow the called contract to use the bankroll's RAM
 */
ACTION pinkbankroll::notifyresult(name creator, uint64_t creator_id, uint32_t result) {
  require_auth(_self);
  require_recipient(creator);
}



//Only for external logging
  
ACTION pinkbankroll::logannounce(uint64_t roll_id, name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient) {
  require_auth(_self);
}

ACTION pinkbankroll::logbet(uint64_t roll_id, uint64_t bet_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed) {
  require_auth(_self);
}

ACTION pinkbankroll::logstartroll(uint64_t roll_id, name creator, uint64_t creator_id) {
  require_auth(_self);
}

ACTION pinkbankroll::loggetrand(uint64_t roll_id, uint32_t result, asset bankroll_change, asset new_bankroll, checksum256 random_value) {
  require_auth(_self);
}

ACTION pinkbankroll::logbrchange(asset change, std::string message, asset new_bankroll) {
  require_auth(_self);
}
