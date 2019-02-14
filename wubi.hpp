/**
 * This is a derivative of eosio.token that pays (issues) one token per day per account since 
 *   the day the account is opened or makes its first transfer. The UBI is paid on open() and 
 *   on transfer() for the originating ("from") account.
 * 
 * When an UBI is paid, it is paid for all of the unclaimed days in the past up to the next 
 *   30 days so one does not have to claim them every day to accumulate a significant balance.
 *
 * To create an uncapped supply token, just set the max_supply to 2^62 which is the maximum 
 *   supported (taking into account the digits spent on the token precision). For a precision
 *   of 4, that's around 461 trillion tokens. 
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class [[eosio::contract("wubi")]] token : public contract {
      public:
         using contract::contract;

         [[eosio::action]]
         void create( name   issuer,
                      asset  maximum_supply);

         [[eosio::action]]
         void issue( name to, asset quantity, string memo );

         [[eosio::action]]
         void retire( asset quantity, string memo );

         [[eosio::action]]
         void transfer( name    from,
                        name    to,
                        asset   quantity,
                        string  memo );

         [[eosio::action]]
         void open( name owner, const symbol& symbol, name ram_payer );

         [[eosio::action]]
         void close( name owner, const symbol& symbol );

         static asset get_supply( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.supply;
         }

         static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
         {
            accounts accountstable( token_contract_account, owner.value );
            const auto& ac = accountstable.get( sym_code.raw() );
            return ac.balance;
         }

      private:
	 typedef uint16_t time_type;

         struct [[eosio::table]] account {
	    asset     balance;

            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         struct [[eosio::table]] currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
         };

         typedef eosio::multi_index< "accounts"_n, account > accounts;
         typedef eosio::multi_index< "stat"_n, currency_stats > stats;

         void sub_balance( name owner, asset value );
         void add_balance( name owner, asset value, name ram_payer );

	 // Unfortunately, we need to waste a ton of space to store an extra 16 bits per user account
	 //    to conform to the interface expected by eosio.token.
	 // As with the "account" struct, the scope is recycled as the token holder, and the primary
	 //    key of our structure is the token symbol code.
	 struct [[eosio::table]] extra {
	    uint64_t  symbol_code_raw;
	    time_type last_claim_day;

	    uint64_t primary_key()const { return symbol_code_raw; }
	 };

	 typedef eosio::multi_index< "extras"_n, extra > extras;

	 void log_claim( name claimant, asset claim_quantity, time_type last_claim_day, time_type last_claim_day_delta, time_type lost_days );
 
	 int64_t get_precision_multiplier ( const symbol& symbol ) {
	   int64_t precision_multiplier = 1;
	   for (int i=0; i<symbol.precision(); ++i)
	     precision_multiplier *= 10;
	   return precision_multiplier;
	 }

	 static string days_to_string( int64_t days );
 
	 static time_type get_today() { return (time_type)(current_time() / 86400000000); }

	 static const int64_t claim_days = 30;

	 static const int64_t max_past_claim_days = 360;
   };

} /// namespace eosio
