#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/transaction.hpp>

using namespace eosio;

CONTRACT pinkbankroll : public contract {
  public:
    using contract::contract;
    pinkbankroll(name receiver, name code, datastream<const char*> ds):contract(receiver, code, ds),
    rollsTable(receiver, receiver.value),
    investorsTable(receiver, receiver.value),
    payoutsTable(receiver, receiver.value),
    statsTable(receiver, receiver.value)
    {}
    
    ACTION init();
    ACTION announceroll(name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient);
    ACTION announcebet(name creator, uint64_t creator_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed);
    ACTION payoutbet(name from, asset quantity, uint64_t irrelevant);
    ACTION withdraw(name from, uint64_t weight_to_withdraw);
    ACTION setpaused(bool paused);
    
    ACTION receiverand(uint64_t assoc_id, checksum256 random_value);
    [[eosio::on_notify("eosio.token::transfer")]] void receivetransfer(name from, name to, asset quantity, std::string memo);
    
    ACTION notifyresult(name creator, uint64_t creator_id, uint32_t result);
  
    ACTION logannounce(uint64_t roll_id, name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient);
    ACTION logbet(uint64_t roll_id, uint64_t bet_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed);
    ACTION logstartroll(uint64_t roll_id, name creator, uint64_t creator_id);
    ACTION loggetrand(uint64_t roll_id, uint32_t result, asset bankroll_change, checksum256 random_value);
    //Bankroll increase/ decrease
    ACTION logbrchange(asset change, std::string message, asset new_bankroll);
  
  private:
    
    TABLE rollStruct {
      uint64_t roll_id;
      name creator;
      uint64_t creator_id;
      uint32_t max_result;
      name rake_recipient;
      bool paid;
      
      uint64_t primary_key() const { return roll_id; }
      uint128_t get_creator_and_id() const { return uint128_t{creator.value} << 64 | creator_id; }
    };
    typedef multi_index<
    "rolls"_n,
    rollStruct,
    indexed_by<"creatorandid"_n, const_mem_fun<rollStruct, uint128_t, &rollStruct::get_creator_and_id>>>
    rolls_t;

    
    TABLE betStruct {
      uint64_t bet_id;
      name bettor;
      asset quantity;
      uint32_t lower_bound;
      uint32_t upper_bound;
      uint32_t multiplier;
      uint64_t random_seed;
      
      uint64_t primary_key() const { return bet_id; }
    };
    typedef multi_index<"rollbets"_n, betStruct> rollBets_t;
    
    
    TABLE investorStruct {
      name investor;
      uint64_t bankroll_weight;
      
      uint64_t primary_key() const { return investor.value; }
    };
    typedef multi_index<"investors"_n, investorStruct> investors_t;
    
    
    TABLE payoutStruct {
      name bettor;
      asset outstanding_payout;
      
      uint64_t primary_key() const { return bettor.value; }
    };
    typedef multi_index<"payouts"_n, payoutStruct> payouts_t;
    
    
    TABLE statsStruct {
      asset bankroll = asset(0, symbol("WAX", 8));
      uint64_t total_bankroll_weight = 0;
      uint64_t current_roll_id = 0;
      bool paused = false;
    };
    typedef singleton<"stats"_n, statsStruct> stats_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index<"stats"_n, statsStruct> stats_t_for_abi;
    
    
    TABLE rngUsedSeedStruct {
      uint64_t seed;
      
      uint64_t primary_key() const { return seed; }
    };
    typedef multi_index<"usedseeds"_n, rngUsedSeedStruct> rng_usedseeds_t;
    
    
    rolls_t rollsTable;
    investors_t investorsTable;
    payouts_t payoutsTable;
    stats_t statsTable;
  
    void transferFromBankroll(name recipient, asset quantity, std::string memo);
    void handleDeposit(name investor, asset quantity);
    void handleStartRoll(name creator, uint64_t creator_id, asset quantity);
    bool isPaused();
};
