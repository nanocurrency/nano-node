#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>

namespace nano
{
class ledger_processor final : public nano::mutable_block_visitor
{
public:
	ledger_processor (nano::secure::write_transaction const &, nano::ledger &, nano::signature_verification = nano::signature_verification::unknown);

	void send_block (nano::send_block &) override;
	void receive_block (nano::receive_block &) override;
	void open_block (nano::open_block &) override;
	void change_block (nano::change_block &) override;
	void state_block (nano::state_block &) override;

	void state_block_impl (nano::state_block &);
	void epoch_block_impl (nano::state_block &);

	nano::secure::write_transaction const & transaction;
	nano::ledger & ledger;
	nano::signature_verification const verification;
	nano::block_status result{ nano::block_status::invalid };

private:
	bool validate_epoch_block (nano::state_block const & block);

	// Signature check that consumes the pre-computed verification result when available, falling back to validate_message () otherwise.
	// Returns true if the signature is invalid, matching validate_message () semantics.
	bool validate_signature (nano::account const & signer, nano::block_hash const & hash, nano::signature const & signature, bool epoch_signer = false) const;

	// Returns 1 + max(deps' topo_height) or 0 (unindexed sentinel) when the index is disabled or any dependency is itself unindexed
	uint64_t topology_height (std::shared_ptr<nano::block> const & dep1, std::shared_ptr<nano::block> const & dep2 = nullptr) const;
};
}