#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/common.hpp>
#include <nano/store/fwd.hpp>
#include <nano/store/write_queue.hpp>

#include <filesystem>
#include <memory>

namespace nano::store
{
struct ledger_store_params
{
	bool backup_before_upgrade{ false };
	bool defer_open{ false }; // If true, skip automatic open/upgrade in constructor (for testing)
};

class ledger_store
{
public:
	explicit ledger_store (std::unique_ptr<nano::store::backend>, nano::store::open_mode mode, nano::stats &, nano::logger &, ledger_store_params = {});
	~ledger_store ();

	nano::store::write_transaction tx_begin_write ();
	nano::store::read_transaction tx_begin_read () const;

	bool empty (nano::store::transaction const &) const;
	void initialize (nano::store::write_transaction const &, nano::ledger_constants const &);
	void perform_upgrades (nano::store::backend_meta);

	uint64_t count (nano::store::transaction const &, nano::store::table) const;

	uint64_t get_version () const;
	std::string get_vendor () const;
	std::filesystem::path get_database_path () const;
	nano::store::open_mode get_mode () const;

public: // Upgrades
	void upgrade_v21_to_v22 ();
	void upgrade_v22_to_v23 ();
	void upgrade_v23_to_v24 ();
	void upgrade_v24_to_v25 ();
	void upgrade_v25_to_v26 ();

public:
	nano::store::write_queue write_queue; // TODO: Shouldn't be public

public:
	nano::stats & stats;
	nano::logger & logger;

private:
	std::unique_ptr<nano::store::backend> backend_impl;
	std::unique_ptr<nano::store::ledger::successor_view> successor_impl;
	std::unique_ptr<nano::store::ledger::block_view> block_impl;
	std::unique_ptr<nano::store::ledger::account_view> account_impl;
	std::unique_ptr<nano::store::ledger::pending_view> pending_impl;
	std::unique_ptr<nano::store::ledger::rep_weight_view> rep_weight_impl;
	std::unique_ptr<nano::store::ledger::online_weight_view> online_weight_impl;
	std::unique_ptr<nano::store::ledger::pruned_view> pruned_impl;
	std::unique_ptr<nano::store::ledger::peer_view> peer_impl;
	std::unique_ptr<nano::store::ledger::confirmation_height_view> confirmation_height_impl;
	std::unique_ptr<nano::store::ledger::final_vote_view> final_vote_impl;
	std::unique_ptr<nano::store::ledger::version_view> version_impl;

public:
	nano::store::backend & backend;
	nano::store::ledger::successor_view & successor;
	nano::store::ledger::block_view & block;
	nano::store::ledger::account_view & account;
	nano::store::ledger::pending_view & pending;
	nano::store::ledger::rep_weight_view & rep_weight;
	nano::store::ledger::online_weight_view & online_weight;
	nano::store::ledger::pruned_view & pruned;
	nano::store::ledger::peer_view & peer;
	nano::store::ledger::confirmation_height_view & confirmation_height;
	nano::store::ledger::final_vote_view & final_vote;
	nano::store::ledger::version_view & version;

public:
	static nano::store::backend_version_t constexpr version_minimum{ 21 };
	static nano::store::backend_version_t constexpr version_current{ 26 };

public:
	static nano::store::column_schema const schema_current;
	static nano::store::column_schema const schema_v21;
	static nano::store::column_schema const schema_v22;
	static nano::store::column_schema const schema_v23;
	static nano::store::column_schema const schema_v24;
	static nano::store::column_schema const schema_v25;
};
};