#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/noncopyable.hpp>

namespace mu_coin {
    class block;
    class balance : boost::noncopyable
    {
    public:
        virtual boost::multiprecision::uint256_t coins () const = 0;
        virtual boost::multiprecision::uint256_t votes () const = 0;
        virtual void operator += (mu_coin::block const &) = 0;
        virtual void operator -= (mu_coin::block const &) = 0;
    };
    
    class balance_memory : public balance
    {
    public:
        void operator += (mu_coin::block const &) override;
        void operator -= (mu_coin::block const &) override;
        boost::multiprecision::uint256_t coins () const override;
        boost::multiprecision::uint256_t votes () const override;
        boost::multiprecision::uint256_t coins_m;
        boost::multiprecision::uint256_t votes_m;
    };
}