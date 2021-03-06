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
    check( sym.is_valid(), "invalid symbol name" );
    check( maximum_supply.is_valid(), "invalid supply");
    check( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing == statstable.end(), "token with symbol already exists" );

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
//   (i.e. set active/owner permissions to "eosio.code"), so that issue() is impossible.
void token::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

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

// If the issuer is *this* contract/account, then ANYONE can retire tokens.
void token::retire( asset quantity, string memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    // If the issuer is set to this contract, then anyone can retire the tokens.
    if (st.issuer != _self)
      require_auth( st.issuer );
    
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must retire positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

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
    check( from != to, "cannot transfer to self" );

    require_auth( from );
    check( is_account( to ), "to account does not exist" );
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    auto payer = has_auth( to ) ? to : from;

    // Check for an UBI claim.
    try_ubi_claim( from, quantity.symbol, payer, statstable, st );
        
    // Do the transfer.
    sub_balance( from, quantity );
    add_balance( to, quantity, payer );
}

void token::sub_balance( name owner, asset value ) {
   accounts from_acnts( _self, owner.value );

   const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   check( from.balance.amount >= value.amount, "overdrawn balance" );

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
	a.last_claim_day = get_today() - 1;
      
	// if we are in an everything-goes, free EOSIO public chain, then
	//   open() needs to add a two-day grace period for any UBI claims
	//   to mitigate money printing by repeated account creation/destruction.
	if (unbounded_UBI_account_creation)
	  a.last_claim_day += 2;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void token::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   check( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if ( it == acnts.end() ) {
     acnts.emplace( ram_payer, [&]( auto& a ){
	 a.balance = asset{0, symbol};
       });
   }
}

void token::close( name owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( _self, owner.value );
   auto it = acnts.find( symbol.code().raw() );
   check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
   check( it->last_claim_day < get_today(), "Cannot close() yet: income was already claimed for today." );
   acnts.erase( it );
}

// This was moved from transfer() to keep it readable.
void token::try_ubi_claim( name from, const symbol& sym, name payer, stats& statstable, const currency_stats& st )
{
  // Check if the "from" account is authorized to receive it.
  if (! can_claim_UBI( from ))
    return;

  // The token contract account is NOT eligible for an UBI.
  if (from == _self)
    return;
  
  accounts from_acnts( _self, from.value );
  const auto& from_account = from_acnts.get( sym.code().raw(), "no balance object found" );
  
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
    
    int64_t precision_multiplier = get_precision_multiplier(sym);
    asset claim_quantity = asset{claim_amount * precision_multiplier, sym};
    
    // Respect the max_supply limit for UBI issuance.
    int64_t available_amount = st.max_supply.amount - st.supply.amount;
    if (claim_quantity.amount > available_amount)
      claim_quantity.set_amount(available_amount);
    
    time_type last_claim_day_delta = lost_days + (claim_quantity.amount / precision_multiplier);
    
    if (claim_quantity.amount > 0) {
      
      // Log this basic income payment with a fake inline transfer action to self.
      log_claim( from, claim_quantity, from_account.last_claim_day + last_claim_day_delta, lost_days );
      
      // Update the token total supply.
      statstable.modify( st, same_payer, [&]( auto& s ) {
	  s.supply += claim_quantity;
        });
      
      // Finally, move the claim date window proportional to the amount of days of income we claimed
      //   (and also account for days of income that have been forever lost)
      from_acnts.modify( from_account, from, [&]( auto& a ) {
	  a.last_claim_day += last_claim_day_delta;
	});
      
      // Pay the user doing the transfer ("from").
      add_balance( from, claim_quantity, payer );
    }
  }
}

// Logs the UBI claim as an "income" action that only the contract can call.
void token::log_claim( name claimant, asset claim_quantity, time_type next_last_claim_day, time_type lost_days )
{
  string claim_memo = "next on ";
  claim_memo.append( days_to_string(next_last_claim_day + 1) );
  if (lost_days > 0) {
    claim_memo.append(", lost ");
    claim_memo.append( std::to_string(lost_days) );
    claim_memo.append(" days of income.");
  }

  action {
    permission_level{_self, name("active")},
    _self,
    name("income"),
    income_notification_abi { .to=claimant, .quantity=claim_quantity, .memo=claim_memo }
  }.send();
}

// Input is days since epoch
string token::days_to_string( int64_t days )
{
  // https://stackoverflow.com/questions/7960318/math-to-convert-seconds-since-1970-into-date-and-vice-versa
  // http://howardhinnant.github.io/date_algorithms.html
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);       // [0, 146096]
  const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
  const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
  const unsigned d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
  const unsigned m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
  
  string s = std::to_string(d);
  if (s.length() == 1)
    s = "0" + s;
  s.append("-");
  string ms = std::to_string(m);
  if (ms.length() == 1)
    ms = "0" + ms;
  s.append( ms );
  s.append("-");
  s.append( std::to_string(y + (m <= 2)) );
  return s;
}

// UBI payment notification action (users can't call this action, it's used just for logging its parameters)
void token::income( name to, asset quantity, string memo ) {
  require_auth( _self );
  require_recipient( to );
}


} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(transfer)(open)(close)(retire)(income) )
