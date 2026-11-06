#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/signature_verification.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>

#include <optional>

namespace nano
{
class ledger_processor final : public nano::mutable_block_visitor
{
public:
	ledger_processor (nano::secure::write_transaction const &, nano::ledger &, std::optional<nano::signature_verification> const & verification);

	void send_block (nano::send_block &) override;
	void receive_block (nano::receive_block &) override;
	void open_block (nano::open_block &) override;
	void change_block (nano::change_block &) override;
	void state_block (nano::state_block &) override;

	void state_block_impl (nano::state_block &);
	void epoch_block_impl (nano::state_block &);

public:
	nano::secure::write_transaction const & transaction;
	nano::ledger & ledger;

	/// Verdict obtained ahead of processing, reused when it covers exactly the signature the ledger would check
	std::optional<nano::signature_verification> verification;

	/// Result of the block processing, set after the visitor is applied to a block
	nano::block_status result{ nano::block_status::invalid };

private:
	bool validate_epoch_block (nano::state_block const & block);

	/// True when the signature does not verify against the signer, taking the answer from the upfront verdict when it names this signer
	bool validate_signature (nano::account const & signer, nano::block_hash const & hash, nano::signature const & signature) const;

	// Returns 1 + max(deps' topo_height) or 0 (unindexed sentinel) when the index is disabled or any dependency is itself unindexed
	uint64_t topology_height (std::shared_ptr<nano::block> const & dep1, std::shared_ptr<nano::block> const & dep2 = nullptr) const;
};
}