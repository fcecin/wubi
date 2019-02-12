# wubi

WARNING: This is EXPERIMENTAL software and it has not been audited or throughly tested. Use it at your own risk.

This is a simple extension of the eosio.token smart contract that pays a fixed Universal Basic Income (UBI) to all account holders. It is intended to be deployed into eosio instances that enforce one-human-user-per-account limits, in which case it will issue one new token per person in perpetuity. 

This code is mostly copied from the standard eosio.token contract, with a few modifications.

Any token create()d in this contract is a token that grants exactly one (1.0) unit of itself, per day, to every account holder.

The open() action grants a basic income payment to the newly opened account.

The transfer() action, in addition to transferring tokens from a sender to a receiver, will pay any due basic income that is owed to the sender. It is the only way for an account to claim their UBI.

An account can only claim up to 360 days of pending income. Any income not claimed for more than 360 days is forfeited.

In addition to collecting income for days in the past, the user receives advance payments for the next 30 days. Thus, once an account collects UBI, it doesn't have to collect it again for the next 30 days, as they have already claimed it. During that period, the close() action will have no effect, in order to block users from cheating.

The issue() action is mostly useless, as tokens are issued automatically to users over time. There is no need to perform any kind of initial distribution.

The retire() action has no authority checks. It is advised that the contract account be set as the sole issuer of all tokens created in this contract. That will mean the contract account is a "token burn pit" which allows any user to burn any tokens that are sent to the contract account.

The contract respects the max_supply parameter passed on create(), so if the intent is to create an uncapped supply token, the best that can be done is to use the largest supported value for max_supply (that will be around 460 trillion tokens if the token precision is set to 4).

----

WUBI is developed for the Democratic Money project:

https://medium.com/@fcecin/the-democratic-money-network-f7131c4179cc
