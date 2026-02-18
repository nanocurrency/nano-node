#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/successor.hpp>

namespace nano::store::ledger
{
// block_iterator implementation

block_iterator::block_iterator (inner_type && inner_a, successor_view const * successors_a, nano::store::transaction const * txn_a) :
	inner{ std::move (inner_a) },
	successors{ successors_a },
	txn{ txn_a }
{
	enrich ();
}

block_iterator::block_iterator (block_iterator && other) noexcept :
	inner{ std::move (other.inner) },
	successors{ other.successors },
	txn{ other.txn },
	current{ std::move (other.current) }
{
}

auto block_iterator::operator= (block_iterator && other) noexcept -> block_iterator &
{
	inner = std::move (other.inner);
	successors = other.successors;
	txn = other.txn;
	current = std::move (other.current);
	return *this;
}

auto block_iterator::operator++ () -> block_iterator &
{
	++inner;
	enrich ();
	return *this;
}

auto block_iterator::operator-- () -> block_iterator &
{
	--inner;
	enrich ();
	return *this;
}

auto block_iterator::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return std::get_if<value_type> (&current);
}

auto block_iterator::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return std::get<value_type> (current);
}

auto block_iterator::operator== (block_iterator const & other) const -> bool
{
	return inner == other.inner;
}

auto block_iterator::is_end () const -> bool
{
	return std::holds_alternative<std::monostate> (current);
}

void block_iterator::enrich ()
{
	if (!inner.is_end ())
	{
		auto const & [index, bws] = *inner;
		value_type enriched{ index, bws };
		if (successors && txn)
		{
			auto hash = enriched.second.block->hash ();
			auto successor_opt = successors->get (*txn, hash);
			if (successor_opt)
			{
				enriched.second.sideband.successor = *successor_opt;
				enriched.second.block->sideband_set (enriched.second.sideband);
			}
		}
		current = std::move (enriched);
	}
	else
	{
		current = std::monostate{};
	}
}

// block_view implementation

block_view::block_view (nano::store::backend & backend_a, nano::store::ledger::successor_view & successor_store_a) :
	backend{ backend_a },
	successor_store{ successor_store_a }
{
}

void block_view::load_sequence_counter (nano::store::transaction const & txn)
{
	nano::uint256_union counter_key{ 2 };
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::meta, counter_key, value);
	if (backend.success (status))
	{
		nano::uint256_union counter_value{ value };
		next_index = counter_value.number ().convert_to<uint64_t> ();
	}
	else
	{
		next_index = 1;
	}
}

uint64_t block_view::allocate_index (nano::store::write_transaction const & txn)
{
	auto index = next_index.fetch_add (1);
	nano::uint256_union counter_key{ 2 };
	nano::uint256_union counter_value{ next_index.load () };
	auto status = backend.put (txn, nano::store::table::meta, counter_key, counter_value);
	backend.release_assert_success (status);
	return index;
}

uint64_t block_view::index_upper_bound () const
{
	return next_index.load ();
}

void block_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash, nano::block const & block)
{
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream{ vector };
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (txn, vector, hash);
	if (!block.previous ().is_zero ())
	{
		successor_store.put (txn, block.previous (), hash);
	}
	debug_assert (block.previous ().is_zero () || successor_store.get (txn, block.previous ()) == hash);
}

void block_view::raw_put (nano::store::write_transaction const & txn, std::vector<uint8_t> const & data, nano::block_hash const & hash)
{
	auto index = allocate_index (txn);
	nano::store::db_val index_val{ index };
	auto status = backend.put (txn, nano::store::table::block_index, hash, index_val);
	backend.release_assert_success (status);
	nano::store::db_val value{ data.size (), (void *)data.data () };
	status = backend.put (txn, nano::store::table::block_data, index_val, value);
	backend.release_assert_success (status);
}

std::shared_ptr<nano::block> block_view::get (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	// Stage 1: Look up index from block_index table
	nano::store::db_val index_val;
	auto status = backend.get (txn, nano::store::table::block_index, hash, index_val);
	if (backend.not_found (status))
	{
		return nullptr;
	}
	release_assert (backend.success (status), backend.error_string (status));

	// Stage 2: Look up block data from block_data table
	nano::store::db_val value;
	status = backend.get (txn, nano::store::table::block_data, index_val, value);
	release_assert (backend.success (status), backend.error_string (status));

	nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
	nano::block_type type;
	bool error = try_read (stream, type);
	release_assert (!error);
	auto result = nano::deserialize_block (stream, type);
	release_assert (result != nullptr);
	nano::block_sideband sideband;
	error = sideband.deserialize (stream, type);
	release_assert (!error);

	// Stage 3: Look up successor from successor table
	auto successor_opt = successor_store.get (txn, hash);
	if (successor_opt)
	{
		sideband.successor = *successor_opt;
	}

	result->sideband_set (sideband);
	return result;
}

void block_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	// Look up index first
	nano::store::db_val index_val;
	auto status = backend.get (txn, nano::store::table::block_index, hash, index_val);
	release_assert (backend.success (status), backend.error_string (status));

	// Delete from both tables
	status = backend.del (txn, nano::store::table::block_data, index_val);
	backend.release_assert_success (status);
	status = backend.del (txn, nano::store::table::block_index, hash);
	backend.release_assert_success (status);
}

bool block_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::block_index, hash);
}

uint64_t block_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::block_index);
}

auto block_view::begin (nano::store::transaction const & txn) const -> iterator
{
	using inner_type = block_iterator::inner_type;
	return iterator{ inner_type{ backend.begin (txn, nano::store::table::block_data) }, &successor_store, &txn };
}

auto block_view::begin (nano::store::transaction const & txn, uint64_t index) const -> iterator
{
	using inner_type = block_iterator::inner_type;
	nano::store::db_val index_val{ index };
	return iterator{ inner_type{ backend.begin (txn, nano::store::table::block_data, index_val) }, &successor_store, &txn };
}

auto block_view::end (nano::store::transaction const & txn) const -> iterator
{
	using inner_type = block_iterator::inner_type;
	return iterator{ inner_type{ backend.end (txn, nano::store::table::block_data) }, nullptr, nullptr };
}

void block_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<uint64_t> (
	[&action, this] (uint64_t const & start, uint64_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}

}
