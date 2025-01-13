#include <celerix/boost/asio/ip/tcp.hpp>
#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/env.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/store/component.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/variant/get.hpp>

#include <limits>
#include <queue>

#include <crypto/ed25519-donna/ed25519.h>
#include <cryptopp/words.h>

celerix::networks celerix::network_constants::active_network = celerix::networks::ACTIVE_NETWORK;
celerix::uint128_t GENESIS_AMOUNT = 21700000000; //21.7 billion

namespace
{
char const * dev_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * dev_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "259A438A8F9F9226130C84D902C237AF3E57C0981C7D709C288046B110D8C8AC"; // celerix_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
std::string const test_public_key_data = celerix::env::get ("CELERIX_TEST_GENESIS_PUB").value_or ("45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED"); // celerix_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j

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
	"representative": "celerix_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",
	"account": "celerix_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",	
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

std::string const test_genesis_data = celerix::env::get ("CELERIX_TEST_GENESIS_BLOCK").value_or (R"%%%({
	"type": "open",
	"source": "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED",
	"representative": "celerix_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"account": "celerix_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"work": "bc1ef279c1a34eb1",
	"signature": "15049467CAEE3EC768639E8E35792399B6078DA763DA4EBA8ECAD33B0EDC4AF2E7403893A5A602EB89B978DABEF1D6606BB00F3C0EE11449232B143B6E07170E"
    })%%%");

std::shared_ptr<celerix::block> parse_block_from_genesis_data (std::string const & genesis_data_a)
{
	boost::property_tree::ptree tree;
	std::stringstream istream (genesis_data_a);
	boost::property_tree::read_json (istream, tree);
	return celerix::deserialize_block_json (tree);
}
}

/*
 * celerix::dev constants
 */

celerix::keypair celerix::dev::genesis_key{ dev_private_key_data };
celerix::network_params celerix::dev::network_params{ celerix::networks::celerix_dev_network };
celerix::ledger_constants & celerix::dev::constants{ celerix::dev::network_params.ledger };
std::shared_ptr<celerix::block> & celerix::dev::genesis = celerix::dev::constants.genesis;

/*
 *
 */

celerix::work_thresholds const & celerix::work_thresholds_for_network (celerix::networks network_type)
{
	switch (network_type)
	{
		case celerix::networks::celerix_live_network:
			return celerix::work_thresholds::publish_full;
		case celerix::networks::celerix_beta_network:
			return celerix::work_thresholds::publish_beta;
		case celerix::networks::celerix_dev_network:
			return celerix::work_thresholds::publish_dev;
		case celerix::networks::celerix_test_network:
			return celerix::work_thresholds::publish_test;
		default:
			release_assert (false, "invalid network");
	}
}

celerix::network_params::network_params (celerix::networks network_type) :
	work{ work_thresholds_for_network (network_type) },
	network{ work, network_type },
	ledger{ work, network_type },
	voting{ network },
	node{ network },
	portmapping{ network },
	bootstrap{ network }
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_dev_work = 8;
	kdf_work = network.is_dev_network () ? kdf_dev_work : kdf_full_work;
}

/*
 *
 */

celerix::ledger_constants::ledger_constants (celerix::work_thresholds & work, celerix::networks network_type) :
	work{ work },
	zero_key{ "0" },
	celerix_beta_account{ beta_public_key_data },
	celerix_live_account{ live_public_key_data },
	celerix_test_account{ test_public_key_data },
	celerix_dev_genesis{ parse_block_from_genesis_data (dev_genesis_data) },
	celerix_beta_genesis{ parse_block_from_genesis_data (beta_genesis_data) },
	celerix_live_genesis{ parse_block_from_genesis_data (live_genesis_data) },
	celerix_test_genesis{ parse_block_from_genesis_data (test_genesis_data) },
	genesis_amount{ GENESIS_AMOUNT },
	burn_account{ celerix::account{ 0 } }
{
	celerix_beta_genesis->sideband_set (celerix::block_sideband{
	/* account */ celerix_beta_genesis->account_field ().value (),
	/* successor (block_hash) */ celerix::block_hash{ 0 },
	/* balance (amount) */ celerix::amount{ GENESIS_AMOUNT },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ celerix::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ celerix::epoch::epoch_0 });

	celerix_dev_genesis->sideband_set (celerix::block_sideband{
	/* account */ celerix_dev_genesis->account_field ().value (),
	/* successor (block_hash) */ celerix::block_hash{ 0 },
	/* balance (amount) */ celerix::amount{ GENESIS_AMOUNT },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ celerix::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ celerix::epoch::epoch_0 });

	celerix_live_genesis->sideband_set (celerix::block_sideband{
	/* account */ celerix_live_genesis->account_field ().value (),
	/* successor (block_hash) */ celerix::block_hash{ 0 },
	/* balance (amount) */ celerix::amount{ GENESIS_AMOUNT },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ celerix::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ celerix::epoch::epoch_0 });

	celerix_test_genesis->sideband_set (celerix::block_sideband{
	/* account */ celerix_test_genesis->account_field ().value (),
	/* successor (block_hash) */ celerix::block_hash{ 0 },
	/* balance (amount) */ celerix::amount{ GENESIS_AMOUNT },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ celerix::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ celerix::epoch::epoch_0 });

	celerix::account epoch_v2_signer;
	switch (network_type)
	{
		case networks::celerix_dev_network:
		{
			genesis = celerix_dev_genesis;
			epoch_v2_signer = celerix::dev::genesis_key.pub;
		}
		break;
		case networks::celerix_live_network:
		{
			genesis = celerix_live_genesis;
			epoch_v2_signer = celerix::account::from_account ("celerix_3qb6o6i1tkzr6jwr5s7eehfxwg9x6eemitdinbpi7u8bjjwsgqfj4wzser3x");
		}
		break;
		case networks::celerix_beta_network:
		{
			genesis = celerix_beta_genesis;
			epoch_v2_signer = celerix_beta_account;
		}
		break;
		case networks::celerix_test_network:
		{
			genesis = celerix_test_genesis;
			epoch_v2_signer = celerix_test_account;
		}
		break;
		default:
			release_assert (false, "invalid network");
			break;
	}
	release_assert (genesis != nullptr);
	release_assert (!epoch_v2_signer.is_zero ());

	celerix::link const epoch_link_v1{ "epoch v1 block" };
	epochs.add (celerix::epoch::epoch_1, genesis->account (), epoch_link_v1);

	celerix::link const epoch_link_v2{ "epoch v2 block" };
	epochs.add (celerix::epoch::epoch_2, epoch_v2_signer, epoch_link_v2);
}

/*
 *
 */

celerix::hardened_constants & celerix::hardened_constants::get ()
{
	static hardened_constants instance{};
	return instance;
}

celerix::hardened_constants::hardened_constants () :
	not_an_account{},
	random_128{}
{
	celerix::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	celerix::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

/*
 *
 */

celerix::node_constants::node_constants (celerix::network_constants & network_constants)
{
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	unchecked_cleaning_interval = std::chrono::minutes (30);
	process_confirmed_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	weight_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::minutes (5);
	weight_cutoff = (network_constants.is_live_network () || network_constants.is_test_network ()) ? std::chrono::weeks (2) : std::chrono::days (1);
}

/*
 *
 */

celerix::voting_constants::voting_constants (celerix::network_constants & network_constants) :
	max_cache{ network_constants.is_dev_network () ? 256U : 128U * 1024 },
	delay{ network_constants.is_dev_network () ? 1 : 15 }
{
}

/*
 *
 */

celerix::portmapping_constants::portmapping_constants (celerix::network_constants & network_constants)
{
	lease_duration = std::chrono::seconds (1787); // ~30 minutes
	health_check_period = std::chrono::seconds (53);
}

/*
 *
 */

celerix::bootstrap_constants::bootstrap_constants (celerix::network_constants & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_dev_network () ? 2 : 512;
	lazy_min_pull_blocks = network_constants.is_dev_network () ? 1 : 32;
	frontier_retry_limit = network_constants.is_dev_network () ? 2 : 16;
	lazy_retry_limit = network_constants.is_dev_network () ? 2 : frontier_retry_limit * 4;
	lazy_destinations_retry_limit = network_constants.is_dev_network () ? 1 : frontier_retry_limit / 4;
	gap_cache_bootstrap_start_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (5) : std::chrono::milliseconds (30 * 1000);
	default_frontiers_age_seconds = network_constants.is_dev_network () ? 1 : 24 * 60 * 60; // 1 second for dev network, 24 hours for live/beta
}

/*
 * keypair
 */

// Create a new random keypair
celerix::keypair::keypair ()
{
	random_pool::generate_block (prv.bytes.data (), prv.bytes.size ());
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
celerix::keypair::keypair (celerix::raw_key && prv_a) :
	prv (std::move (prv_a))
{
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
celerix::keypair::keypair (std::string const & prv_a)
{
	[[maybe_unused]] auto error (prv.decode_hex (prv_a));
	debug_assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

/*
 * unchecked_info
 */

celerix::unchecked_info::unchecked_info (std::shared_ptr<celerix::block> const & block_a) :
	block (block_a),
	modified_m (celerix::seconds_since_epoch ())
{
}

void celerix::unchecked_info::serialize (celerix::stream & stream_a) const
{
	debug_assert (block != nullptr);
	celerix::serialize_block (stream_a, *block);
	celerix::write (stream_a, modified_m);
}

bool celerix::unchecked_info::deserialize (celerix::stream & stream_a)
{
	block = celerix::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			celerix::read (stream_a, modified_m);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

uint64_t celerix::unchecked_info::modified () const
{
	return modified_m;
}

/*
 * endpoint_key
 */

celerix::endpoint_key::endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a) :
	address (address_a),
	network_port (boost::endian::native_to_big (port_a))
{
}

celerix::endpoint_key::endpoint_key (celerix::endpoint const & endpoint_a) :
	endpoint_key (endpoint_a.address ().to_v6 ().to_bytes (), endpoint_a.port ())
{
}

std::array<uint8_t, 16> const & celerix::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t celerix::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

celerix::endpoint celerix::endpoint_key::endpoint () const
{
	return { boost::asio::ip::address_v6 (address), port () };
}

/*
 * confirmation_height_info
 */

celerix::confirmation_height_info::confirmation_height_info (uint64_t confirmation_height_a, celerix::block_hash const & confirmed_frontier_a) :
	height (confirmation_height_a),
	frontier (confirmed_frontier_a)
{
}

void celerix::confirmation_height_info::serialize (celerix::stream & stream_a) const
{
	celerix::write (stream_a, height);
	celerix::write (stream_a, frontier);
}

bool celerix::confirmation_height_info::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		celerix::read (stream_a, height);
		celerix::read (stream_a, frontier);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

celerix::block_info::block_info (celerix::account const & account_a, celerix::amount const & balance_a) :
	account (account_a),
	balance (balance_a)
{
}

celerix::wallet_id celerix::random_wallet_id ()
{
	celerix::wallet_id wallet_id;
	celerix::uint256_union dummy_secret;
	random_pool::generate_block (dummy_secret.bytes.data (), dummy_secret.bytes.size ());
	ed25519_publickey (dummy_secret.bytes.data (), wallet_id.bytes.data ());
	return wallet_id;
}

/*
 * unchecked_key
 */

celerix::unchecked_key::unchecked_key (celerix::hash_or_account const & dependency) :
	unchecked_key{ dependency, 0 }
{
}

celerix::unchecked_key::unchecked_key (celerix::hash_or_account const & previous_a, celerix::block_hash const & hash_a) :
	previous (previous_a.as_block_hash ()),
	hash (hash_a)
{
}

celerix::unchecked_key::unchecked_key (celerix::uint512_union const & union_a) :
	previous (union_a.uint256s[0].number ()),
	hash (union_a.uint256s[1].number ())
{
}

bool celerix::unchecked_key::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		celerix::read (stream_a, previous.bytes);
		celerix::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool celerix::unchecked_key::operator== (celerix::unchecked_key const & other_a) const
{
	return previous == other_a.previous && hash == other_a.hash;
}

bool celerix::unchecked_key::operator< (celerix::unchecked_key const & other_a) const
{
	return previous != other_a.previous ? previous < other_a.previous : hash < other_a.hash;
}

celerix::block_hash const & celerix::unchecked_key::key () const
{
	return previous;
}

/*
 *
 */

std::string_view celerix::to_string (celerix::block_status code)
{
	return celerix::enum_util::name (code);
}

celerix::stat::detail celerix::to_stat_detail (celerix::block_status code)
{
	return celerix::enum_util::cast<celerix::stat::detail> (code);
}
