#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/component.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/variant/get.hpp>

#include <limits>
#include <queue>

#include <crypto/ed25519-donna/ed25519.h>
#include <cryptopp/words.h>
#include <magic_enum.hpp>

nano::networks nano::network_constants::active_network = nano::networks::ACTIVE_NETWORK;

namespace
{
char const * dev_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * dev_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "259A438A8F9F9226130C84D902C237AF3E57C0981C7D709C288046B110D8C8AC"; // nano_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
std::string const test_public_key_data = nano::get_env_or_default ("NANO_TEST_GENESIS_PUB", "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED"); // nano_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j
char const * dev_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "7b42a00ee91d5810",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
    })%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "259A438A8F9F9226130C84D902C237AF3E57C0981C7D709C288046B110D8C8AC",	
	"representative": "nano_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",
	"account": "nano_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",	
	"work": "e87a3ce39b43b84c",
	"signature": "BC588273AC689726D129D3137653FB319B6EE6DB178F97421D11D075B46FD52B6748223C8FF4179399D35CB1A8DF36F759325BD2D3D4504904321FAFB71D7602"
	})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA",
	"representative": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"account": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"work": "62f05417dd3fb691",
	"signature": "9F0C933C8ADE004D808EA1985FA746A7E95BA2A38F867640F53EC8F180BDFE9E2C1268DEAD7C2664F356E37ABA362BC58E46DBA03E523A7B5A19E4B6EB12BB02"
    })%%%";

std::string const test_genesis_data = nano::get_env_or_default ("NANO_TEST_GENESIS_BLOCK", R"%%%({
	"type": "open",
	"source": "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED",
	"representative": "nano_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"account": "nano_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"work": "bc1ef279c1a34eb1",
	"signature": "15049467CAEE3EC768639E8E35792399B6078DA763DA4EBA8ECAD33B0EDC4AF2E7403893A5A602EB89B978DABEF1D6606BB00F3C0EE11449232B143B6E07170E"
    })%%%");

std::shared_ptr<nano::block> parse_block_from_genesis_data (std::string const & genesis_data_a)
{
	boost::property_tree::ptree tree;
	std::stringstream istream (genesis_data_a);
	boost::property_tree::read_json (istream, tree);
	return nano::deserialize_block_json (tree);
}
}

nano::keypair nano::dev::genesis_key{ dev_private_key_data };
nano::network_params nano::dev::network_params{ nano::networks::nano_dev_network };
nano::ledger_constants & nano::dev::constants{ nano::dev::network_params.ledger };
std::shared_ptr<nano::block> & nano::dev::genesis = nano::dev::constants.genesis;

nano::network_params::network_params (nano::networks network_a) :
	work{ network_a == nano::networks::nano_live_network ? nano::work_thresholds::publish_full : network_a == nano::networks::nano_beta_network ? nano::work_thresholds::publish_beta
		: network_a == nano::networks::nano_test_network                                                                                        ? nano::work_thresholds::publish_test
																																				: nano::work_thresholds::publish_dev },
	network{ work, network_a },
	ledger{ work, network_a },
	voting{ network },
	node{ network },
	portmapping{ network },
	bootstrap{ network }
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_dev_work = 8;
	kdf_work = network.is_dev_network () ? kdf_dev_work : kdf_full_work;
}

nano::ledger_constants::ledger_constants (nano::work_thresholds & work, nano::networks network_a) :
	work{ work },
	zero_key ("0"),
	nano_beta_account (beta_public_key_data),
	nano_live_account (live_public_key_data),
	nano_test_account (test_public_key_data),
	nano_dev_genesis (parse_block_from_genesis_data (dev_genesis_data)),
	nano_beta_genesis (parse_block_from_genesis_data (beta_genesis_data)),
	nano_live_genesis (parse_block_from_genesis_data (live_genesis_data)),
	nano_test_genesis (parse_block_from_genesis_data (test_genesis_data)),
	genesis (network_a == nano::networks::nano_dev_network ? nano_dev_genesis : network_a == nano::networks::nano_beta_network ? nano_beta_genesis
	: network_a == nano::networks::nano_test_network                                                                           ? nano_test_genesis
																															   : nano_live_genesis),
	genesis_amount{ std::numeric_limits<nano::uint128_t>::max () },
	burn_account{}
{
	nano_beta_genesis->sideband_set (nano::block_sideband (nano_beta_genesis->account_field ().value (), 0, std::numeric_limits<nano::uint128_t>::max (), 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));
	nano_dev_genesis->sideband_set (nano::block_sideband (nano_dev_genesis->account_field ().value (), 0, std::numeric_limits<nano::uint128_t>::max (), 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));
	nano_live_genesis->sideband_set (nano::block_sideband (nano_live_genesis->account_field ().value (), 0, std::numeric_limits<nano::uint128_t>::max (), 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));
	nano_test_genesis->sideband_set (nano::block_sideband (nano_test_genesis->account_field ().value (), 0, std::numeric_limits<nano::uint128_t>::max (), 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));

	nano::link epoch_link_v1;
	char const * epoch_message_v1 ("epoch v1 block");
	strncpy ((char *)epoch_link_v1.bytes.data (), epoch_message_v1, epoch_link_v1.bytes.size ());
	epochs.add (nano::epoch::epoch_1, genesis->account (), epoch_link_v1);

	nano::link epoch_link_v2;
	nano::account nano_live_epoch_v2_signer;
	auto error (nano_live_epoch_v2_signer.decode_account ("nano_3qb6o6i1tkzr6jwr5s7eehfxwg9x6eemitdinbpi7u8bjjwsgqfj4wzser3x"));
	debug_assert (!error);
	auto epoch_v2_signer (network_a == nano::networks::nano_dev_network ? nano::dev::genesis_key.pub : network_a == nano::networks::nano_beta_network ? nano_beta_account
	: network_a == nano::networks::nano_test_network                                                                                                  ? nano_test_account
																																					  : nano_live_epoch_v2_signer);
	char const * epoch_message_v2 ("epoch v2 block");
	strncpy ((char *)epoch_link_v2.bytes.data (), epoch_message_v2, epoch_link_v2.bytes.size ());
	epochs.add (nano::epoch::epoch_2, epoch_v2_signer, epoch_link_v2);
}

nano::hardened_constants & nano::hardened_constants::get ()
{
	static hardened_constants instance{};
	return instance;
}

nano::hardened_constants::hardened_constants () :
	not_an_account{},
	random_128{}
{
	nano::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	nano::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

nano::node_constants::node_constants (nano::network_constants & network_constants)
{
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	unchecked_cleaning_interval = std::chrono::minutes (30);
	process_confirmed_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	max_weight_samples = (network_constants.is_live_network () || network_constants.is_test_network ()) ? 4032 : 288;
	weight_period = 5 * 60; // 5 minutes
}

nano::voting_constants::voting_constants (nano::network_constants & network_constants) :
	max_cache{ network_constants.is_dev_network () ? 256U : 128U * 1024 },
	delay{ network_constants.is_dev_network () ? 1 : 15 }
{
}

nano::portmapping_constants::portmapping_constants (nano::network_constants & network_constants)
{
	lease_duration = std::chrono::seconds (1787); // ~30 minutes
	health_check_period = std::chrono::seconds (53);
}

nano::bootstrap_constants::bootstrap_constants (nano::network_constants & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_dev_network () ? 2 : 512;
	lazy_min_pull_blocks = network_constants.is_dev_network () ? 1 : 32;
	frontier_retry_limit = network_constants.is_dev_network () ? 2 : 16;
	lazy_retry_limit = network_constants.is_dev_network () ? 2 : frontier_retry_limit * 4;
	lazy_destinations_retry_limit = network_constants.is_dev_network () ? 1 : frontier_retry_limit / 4;
	gap_cache_bootstrap_start_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (5) : std::chrono::milliseconds (30 * 1000);
	default_frontiers_age_seconds = network_constants.is_dev_network () ? 1 : 24 * 60 * 60; // 1 second for dev network, 24 hours for live/beta
}

// Create a new random keypair
nano::keypair::keypair ()
{
	random_pool::generate_block (prv.bytes.data (), prv.bytes.size ());
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
nano::keypair::keypair (nano::raw_key && prv_a) :
	prv (std::move (prv_a))
{
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
nano::keypair::keypair (std::string const & prv_a)
{
	[[maybe_unused]] auto error (prv.decode_hex (prv_a));
	debug_assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

nano::unchecked_info::unchecked_info (std::shared_ptr<nano::block> const & block_a) :
	block (block_a),
	modified_m (nano::seconds_since_epoch ())
{
}

void nano::unchecked_info::serialize (nano::stream & stream_a) const
{
	debug_assert (block != nullptr);
	nano::serialize_block (stream_a, *block);
	nano::write (stream_a, modified_m);
}

bool nano::unchecked_info::deserialize (nano::stream & stream_a)
{
	block = nano::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			nano::read (stream_a, modified_m);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

uint64_t nano::unchecked_info::modified () const
{
	return modified_m;
}

nano::endpoint_key::endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a) :
	address (address_a), network_port (boost::endian::native_to_big (port_a))
{
}

std::array<uint8_t, 16> const & nano::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t nano::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

nano::confirmation_height_info::confirmation_height_info (uint64_t confirmation_height_a, nano::block_hash const & confirmed_frontier_a) :
	height (confirmation_height_a),
	frontier (confirmed_frontier_a)
{
}

void nano::confirmation_height_info::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, height);
	nano::write (stream_a, frontier);
}

bool nano::confirmation_height_info::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, height);
		nano::read (stream_a, frontier);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

nano::block_info::block_info (nano::account const & account_a, nano::amount const & balance_a) :
	account (account_a),
	balance (balance_a)
{
}

nano::wallet_id nano::random_wallet_id ()
{
	nano::wallet_id wallet_id;
	nano::uint256_union dummy_secret;
	random_pool::generate_block (dummy_secret.bytes.data (), dummy_secret.bytes.size ());
	ed25519_publickey (dummy_secret.bytes.data (), wallet_id.bytes.data ());
	return wallet_id;
}

nano::unchecked_key::unchecked_key (nano::hash_or_account const & dependency) :
	unchecked_key{ dependency, 0 }
{
}

nano::unchecked_key::unchecked_key (nano::hash_or_account const & previous_a, nano::block_hash const & hash_a) :
	previous (previous_a.as_block_hash ()),
	hash (hash_a)
{
}

nano::unchecked_key::unchecked_key (nano::uint512_union const & union_a) :
	previous (union_a.uint256s[0].number ()),
	hash (union_a.uint256s[1].number ())
{
}

bool nano::unchecked_key::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, previous.bytes);
		nano::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::unchecked_key::operator== (nano::unchecked_key const & other_a) const
{
	return previous == other_a.previous && hash == other_a.hash;
}

bool nano::unchecked_key::operator< (nano::unchecked_key const & other_a) const
{
	return previous != other_a.previous ? previous < other_a.previous : hash < other_a.hash;
}

nano::block_hash const & nano::unchecked_key::key () const
{
	return previous;
}

std::string_view nano::to_string (nano::block_status code)
{
	return magic_enum::enum_name (code);
}

nano::stat::detail nano::to_stat_detail (nano::block_status code)
{
	auto value = magic_enum::enum_cast<nano::stat::detail> (magic_enum::enum_name (code));
	debug_assert (value);
	return value.value_or (nano::stat::detail{});
}
