#include <math.h>
#include <eosio/print.hpp>

/**
 * This is a modified linked list.
 * This datastructure is used for relatively efficient insertion of elements in the middle.
 * By inserting bets in the first element, they are passed through the list, until they are inserted where appropiate.
 * If required, new ChainedRanges are automatically created and inserted in the middle of this ChainedRange
 * 
 * Note:
 * Using the new operator for creating ChainedRanges and not freeing them later is a memory leak.
 * However, the way eos works is to set up a new environment for every action and then purge it when the action is done
 * Therefore, the memory leak solves itself
 */

class ChainedRange {
  public:
  
    ChainedRange* next;
    uint32_t lowerBound;
    uint32_t upperBound;
    uint64_t payout;
    
    ChainedRange(uint32_t lowerBound1, uint32_t upperBound1, uint64_t payout1) {
      next = nullptr;
      lowerBound = lowerBound1;
      upperBound = upperBound1;
      payout = payout1;
    }
    
    void insertNextRange(ChainedRange* insertRangePtr) {
      insertRangePtr->next = next;
      next = insertRangePtr;
    }
    
    void insertBet(uint32_t betLowerBound, uint32_t betUpperBound, uint64_t betAmount) {
      if (betUpperBound > upperBound) {
        next->insertBet(betLowerBound, betUpperBound, betAmount);
      }
      
      if (betLowerBound <= upperBound) {
        if (betLowerBound <= lowerBound) {
          if (betUpperBound >= upperBound) {
            // Bet is in whole range
            payout += betAmount;
          } else {
            //Bet is on the left side of range
            ChainedRange* newRange = new ChainedRange(betUpperBound + 1, upperBound, payout);
            upperBound = betUpperBound;
            insertNextRange(newRange);
            payout += betAmount;
          }
        } else {
          if (betUpperBound >= upperBound) {
            //Bet is on the right side of range
            ChainedRange* newRange = new ChainedRange(betLowerBound, upperBound, payout + betAmount);
            upperBound = betLowerBound - 1;
            insertNextRange(newRange);
          } else {
            //Bet is in the middle of the range
            ChainedRange* newMiddleRange = new ChainedRange(betLowerBound, betUpperBound, payout + betAmount);
            ChainedRange* newRightRange = new ChainedRange(betUpperBound + 1, upperBound, payout);
            upperBound = betLowerBound - 1;
            insertNextRange(newMiddleRange);
            newMiddleRange->insertNextRange(newRightRange);
          }
        }
      }
    }
};


//This probably makes absolutely no sense to you.
//I will soon publish an article explaining why and how this works
asset getRequiredBankroll(ChainedRange firstRange, uint64_t totalBetAmount, uint32_t maxRangeLimit) {
  double variance = 0;
  ChainedRange* currentRangePtr = &firstRange;
  while (currentRangePtr != nullptr) {
    
    //TODO remove, debug
    //eosio::print(currentRangePtr->lowerBound, " - ", currentRangePtr->upperBound, " - ", currentRangePtr->payout, " ||| ");
    
    //Only losing ranges are considered
    if (currentRangePtr->payout > totalBetAmount) {
      //dds of this range winning
      double odds = (double)(currentRangePtr->upperBound - currentRangePtr->lowerBound + 1) / (double)maxRangeLimit;
      //This factor is the max percentage of the bankroll that could be bet on this result, if it were the only bet
      double maxBetFactor = (double)5 / sqrt(((double)1 / odds) - (double)1) - 0.2;
      //This is the amount that the bankroll has to play if this range wins, plus the initial bet amount on this range
      double effectivePayout = ((double) (currentRangePtr->payout - totalBetAmount) + (double)currentRangePtr->payout * odds);
      //The odds of going losing 50% of the bankroll in 100 bets approximately grows proportional to the cube of the relative size of the bet
      variance += pow(effectivePayout * odds / maxBetFactor, 3);
    }
    currentRangePtr = currentRangePtr->next;
  }
  variance = cbrt(variance);
  
  uint64_t requiredBankrollAmount = (uint64_t)(variance * (double)100);
  return asset(requiredBankrollAmount, symbol("WAX", 8));
}