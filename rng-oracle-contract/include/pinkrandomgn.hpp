#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/crypto.hpp>

using namespace eosio;

CONTRACT pinkrandomgn : public contract {
  public:
    using contract::contract;
    pinkrandomgn(name receiver, name code, datastream<const char*> ds):contract(receiver, code, ds),
    openJobsTable(receiver, receiver.value),
    usedSeedsTable(receiver, receiver.value),
    configTable(receiver, receiver.value)
    {}
    
    ACTION init();
    
    ACTION requestrand(uint64_t assoc_id, uint64_t signing_value, name caller);
    ACTION setrand(uint64_t job_id, signature sig);
    ACTION setpubkey(public_key pub_key);
    
    ACTION testverify(public_key pub_key, checksum256 value, signature sig);
    
  private:
  
    TABLE openJobsStruct {
      uint64_t id;
      name caller;
      uint64_t assoc_id;
      uint64_t signing_value;
      checksum256 signing_hash;
      
      uint64_t primary_key() const { return id; }
    };
    typedef multi_index<"openjobs"_n, openJobsStruct> openjobs_t;
    
    
    
    TABLE usedSeedStruct {
      uint64_t seed;
      
      uint64_t primary_key() const { return seed; }
    };
    typedef multi_index<"usedseeds"_n, usedSeedStruct> usedseeds_t;
    
    
    TABLE configStruct {
      public_key pub_key;
      bool paused = false;
    };
    typedef singleton<"config"_n, configStruct> config_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index<"config"_n, configStruct> config_t_for_abi;
    
    
    openjobs_t openJobsTable;
    usedseeds_t usedSeedsTable;
    config_t configTable;
  
    bool isPaused();
};