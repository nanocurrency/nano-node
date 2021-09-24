#pragma once

// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#include <nano/node/dht/dht.hpp>
#include <nano/node/lmdb/lmdb_iterator.hpp>
#include <nano/secure/store.hpp>

#include <diskhash_iterator.hpp>

namespace nano
{
template <typename T, typename U, typename V, typename W>
class dht_iterator : public mdb_iterator<T, U>
{
private:
	typename dht::DiskHash<W>::iterator current_iterator;
	typename dht::DiskHash<W> & dht;
	std::pair<V, W> current;

public:
	dht_iterator () = delete;

	dht_iterator (typename dht::DiskHash<W> const & dht) :
		dht (dht), current_iterator (dht.begin ())
	{
	}

	dht_iterator (typename dht::DiskHash<W>::iterator const & iterator) :
		dht (iterator.dht), current_iterator (iterator)
	{
	}

	dht_iterator (nano::dht_iterator<T, U, V, W> const &) = delete;

	nano::dht_iterator<T, U, V, W> & operator++ () override
	{
		return dht_iterator (current_iterator++);
	}

	nano::dht_iterator<T, U, V, W> & operator-- () override
	{
		return dht_iterator (current_iterator--);
	}

	bool operator== (nano::dht_iterator<T, U, V, W> const & other_a) const override
	{
		return (current_iterator == other_a.current_iterator);
	}

	bool is_end_sentinal () const override
	{
		return (current_iterator == dht.end ());
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		if (current.first.size () != 0)
		{
			value_a.first = static_cast<T> (current.first);
		}
		else
		{
			value_a.first = T ();
		}
		if (current.second.size () != 0)
		{
			value_a.second = static_cast<U> (current.second);
		}
		else
		{
			value_a.second = U ();
		}
	}

	bool operator== (nano::dht_iterator<T, U, V, W> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}

	bool operator!= (nano::dht_iterator<T, U, V, W> const & other_a) const
	{
		return !(*this == other_a);
	}
};
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
