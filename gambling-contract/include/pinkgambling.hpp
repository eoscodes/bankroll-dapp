#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>

using namespace eosio;

CONTRACT pinkgambling : public contract {
  public:
    using contract::contract;
    pinkgambling(name receiver, name code, datastream<const char*> ds):contract(receiver, code, ds),
    rollsTable(receiver, receiver.value),
    statsTable(receiver, receiver.value)
    {}
    
    ACTION init();
    
    ACTION startroll(uint64_t roll_id);
    
    [[eosio::on_notify("eosio.token::transfer")]] void receivetransfer(name from, name to, asset quantity, std::string memo);
    [[eosio::on_notify("pinkbankroll::notifyresult")]] void receivenotifyresult(name creator, uint64_t creator_id, uint32_t result);
  
    ACTION logbet(uint64_t roll_id, uint64_t cycle_number, uint64_t bet_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t client_seed);
    ACTION logresult(uint64_t roll_id, uint64_t cycle_number, uint32_t max_result, name rake_recipient, uint32_t roll_result, uint64_t identifier, uint32_t cycle_time);
  
  private:
    
    TABLE rollStruct {
      uint64_t roll_id;
      uint32_t max_result;
      name rake_recipient;
      bool waiting_for_result;
      uint64_t identifier;    //only used for single bets, not for cycles
      uint64_t cycle_number;  //0 when roll is not cyclic
      time_point last_cycle;  //0 when roll is not cyclic
      time_point last_player_joined; //0 when roll is not cyclic
      uint32_t cycle_time;    //0 when roll is not cyclic
      
      uint64_t primary_key() const { return roll_id; }
    };
    typedef multi_index<"rolls"_n, rollStruct> rolls_t;

    
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
    
    
    TABLE statsStruct {
      uint64_t current_roll_id = 0;
    };
    typedef singleton<"stats"_n, statsStruct> stats_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index<"stats"_n, statsStruct> stats_t_for_abi;
    
    
    //This is needed to get the current bankroll of the bankroll contract
    struct bankrollStatsStruct {
      asset bankroll = asset(0, symbol("WAX", 8));
      uint64_t total_bankroll_weight = 0;
      uint64_t current_roll_id = 0;
    };
    typedef singleton<"stats"_n, bankrollStatsStruct> bankroll_stats_t;
    
    
    rolls_t rollsTable;
    stats_t statsTable;
  
    void createCycle(uint32_t max_result, name rake_recipient, uint32_t cycle_time);
    void quickBet(asset quantity, name bettor, uint32_t multiplier, uint32_t lower_bound, uint32_t upper_bound, name rake_recipient, uint64_t identifier, uint64_t random_seed);
    void addBet(asset quantity, uint64_t roll_id, name bettor, uint32_t multiplier, uint32_t lower_bound, uint32_t upper_bound, uint64_t random_seed);
    void sendRoll(uint64_t roll_id);
    void handleResult(uint64_t roll_id, uint32_t result);
};