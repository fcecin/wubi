/**
 * Source code for the WUBI token contract. 
 */

#include <wubi.hpp>

namespace eosio {

void token::create( name   issuer,
                    asset  maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}

// issue() need not be invoked for our UBI token. It doesn't seem to make any sense.
// The authority that issues tokens is "time." Elapsed time and KYC do all of the issuance.
// When creating the UBI token record, the "issuer" can be set to the name of the account
//   where this contract is deployed, and later the contract can be set to be "immutable"
//   (i.e. set active/owner permissions to "eosio"), so that issue() is impossible.
void token::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );

    if( to != st.issuer ) {
      SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                          { st.issuer, to, quantity, memo }
      );
    }
}

// The retire() action has no authorization checks. Anyone can burn tokens that are sitting
//   in the issuer account (which should probably be set to the account that hosts the contract).
void token::retire( asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    // ANYONE can burn tokens that are sitting in the issuer account.
    // The issuer account is a token burn pit.
    //require_auth( st.issuer );
    
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must retire positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( st.issuer, quantity );
}

// If account "from" has unclaimed UBI tokens, it will receive them before the
//   sufficient-balance check for the transfer. 
void token::transfer( name    from,
                      name    to,
                      asset   quantity,
                      string  memo )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    auto payer = has_auth( to ) ? to : from;

    //------------------------------------------------------------------------------------------
    // UBI claim check begin.
    //------------------------------------------------------------------------------------------

    accounts from_acnts( _self, from.value );
    const auto& from_account = from_acnts.get( sym.raw(), "no balance object found" );
   
    const time_type today = get_today();

    if (from_account.last_claim_day < today) {

      // The UBI grants 1 token per day per account. 
      // Users will automatically issue their own money as a side-effect of giving money to others.
      
      // Compute the claim amount relative to days elapsed since the last claim, excluding today's pay.
      // If you claimed yesterday, this is zero.
      int64_t claim_amount = today - from_account.last_claim_day - 1;
      // The limit for claiming accumulated past income is 360 days/coins. Unclaimed tokens past that
      //   one year maximum of accumulation are lost.
      time_type lost_days = 0;
      if (claim_amount > max_past_claim_days) {
        lost_days = claim_amount - max_past_claim_days;
	claim_amount = max_past_claim_days;
      }
      // You always claim for the next 30 days, counting today. This is the advance-payment part
      //   of the UBI claim.
      claim_amount += claim_days;

      int64_t precision_multiplier = get_precision_multiplier(quantity.symbol);
      asset claim_quantity = asset{claim_amount * precision_multiplier, quantity.symbol};

      // Respect the max_supply limit for UBI issuance.
      int64_t available_amount = st.max_supply.amount - st.supply.amount;
      if (claim_quantity.amount > available_amount)
	claim_quantity.set_amount(available_amount);

      if (claim_quantity.amount > 0) {

	// Update the token total supply.
	statstable.modify( st, same_payer, [&]( auto& s ) {
	    s.supply += claim_quantity;
        });

	// Finally, move the claim date window proportional to the amount of days of income we claimed
	//   (and also account for days of income that have been forever lost)
	from_acnts.modify( from_account, from, [&]( auto& a ) {
	    a.last_claim_day += lost_days + (claim_quantity.amount / precision_multiplier);
	  });

	// Pay the user doing the transfer ("from").
	add_balance( from, claim_quantity, payer );
      }
    }

    //------------------------------------------------------------------------------------------
    // UBI claim check end.
    //------------------------------------------------------------------------------------------

    sub_balance( from, quantity );
    add_balance( to, quantity, payer );
}

void token::sub_balance( name owner, asset value ) {
   accounts from_acnts( _self, owner.value );

   const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

   from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
}

void token::add_balance( name owner, asset value, name ram_payer )
{
   accounts to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

// Opening an account will immediately reward the user with UBI.
void token::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   eosio_assert( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {

     // we are assuming that every account in the system is an account tied to an unique
     // human identity. We will attempt to claim 1.0 token per day for the next 30 days.
     //
     // TODO: for non KYC accounts, just set claim_quantity to zero
     //
     int64_t precision_multiplier = get_precision_multiplier(symbol);
     asset claim_quantity = asset{claim_days * precision_multiplier, symbol};

     // Respect the max_supply limit for UBI issuance.
     int64_t available_amount = st.max_supply.amount - st.supply.amount;
     if (claim_quantity.amount > available_amount)
       claim_quantity.set_amount(available_amount);
     
     // Update the token total supply to account for the issued UBI tokens.
     statstable.modify( st, same_payer, [&]( auto& s ) {
	 s.supply += claim_quantity;
       });

     // Create the account with the initial UBI claim.
     acnts.emplace( ram_payer, [&]( auto& a ){
	 a.balance = claim_quantity;
	 a.last_claim_day = get_today() - 1 + (claim_quantity.amount / precision_multiplier);
       });
   }
}

void token::close( name owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( _self, owner.value );
   auto it = acnts.find( symbol.code().raw() );
   eosio_assert( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   eosio_assert( it->balance.amount == 0, "Cannot close because the balance is not zero." );

   // users cannot close their token records if they have already received income for the
   // current day. if this is not stopped, users can print infinite money by repeatedly closing and reopening.
   eosio_assert( it->last_claim_day < get_today(), "Cannot close() yet: income was already claimed for today." );
   
   acnts.erase( it );
}

} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(transfer)(open)(close)(retire) )
