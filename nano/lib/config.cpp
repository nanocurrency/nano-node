#include <iostream>
#include <limits>
#include <nano/core_test/testutil.hpp>
#include <nano/lib/config.hpp>

nano::nano_networks nano::network_params::active_network = nano::nano_networks::ACTIVE_NETWORK;

namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F"; // xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
char const * test_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "9680625b39d3363d",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
	})%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F",
	"representative": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
	"account": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
	"work": "000000000f0aaeeb",
	"signature": "A726490E3325E4FA59C1C900D5B6EEBB15FE13D99F49D475B93F0AACC5635929A0614CF3892764A04D1C6732A0D716FFEB254D4154C6F544D11E6630F201450B"
	})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA",
	"representative": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"account": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"work": "62f05417dd3fb691",
	"signature": "9F0C933C8ADE004D808EA1985FA746A7E95BA2A38F867640F53EC8F180BDFE9E2C1268DEAD7C2664F356E37ABA362BC58E46DBA03E523A7B5A19E4B6EB12BB02"
	})%%%";
}

nano::network_params::network_params () :
network_params (nano::network_params::active_network)
{
}

nano::network_params::network_params (nano::nano_networks network_a) :
current_network (network_a), ledger (*this), voting (*this), node (*this), portmapping (*this), bootstrap (*this)
{
	// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
	uint64_t constexpr publish_test_threshold = 0xff00000000000000;
	uint64_t constexpr publish_full_threshold = 0xffffffc000000000;
	publish_threshold = is_test_network () ? publish_test_threshold : publish_full_threshold;

	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_test_work = 8;
	kdf_work = is_test_network () ? kdf_test_work : kdf_full_work;

	default_node_port = is_live_network () ? 7075 : 54000;
	default_rpc_port = is_live_network () ? 7076 : 55000;
	request_interval_ms = is_test_network () ? 10 : 16000;
	header_magic_number = is_test_network () ? std::array<uint8_t, 2>{ { 'R', 'A' } } : is_beta_network () ? std::array<uint8_t, 2>{ { 'R', 'B' } } : std::array<uint8_t, 2>{ { 'R', 'C' } };
}

nano::ledger_constants::ledger_constants (nano::network_params & params_a) :
ledger_constants (params_a.network ())
{
}

nano::ledger_constants::ledger_constants (nano::nano_networks network_a) :
zero_key ("0"),
test_genesis_key (test_private_key_data),
nano_test_account (test_public_key_data),
nano_beta_account (beta_public_key_data),
nano_live_account (live_public_key_data),
nano_test_genesis (test_genesis_data),
nano_beta_genesis (beta_genesis_data),
nano_live_genesis (live_genesis_data),
genesis_account (network_a == nano::nano_networks::nano_test_network ? nano_test_account : network_a == nano::nano_networks::nano_beta_network ? nano_beta_account : nano_live_account),
genesis_block (network_a == nano::nano_networks::nano_test_network ? nano_test_genesis : network_a == nano::nano_networks::nano_beta_network ? nano_beta_genesis : nano_live_genesis),
genesis_amount (std::numeric_limits<nano::uint128_t>::max ()),
burn_account (0),
not_an_account_m (0)
{
}

nano::account const & nano::ledger_constants::not_an_account ()
{
	if (not_an_account_m.is_zero ())
	{
		// Randomly generating these mean no two nodes will ever have the same sentinel values which protects against some insecure algorithms
		nano::random_pool::generate_block (not_an_account_m.bytes.data (), not_an_account_m.bytes.size ());
	}
	return not_an_account_m;
}

nano::node_constants::node_constants (nano::network_params & params_a)
{
	period = params_a.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (60);
	preconfigured_keepalive_interval = params_a.is_test_network () ? std::chrono::seconds (1) : std::chrono::minutes (15);
	cutoff = period * 5;
	syn_cookie_cutoff = std::chrono::seconds (5);
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = params_a.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	peer_interval = search_pending_interval;
	unchecked_cleaning_interval = std::chrono::hours (2);
	process_confirmed_interval = params_a.is_test_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	max_weight_samples = params_a.is_live_network () ? 4032 : 864;
	weight_period = 5 * 60; // 5 minutes
}

nano::voting_constants::voting_constants (nano::network_params & params_a)
{
	max_cache = params_a.is_test_network () ? 2 : 1000;
	generator_delay = params_a.is_test_network () ? std::chrono::milliseconds (10) : std::chrono::milliseconds (500);
}

nano::portmapping_constants::portmapping_constants (nano::network_params & params_a)
{
	mapping_timeout = params_a.is_test_network () ? 53 : 3593;
	check_timeout = params_a.is_test_network () ? 17 : 53;
}

nano::bootstrap_constants::bootstrap_constants (nano::network_params & params_a)
{
	lazy_max_pull_blocks = params_a.is_test_network () ? 2 : 512;
}

/** Called by gtest_main to enforce test network */
void force_nano_test_network ()
{
	nano::network_params::set_active_network (nano::nano_networks::nano_test_network);
}

/* Convenience constants for core_test which is always on the test network */
namespace
{
nano::ledger_constants test_constants (nano::nano_networks::nano_test_network);
}

nano::keypair const & nano::zero_key (test_constants.zero_key);
nano::keypair const & nano::test_genesis_key (test_constants.test_genesis_key);
nano::account const & nano::nano_test_account (test_constants.nano_test_account);
std::string const & nano::nano_test_genesis (test_constants.nano_test_genesis);
nano::account const & nano::genesis_account (test_constants.genesis_account);
std::string const & nano::genesis_block (test_constants.genesis_block);
nano::uint128_t const & nano::genesis_amount (test_constants.genesis_amount);
nano::account const & nano::burn_account (test_constants.burn_account);
