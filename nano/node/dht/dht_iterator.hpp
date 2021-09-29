#pragma once

// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#include <nano/node/dht/dht.hpp>
#include <nano/node/dht/dht_definitions.hpp>
#include <nano/secure/store.hpp>

#include <cstdint>

#include <diskhash.hpp>
#include <diskhash_iterator.hpp>

namespace nano
{
class unchecked_dht_iterator : public store_iterator_impl<nano::unchecked_key, nano::unchecked_info>
{
private:
	using diskhash_iterator_type = typename dht::DiskHash<DHT_unchecked_info>::iterator;
	diskhash_iterator_type current_iterator;
	diskhash_iterator_type current_iterator_end;
	std::unique_ptr<unchecked_key> current_key;
	std::unique_ptr<unchecked_info> current_value;
	std::pair<nano::unchecked_key &, nano::unchecked_info &> current;

public:
	unchecked_dht_iterator (typename dht::DiskHash<DHT_unchecked_info> const & dht_a) :
		current_iterator (dht_a.begin ()),
		current_iterator_end (dht_a.end ()),
		current_key (make_current_key (current_iterator->first)),
		current_value (make_current_value (current_iterator->second)),
		current ({ *current_key, *current_value })
	{
	}

	unchecked_dht_iterator () = delete;

	unchecked_dht_iterator (nano::unchecked_dht_iterator && other_a) :
		current_iterator (std::move (other_a.current_iterator)),
		current_iterator_end (std::move (other_a.current_iterator_end)),
		current_key (make_current_key (current_iterator->first)),
		current_value (make_current_value (current_iterator->second)),
		current ({ *current_key, *current_value })
	{
		other_a.load_current ();
	}

	nano::store_iterator_impl<nano::unchecked_key, nano::unchecked_info> & operator++ () override
	{
		++current_iterator;
		load_current ();
		return *this;
	}

	nano::store_iterator_impl<nano::unchecked_key, nano::unchecked_info> & operator-- () override
	{
		// TODO: implement this on DiskHash iterator.
		release_assert (false);
		//	--current_iterator;
		//	load_current();
		return *this;
	}

	std::pair<nano::unchecked_key &, nano::unchecked_info &> * operator-> ()
	{
		return &current;
	}

	bool operator== (nano::store_iterator_impl<nano::unchecked_key, nano::unchecked_info> const & other_a) const override
	{
		auto & converted_it (dynamic_cast<nano::unchecked_dht_iterator const &> (other_a));
		return (current_iterator == converted_it.current_iterator);
	}

	bool is_end_sentinal () const override
	{
		return (current_iterator == current_iterator_end);
	}

	void fill (std::pair<nano::unchecked_key, nano::unchecked_info> & value_a) const override
	{
		value_a.first = *current_key;
		value_a.second = *current_value;
	}

	// TODO: Remove or uncomment this if it is (not) necessary.
	//	nano::unchecked_dht_iterator & operator= (nano::unchecked_dht_iterator && other_a)
	//	{
	//		current_iterator = other_a.current_iterator;
	//		current_iterator_end = other_a.current_iterator_end;
	//		load_current();
	//		other_a.current_iterator = other_a.current_iterator_end;
	//		other_a.load_current();
	//		return *this;
	//	}

	nano::store_iterator_impl<nano::unchecked_key, nano::unchecked_info> & operator= (nano::store_iterator_impl<nano::unchecked_key, nano::unchecked_info> const &) = delete;

	bool operator== (nano::unchecked_dht_iterator const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}

	bool operator!= (nano::unchecked_dht_iterator const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::unique_ptr<nano::unchecked_key> make_current_key (std::string encoded_key)
	{
		auto key = std::make_unique<nano::unchecked_key> ();
		auto status (key->decode_hex (encoded_key));
		release_assert (status);
		return key;
	}

	std::unique_ptr<nano::unchecked_info> make_current_value (DHT_unchecked_info const & value_a)
	{
		nano::unchecked_info_dht_val dht_val (value_a);
		auto value = std::make_unique<nano::unchecked_info> (nano::unchecked_info (dht_val));
		return value;
	}

	void load_current ()
	{
		auto converted_iterator (convert_iterator (current_iterator));
		*current_key = converted_iterator.first;
		*current_value = converted_iterator.second;
	}

	std::pair<nano::unchecked_key, nano::unchecked_info> convert_iterator (diskhash_iterator_type & iterator)
	{
		nano::unchecked_key key;
		key.encode_hex (iterator->first);
		nano::unchecked_info_dht_val dht_val (iterator->second);
		nano::unchecked_info info (dht_val);
		return std::pair<nano::unchecked_key, nano::unchecked_info> ({ nano::unchecked_key (key), nano::unchecked_info (info) });
	}
};

}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
