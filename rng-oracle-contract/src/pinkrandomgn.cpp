#include <pinkrandomgn.hpp>

//Only needs to be called once after contract creation
ACTION pinkrandomgn::init() {
  require_auth(_self);
  configTable.get_or_create(_self, configStruct{});
}


/**
 * External contracts can call this function to request a random value
 * The provided signing_value value is hashed with sha256, and the resulting hash is saved.
 * The hash will then be signed off chain, and the result will be called back via the setrand action
 * 
 * @param assoc_id - The id that will be sent back to the caller together with the random value
 *                   Is meant to track results
 * @param signing_value - The value to be signed and used for the randomness generation
 * @param caller - The account name calling this action
 */
ACTION pinkrandomgn::requestrand(uint64_t assoc_id, uint64_t signing_value, name caller) {
  check(!isPaused(),
  "the randomness oracle is currently paused and does not accept new jobs");
  require_auth(caller);
  
  auto seed_itr = usedSeedsTable.find(signing_value);
  check(seed_itr == usedSeedsTable.end(),
  "signing value already used");
  
  usedSeedsTable.emplace(caller, [&](auto& s) {
    s.seed = signing_value;
  });
  
  configStruct config = configTable.get();
  uint64_t id = config.current_job_id++;
  configTable.set(config, _self);
  
  openJobsTable.emplace(caller, [&](auto& j){
    j.id = id;
    j.caller = caller;
    j.assoc_id = assoc_id;
    j.signing_value = signing_value;
    j.signing_hash = sha256((const char *)&signing_value, 8);
  });
}




/**
 * This function will be called from an external script with a signature of the signed hash generated in the requestrand action
 * To ensure the integrity of the game, the hash is verified on chain
 * 
 * @param job_id - The id of the job for to send the signature for
 * @param sig - The signature from signing the signing_hash of the job
 */
ACTION pinkrandomgn::setrand(uint64_t job_id, signature sig) {
  require_auth(_self);
  auto job_itr = openJobsTable.find(job_id);
  check(job_itr != openJobsTable.end(),
  "no job with this id exists");
  
  configStruct config = configTable.get();
  public_key pub_key = config.pub_key;
  //Confirming that this signature was signed by the private key corresponding to the public key
  assert_recover_key(job_itr->signing_hash, sig, pub_key);
  
  //The signature is then again hashed, to generate the final random hash that
  //will be sent back to the caller
  checksum256 random_hash = sha256((char *)&sig, sizeof(sig));
  
	action(
    permission_level{_self, "active"_n},
    job_itr->caller,
    "receiverand"_n,
    std::make_tuple(job_itr->assoc_id, random_hash)
  ).send();
  
  openJobsTable.erase(job_itr);
  
}




/**
 * @dev Allows the devs to change the public_key
 * Only possible when no bets are open, in order not to risk the randomness integrity
 * 
 * @param pub_key - The new public key (of which the corresponding private key will be used for signing)
 */
ACTION pinkrandomgn::setpubkey(public_key pub_key) {
  require_auth(_self);
  check(openJobsTable.begin() == openJobsTable.end(),
  "cant change the key if there are open bets");
  
  configStruct config = configTable.get();
  config.pub_key = pub_key;
  configTable.set(config, _self);
}




/**
 * @dev Can be called by the dev account to pause/ unpause the contract.
 * This should hopefully never have to be used, but acts as an emergency stop if it is ever needed
 * Withdrawals and Payouts are still enabled when paused, but no new rolls/ bets will be accepted
 * 
 * @param paused - The bool value to pause/ unpause the contract
 */
ACTION pinkrandomgn::setpaused(bool paused) {
  require_auth("pinknetworkx"_n);
  configStruct config = configTable.get();
  config.paused = paused;
  configTable.set(config, _self);
}




bool pinkrandomgn::pinkrandomgn::isPaused() {
  configStruct config = configTable.get();
  return config.paused;
}