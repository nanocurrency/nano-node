#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <mu_coin/address.hpp>

namespace mu_coin {
    class block_hash
    {
    public:
        boost::multiprecision::uint256_t number;
    };

    class block
    {
    public:
        virtual mu_coin::address address () const = 0;
        virtual mu_coin::block_hash previous () const = 0;
        virtual boost::multiprecision::uint256_t coins () const = 0;
        virtual boost::multiprecision::uint256_t votes () const = 0;
    };
        
    class block_memory : public block
    {
    public:
        block_memory () = default;
        block_memory (boost::multiprecision::uint256_t const &, boost::multiprecision::uint256_t const &);
        mu_coin::address address () const override;
        mu_coin::block_hash previous () const override;
        boost::multiprecision::uint256_t coins () const override;
        boost::multiprecision::uint256_t votes () const override;
        mu_coin::address address_m;
        mu_coin::block_hash previous_m;
        boost::multiprecision::uint256_t coins_m;
        boost::multiprecision::uint256_t votes_m;
    };
}