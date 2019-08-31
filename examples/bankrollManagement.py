import math
import random



class Bet:
    def __init__(self, amount, lowerBound, upperBound, multiplier, maxResult):
        self.amount = amount
        self.lowerBound = lowerBound
        self.upperBound = upperBound
        self.multiplier = multiplier
        self.payout = amount * multiplier
        self.odds = (upperBound - lowerBound + 1) / maxResult
        self.EV = self.odds * multiplier
        #if self.EV > 0.990001:  # slightly more so that float inaccuracy dont make this throw
            #raise Exception("EV cant be greater than 0.99")
        self.devPayout = (1 - self.EV - 0.007) * amount     # payouts for both the bankroll devs and the 3rd party devs


class ChainedRange:
    def __init__(self, lowerBound, upperBound, payout):
        self.next = None
        self.lowerBound = lowerBound
        self.upperBound = upperBound
        self.payout = payout

    def insertBet(self, bet):
        if bet.upperBound > self.upperBound:
            self.next.insertBet(bet)
        
        if bet.lowerBound <= self.upperBound:
            if bet.lowerBound <= self.lowerBound:
                if bet.upperBound >= self.upperBound:
                    # Bet is All
                    self.payout += bet.payout
                else:
                    # Bet is Left
                    newRange = ChainedRange(bet.upperBound + 1, self.upperBound, self.payout)
                    self.upperBound = bet.upperBound
                    self.insertNextRange(newRange)
                    self.payout += bet.payout
            else:
                if bet.upperBound >= self.upperBound:
                    # Bet is Right
                    newRange = ChainedRange(bet.lowerBound, self.upperBound, self.payout + bet.payout)
                    self.upperBound = bet.lowerBound - 1
                    self.insertNextRange(newRange)
                else:
                    # Bet is Middle
                    newMiddleRange = ChainedRange(bet.lowerBound, bet.upperBound, self.payout + bet.payout)
                    newRightRange = ChainedRange(bet.upperBound + 1, self.upperBound, self.payout)
                    self.upperBound = bet.lowerBound - 1
                    self.insertNextRange(newMiddleRange)
                    newMiddleRange.insertNextRange(newRightRange)

    def insertNextRange(self, chainedRange):
        chainedRange.next = self.next
        self.next = chainedRange

    def __str__(self):
        mainInfo = "" + str(self.lowerBound) + " - " + str(self.upperBound) + ": " + str(self.payout) + " payout"
        if self.next is not None:
            return mainInfo + "\n" + str(self.next)
        else:
            return mainInfo


# Calculates the minimum bankroll required to accept the bets in the chained ranges
def calculateMinBankroll(chainedRangeStart, amountCollected, maxResult):
    variance = 0
    currentRange = chainedRangeStart
    while currentRange is not None:
        if currentRange.payout > amountCollected:
            # Odds of this range winning
            odds = (currentRange.upperBound - currentRange.lowerBound + 1) / maxResult
            # This factor is the max percentage of the bankroll that could be bet on this result, if it were the only bet
            maxBetFactor = 5 / math.sqrt(1 / odds - 1) - 0.2
            # This is the amount that the bankroll has to play if this range wins, plus the initial bet amount on this range
            effectivePayout = currentRange.payout - amountCollected + currentRange.payout * odds
            # The odds of going losing 50% of the bankroll in 100 bets approximately grows proportional to the cube of the relative size of the bet
            variance += ((effectivePayout * odds) / maxBetFactor) ** 3
        currentRange = currentRange.next
    # To make the previously cubed values proportional to the bankroll that allows them, the cube root is needed
    return variance ** (1/3) * 125


# Simulates a specified amount of rolls and returns the bankroll after these rolls
def simulateBets(bets, rollAmount, startBankroll):
    startBankroll = startBankroll
    bankroll = startBankroll

    for i in range(rollAmount):
        currentFactor = bankroll / startBankroll
        result = random.randint(1, 1000)
        for bet in bets:
            betResult = bet.amount - bet.devPayout
            if bet.lowerBound <= result <= bet.upperBound:
                betResult -= bet.payout
            bankroll += betResult * currentFactor
    return bankroll


# Calls the simulateBets() function simAmount times and counts how often the result of these calls are below the specified watchLimit
def simulateMultipleBets(bets, startBankroll, rollAmount, simAmount, watchLimit):
    results = [0]*simAmount
    watchAmount = 0
    for i in range(simAmount):
        results[i] = simulateBets(bets, rollAmount, startBankroll)
        if results[i] <= watchLimit:
            watchAmount += 1
    return watchAmount / simAmount


# Calculates the minimum required bankroll needed to accept this roll (mirrors smart contract functionality)
# Then tests this by simulating 10,000 of the following experiments:
# Simulate 100 rolls, and check if the bankroll is less than 50% of the starting bankroll
#
# Note: The bets are not modified for the individual rolls. That means, that even if the bankroll already decreased,
#       the bets are still the same size as in the beginning. That makes ending up with <0.5x starting bankroll more likely
#       So in reality, those odds are smaller
def testBankrollManagement(bets, maxResult):
    initialRange = ChainedRange(1, 1000, 0)
    totalCollected = 0
    for bet in bets:
        totalCollected += bet.amount - bet.devPayout
        initialRange.insertBet(bet)
    minBR = calculateMinBankroll(initialRange, totalCollected, maxResult)
    print("The minimum required bankroll for this roll is: " + str(minBR))

    simResult = simulateMultipleBets(bets, minBR, 100, 10000, 0.5 * minBR)
    simResultString = str(round(simResult * 10000) / 100) + "%"
    print("\nAfter 100 rolls, an average of " + simResultString + " ended with less than 0.5x of the starting bankroll")


# Example bets. Feel free to change these, remove them or add more
bets = [
    Bet(50, 1, 50, 2, 100),
    Bet(20, 41, 65, 4, 100),
    Bet(40, 200, 300, 9, 100),
    Bet(100, 100, 900, 1.10, 100)
]

testBankrollManagement(bets, 100)
