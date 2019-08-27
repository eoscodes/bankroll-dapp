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
    payoutsTable(receiver, receiver.value),
    statsTable(receiver, receiver.value),
    signvals_table("orng.wax"_n, "orng.wax"_n.value)
    {}
    
    ACTION init();
    ACTION announceroll(name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient);
    ACTION announcebet(name creator, uint64_t creator_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed);
    ACTION payoutbet(name from, asset quantity);
    ACTION setpaused(bool paused);
    
    ACTION receiverand(uint64_t assoc_id, checksum256 random_value);
    [[eosio::on_notify("eosio.token::transfer")]] void receivewaxtransfer(name from, name to, asset quantity, std::string memo);
    [[eosio::on_notify("pinknettoken::transfer")]] void receivepinktransfer(name from, name to, asset quantity, std::string memo);
    
    ACTION notifyresult(name creator, uint64_t creator_id, uint32_t result);
  
    ACTION logannounce(uint64_t roll_id, name creator, uint64_t creator_id, uint32_t max_result, name rake_recipient);
    ACTION logbet(uint64_t roll_id, uint64_t bet_id, name bettor, asset quantity, uint32_t lower_bound, uint32_t upper_bound, uint32_t multiplier, uint64_t random_seed);
    ACTION logstartroll(uint64_t roll_id, name creator, uint64_t creator_id);
    ACTION loggetrand(uint64_t roll_id, uint32_t result, asset bankroll_change, asset new_bankroll, checksum256 random_value);
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
    
    
    TABLE payoutStruct {
      name bettor;
      asset outstanding_payout;
      
      uint64_t primary_key() const { return bettor.value; }
    };
    typedef multi_index<"payouts"_n, payoutStruct> payouts_t;
    
    
    TABLE statsStruct {
      asset bankroll = asset(0, symbol("WAX", 8));
      uint64_t current_roll_id = 0;
      bool paused = false;
    };
    typedef singleton<"stats"_n, statsStruct> stats_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index<"stats"_n, statsStruct> stats_t_for_abi;
    
    
    TABLE signvals_a {
        uint64_t signing_value;

        auto primary_key() const { return signing_value; }
    };
    using signvals_table_type = multi_index<"signvals.a"_n, signvals_a>;
    
    
    rolls_t rollsTable;
    payouts_t payoutsTable;
    stats_t statsTable;
    signvals_table_type signvals_table;
  
    void transferFromBankroll(name recipient, asset quantity, std::string memo);
    void handleDeposit(name investor, asset quantity);
    void handleStartRoll(name creator, uint64_t creator_id, asset quantity);
    bool isPaused();
    
    
    /**
     * The following code is taken from the eosio.token contract
     * https://github.com/EOSIO/eosio.contracts/blob/master/contracts/eosio.token
     * It is needed to get the supply from the pinknettoken contract
     * /
    
    
    /**
    * Get supply method.
    *
    * @details Gets the supply for token `sym_code`, created by `token_contract_account` account.
    *
    * @param token_contract_account - the account to get the supply for,
    * @param sym_code - the symbol to get the supply for.
    */
    static asset get_supply( const name& token_contract_account, const symbol_code& sym_code )
    {
      stats statstable( token_contract_account, sym_code.raw() );
      const auto& st = statstable.get( sym_code.raw() );
      return st.supply;
    }
    
    
    struct [[eosio::table]] currency_stats {
      asset    supply;
      asset    max_supply;
      name     issuer;
      
      uint64_t primary_key()const { return supply.symbol.code().raw(); }
    };
    
    typedef eosio::multi_index< "stat"_n, currency_stats > stats;
};
