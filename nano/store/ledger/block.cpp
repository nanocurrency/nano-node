#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/block.hpp>

namespace nano::store::ledger
{
block_view::block_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void block_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash, nano::block const & block)
{
	class block_predecessor_set : public nano::block_visitor
	{
	public:
		block_predecessor_set (nano::store::write_transaction const & txn_a, nano::store::ledger::block_view & block_store_a) :
			txn{ txn_a },
			block_store{ block_store_a }
		{
		}

		~block_predecessor_set () override = default;

		void fill_value (nano::block const & block_a)
		{
			auto const hash = block_a.hash ();
			nano::store::db_val value;
			block_store.block_raw_get (txn, block_a.previous (), value);
			debug_assert (value.size () != 0);
			auto const type = block_store.block_type_from_raw (value.data ());
			std::vector<uint8_t> data{ static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size () };
			std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + block_store.block_successor_offset (value.size (), type));
			block_store.raw_put (txn, data, block_a.previous ());
		}

		void send_block (nano::send_block const & block_a) override
		{
			fill_value (block_a);
		}

		void receive_block (nano::receive_block const & block_a) override
		{
			fill_value (block_a);
		}

		void open_block (nano::open_block const & block_a) override
		{
			// Open blocks don't have a predecessor
		}

		void change_block (nano::change_block const & block_a) override
		{
			fill_value (block_a);
		}

		void state_block (nano::state_block const & block_a) override
		{
			if (!block_a.previous ().is_zero ())
			{
				fill_value (block_a);
			}
		}

	private:
		nano::store::write_transaction const & txn;
		nano::store::ledger::block_view & block_store;
	};

	debug_assert (block.sideband ().successor.is_zero () || exists (txn, block.sideband ().successor));
	std::vector<uint8_t> vector;
	{
		// TODO: Use db_val serialization operators
		nano::vectorstream stream{ vector };
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (txn, vector, hash);
	block_predecessor_set predecessor{ txn, *this };
	block.visit (predecessor);
	debug_assert (block.previous ().is_zero () || successor (txn, block.previous ()) == hash);
}

void block_view::raw_put (nano::store::write_transaction const & txn, std::vector<uint8_t> const & data, nano::block_hash const & hash)
{
	nano::store::db_val value{ data.size (), (void *)data.data () };
	auto status = backend.put (txn, nano::store::table::blocks, hash, value);
	backend.release_assert_success (status);
}

std::optional<nano::block_hash> block_view::successor (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	nano::store::db_val value;
	block_raw_get (txn, hash, value);
	nano::block_hash result;
	if (value.size () != 0)
	{
		debug_assert (value.size () >= result.bytes.size ());
		auto type = block_type_from_raw (value.data ());
		nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (value.size (), type), result.bytes.size () };
		bool error = nano::try_read (stream, result.bytes);
		(void)error;
		debug_assert (!error);
	}
	else
	{
		result.clear ();
	}
	if (result.is_zero ())
	{
		return std::nullopt;
	}
	return result;
}

void block_view::successor_clear (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	nano::store::db_val value;
	block_raw_get (txn, hash, value);
	debug_assert (value.size () != 0);
	auto type = block_type_from_raw (value.data ());
	std::vector<uint8_t> data{ static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size () };
	std::fill_n (data.begin () + block_successor_offset (value.size (), type), sizeof (nano::block_hash), uint8_t{ 0 });
	raw_put (txn, data, hash);
}

std::shared_ptr<nano::block> block_view::get (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	nano::store::db_val value;
	block_raw_get (txn, hash, value);
	std::shared_ptr<nano::block> result;
	if (value.size () != 0)
	{
		nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		nano::block_type type;
		bool error = try_read (stream, type);
		release_assert (!error);
		result = nano::deserialize_block (stream, type);
		release_assert (result != nullptr);
		nano::block_sideband sideband;
		error = sideband.deserialize (stream, type);
		release_assert (!error);
		result->sideband_set (sideband);
	}
	return result;
}

void block_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	auto status = backend.del (txn, nano::store::table::blocks, hash);
	backend.release_assert_success (status);
}

bool block_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::blocks, hash);
}

uint64_t block_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::blocks);
}

auto block_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::blocks) };
}

auto block_view::begin (nano::store::transaction const & txn, nano::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::blocks, hash) };
}

auto block_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::blocks) };
}

void block_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}

void block_view::block_raw_get (nano::store::transaction const & txn, nano::block_hash const & hash, nano::store::db_val & value) const
{
	auto status = backend.get (txn, nano::store::table::blocks, hash, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
}

size_t block_view::block_successor_offset (size_t entry_size, nano::block_type type) const
{
	return entry_size - nano::block_sideband::size (type);
}

nano::block_type block_view::block_type_from_raw (void const * data) const
{
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data))[0]);
}

}
