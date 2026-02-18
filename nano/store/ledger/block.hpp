#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <variant>

namespace nano::store::ledger
{
class successor_view;

/**
 * Iterator that wraps the raw block_data typed_iterator and enriches
 * each block_w_sideband with the successor looked up from the successor table.
 */
class block_iterator
{
public:
	using inner_type = store::typed_iterator<uint64_t, block_w_sideband>;
	using value_type = std::pair<uint64_t, block_w_sideband>;
	using iterator_category = std::bidirectional_iterator_tag;
	using difference_type = std::ptrdiff_t;
	using pointer = value_type const *;
	using const_pointer = value_type const *;
	using reference = value_type const &;
	using const_reference = value_type const &;

	block_iterator (inner_type && inner, successor_view const * successors, nano::store::transaction const * txn);

	block_iterator (block_iterator const &) = delete;
	auto operator= (block_iterator const &) -> block_iterator & = delete;

	block_iterator (block_iterator && other) noexcept;
	auto operator= (block_iterator && other) noexcept -> block_iterator &;

	auto operator++ () -> block_iterator &;
	auto operator-- () -> block_iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (block_iterator const & other) const -> bool;
	auto is_end () const -> bool;

private:
	void enrich ();

	inner_type inner;
	successor_view const * successors;
	nano::store::transaction const * txn;
	std::variant<std::monostate, value_type> current;
};

class block_view
{
public:
	using iterator = block_iterator;

public:
	block_view (nano::store::backend &, nano::store::ledger::successor_view &);

	void put (nano::store::write_transaction const &, nano::block_hash const &, nano::block const &);
	void raw_put (nano::store::write_transaction const &, std::vector<uint8_t> const & data, nano::block_hash const &);
	std::shared_ptr<nano::block> get (nano::store::transaction const &, nano::block_hash const &) const;
	void del (nano::store::write_transaction const &, nano::block_hash const &);
	bool exists (nano::store::transaction const &, nano::block_hash const &) const;
	uint64_t count (nano::store::transaction const &) const;
	iterator begin (nano::store::transaction const &, uint64_t index) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

	void load_sequence_counter (nano::store::transaction const &);
	uint64_t allocate_index (nano::store::write_transaction const &);
	uint64_t index_upper_bound () const;

private:
	nano::store::backend & backend;
	nano::store::ledger::successor_view & successor_store;
	std::atomic<uint64_t> next_index{ 1 };
};
}
