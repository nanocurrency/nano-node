#pragma once

#include <boost/variant.hpp>

namespace nano
{
template <typename Return, typename... Lambdas>
struct lambda_visitor;

template <typename Return, typename Lambda, typename... Lambdas>
struct lambda_visitor<Return, Lambda, Lambdas...> : public Lambda, public lambda_visitor<Return, Lambdas...>
{
	using Lambda::operator();
	using lambda_visitor<Return, Lambdas...>::operator();
	lambda_visitor (Lambda lambda, Lambdas... lambdas) :
	Lambda (lambda), lambda_visitor<Return, Lambdas...> (lambdas...)
	{
	}
};

template <typename Return, typename Lambda>
struct lambda_visitor<Return, Lambda> : public Lambda, public boost::static_visitor<Return>
{
	using Lambda::operator();
	lambda_visitor (Lambda lambda) :
	Lambda (lambda), boost::static_visitor<Return> ()
	{
	}
};

template <typename Return>
struct lambda_visitor<Return> : public boost::static_visitor<Return>
{
	lambda_visitor () :
	boost::static_visitor<Return> ()
	{
	}
};

template <typename Return, typename... Lambdas>
lambda_visitor<Return, Lambdas...> make_lambda_visitor (Lambdas... lambdas)
{
	return { lambdas... };
}
}
