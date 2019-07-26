# Bankroll Contract

This contract is the core of our system. It handles accepting rolls from the outside, getting the randomness from an external contract and then paying out the bets accordingly.

### [Tables](#Tables)
- [rolls](#rolls)
- [rollbets](#rollbets)
- [investors](#investors)
- [payouts](#payouts)
- [payouts](#payouts)
- [stats](#stats)

### [Actions](#Actions)
- [announceroll](#announceroll)
- [announcebet](#announcebet)
- [payoutbet](#payoutbet)
- [withdraw](#withdraw)

### [Wax Transfers](#Wax_Transfers)

-----

# Tables

## rolls (Single Scope: pinkbankroll)

| Type     | Name           | Description                                                                                                                  |
|----------|----------------|------------------------------------------------------------------------------------------------------------------------------|
| uint64_t | **roll_id**        | Unique, auto incrementing id                                                                                                 |
| name     | **creator**        | Account name of the creator of this roll                                                                                     |
| uint64_t | **creator_id**     | Id set by the creator, used as a way to identify the roll without knowing the roll_id                                        |
| uint32_t | **max_result**     | The roll result will be 1 <= roll result <= max_result                                                                       |
| name     | **rake_recipient** | Account name that will receive the rake from this roll                                                                       |
| bool     | **paid**           | Internal value that is true when the roll has already been paid and is waiting for the randomness from the external contract |

## rollbets (Scope: roll_id)

| Type     | Name        | Description                                                              |
|----------|-------------|--------------------------------------------------------------------------|
| uint64_t | **bet_id**      | Unique, auto incrementing id                                             |
| name     | **bettor**      | Account name of the bettor that will receive the payout if this bet wins |
| asset    | **quantity**    | The amount of Wax bet                                                    |
| uint32_t | **lower_bound** | See below                                                                |
| uint32_t | **upper_bound** | The bet wins if lower_bound <= roll result <= upper_bound                |
| uint32_t | **multiplier**  | Multiplier of the bet x1000. (multiplier 2000 -> payout = 2 * quantity)  |
| uint64_t | **random_seed** | Seed that will be used in the randomness generation process              |

## investors (Single Scope: pinkbankroll)

| Type     | Name            | Description                                                                                                                                                                    |
|----------|-----------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| name     | **investor**        | Account name of the investor                                                                                                                                                   |
| uint64_t | **bankroll_weight** | The bankroll_weight that this investor has. An investor owns a part of the bankroll that is equal to the relation of his bankroll weight compared to the total bankroll weight |

## payouts (Single Scope: pinkbankroll)

| Type  | Name               | Description                                                                                   |
|-------|--------------------|-----------------------------------------------------------------------------------------------|
| name  | **bettor**             | Account name of the bettor                                                                    |
| asset | **outstanding_payout** | The amount of Wax that the bettor is still owed. This almost always is paid out automatically |

## stats (Single Scope: pinkbankroll)

| Type     | Name                  | Description                                                                                                           |
|----------|-----------------------|-----------------------------------------------------------------------------------------------------------------------|
| asset    | **bankroll**              | The amount of Wax currently available in the bankroll                                                                 |
| uint64_t | **total_bankroll_weight** | The sum of all investor bankroll weights                                                                              |
| uint64_t | **current_roll_id**       | Unique id that the next announced roll will use. Incrementing.                                                        |
| bool     | **paused**                | Devs can set this to true to accept no more new rolls. Withdrawals, payouts and open rolls will continue to function. |


# Actions

## announceroll
### Parameters:

| Type     | Name           | Description                                                                                                                                              |
|----------|----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
| name     | creator        | The account name of the creator of this roll. Needs to be the account calling this action                                                                |
| uint64_t | creator_id     | Id set by the creator, that will later be used to reference this roll. Needed because there is no way to let the creator know the roll_id set internally |
| uint32_t | max_result     | The roll result will be 1 <= roll result <= max_result                                                                                                   |
| name     | rake_recipient | Account name that will receive the rake from this roll                                                                                                   |

### Decription:

This is the action to be called initially when creating a new roll. The bets can not be set within this action, but are rather set later by calling the accouncebet action.

## announcebet
### Parameters:

| Type     | Name        | Description                                                                              |
|----------|-------------|------------------------------------------------------------------------------------------|
| name     | creator     | The account name of the creator of the roll. Needs to be the account calling this action |
| uint64_t | creator_id  | The creator_id that was set when initially creating this roll                            |
| asset    | quntity     | The amount of Wax to be bet on this                                                      |
| uint32_t | lower_bound | See below                                                                                |
| uint32_t | upper_bound | The bet wins if lower_bound <= roll result <= upper_bound                                |
| uint32_t | multiplier  | Multiplier of the bet x1000. (multiplier 2000 -> payout = 2 * quantity)                  |
| uint64_t | random_seed | Seed that will be used in the randomness generation process                              |

### Description:

Adds a bet to an already created roll. Note that it is not yet paid for immediately. All bets of a roll are paid for at once when starting the roll later. Can only be called by the creator of the roll.

## payoutbet
### Parameters:

| Type     | Name       | Description                                                                                      |
|----------|------------|--------------------------------------------------------------------------------------------------|
| name     | from       | The account name that will receive the payout                                                    |
| asset    | quantity   | The amount of Wax to payout                                                                      |
| uint64_t | irrelevant | This parameter is not used within the action. It is required as a workaround for internal calls. |

### Description:

This is called as a deferred action by the bankroll contract when paying out bets. This is needed to ensure that if an individual payout fails, other payouts of the same roll are not affected. Because deferred actions are not guaranteed to execute, this action can also be called manually by anyone that has outstanding payouts (but not by anyone else).

## withdraw
### Parameters:

| Type     | Name               | Description                                                                                                       |
|----------|--------------------|-------------------------------------------------------------------------------------------------------------------|
| name     | from               | The account name of the investor to withdraw from                                                                 |
| uint64_t | weight_to_withdraw | The bankroll_weight to withdraw. The Wax paid out will be `(weight_to_withdraw / total bankroll weight) * bankroll` |

### Decription:

Withdraws a part of the bankroll weight previously gained by investing. Can only be called by the investor account to withdraw from.

# Wax Transfers

Some actions are also triggered not by directly calling them, but rather by sending Wax to the contract with a special memo.
**The following memo formats are available:**

## memo: deposit
The sent Wax is added to the bankroll, and the sender receives the corresponding bankroll weight added onto his *investors* table entry.


## memo: startroll <creator_roll_id>
This is used to start a roll that has previously been announced. At least one bet has to have been announced as well. The amount of Wax sent needs to be equal to the sum of all bet amounts of this roll.
The internal bankroll management can reject starting the roll, if the risk for the bankroll is too high. In that case, the whole transfer fails and no Wax will be transferred. You can learn more about the bankroll management [here](https://medium.com/@pinknetwork/our-unique-bankroll-management-fun-for-players-safe-for-investors-75d668c39370?source=your_stories_page---------------------------).

