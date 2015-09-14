#include <rai/node.hpp>
#include <rai/cli/daemon.hpp>

#include <argon2.h>

#include <boost/program_options.hpp>

#include <emmintrin.h>

#include <ed25519-donna/ed25519.h>

class xorshift128
{
public:
    uint64_t s[ 2 ];
    
    uint64_t next(void) {
        uint64_t s1 = s[ 0 ];
        const uint64_t s0 = s[ 1 ];
        s[ 0 ] = s0;
        s1 ^= s1 << 23; // a
        return ( s[ 1 ] = ( s1 ^ s0 ^ ( s1 >> 17 ) ^ ( s0 >> 26 ) ) ) + s0; // b, c
    }
};

class xorshift1024
{
public:
    uint64_t s[ 16 ];
    int p;
    
    uint64_t next(void) {
        uint64_t s0 = s[ p ];
        uint64_t s1 = s[ p = ( p + 1 ) & 15 ];
        s1 ^= s1 << 31; // a
        s1 ^= s1 >> 11; // b
        s0 ^= s0 >> 30; // c
        return ( s[ p ] = s0 ^ s1 ) * 1181783497276652981LL;
    }
};

void fill_128_reference (void * data)
{
    xorshift128 rng;
    rng.s [0] = 1;
    rng.s [1] = 0;
    for (auto i (reinterpret_cast <uint64_t *> (data)), n (reinterpret_cast <uint64_t *> (data) + 1024 * 1024); i != n; ++i)
    {
        *i = rng.next ();
    }
}

#if 0
void fill_128_sse (void * data)
{
    xorshift128 rng;
    rng.s [0] = 1;
    rng.s [1] = 0;
    for (auto i (reinterpret_cast <__m128i *> (data)), n (reinterpret_cast <__m128i *> (data) + 512 * 1024); i != n; ++i)
    {
        auto v0 (rng.next ());
        auto v1 (rng.next ());
        _mm_store_si128 (i, _mm_set_epi64x (v1, v0));
    }
}
#endif // 0

void fill_1024_reference (void * data)
{
    xorshift1024 rng;
    rng.p = 0;
    rng.s [0] = 1;
    for (auto i (0u); i < 16; ++i)
    {
        rng.s [i] = 0;
    }
    for (auto i (reinterpret_cast <uint64_t *> (data)), n (reinterpret_cast <uint64_t *> (data) + 1024 * 1024); i != n; ++i)
    {
        *i = rng.next ();
    }
}

#if 0
void fill_1024_sse (void * data)
{
    xorshift1024 rng;
    rng.p = 0;
    rng.s [0] = 1;
    for (auto i (0u); i < 16; ++i)
    {
        rng.s [i] = 0;
    }
    for (auto i (reinterpret_cast <__m128i *> (data)), n (reinterpret_cast <__m128i *> (data) + 512 * 1024); i != n; ++i)
    {
        auto v0 (rng.next ());
        auto v1 (rng.next ());
        _mm_store_si128 (i, _mm_set_epi64x (v1, v0));
    }
}

void fill_zero (void * data)
{
    for (auto i (reinterpret_cast <__m128i *> (data)), n (reinterpret_cast <__m128i *> (data) + 512 * 1024); i != n; ++i)
    {
        _mm_store_si128 (i, _mm_setzero_si128 ());
    }
}
#endif // 0

class inactive_node
{
public:
	inactive_node ()
	{
		auto working (rai::working_path ());
		boost::filesystem::create_directories (working);
		auto service (boost::make_shared <boost::asio::io_service> ());
		node = std::make_shared <rai::node> (init, service, 24000,  working, processor, logging, work);
	}
	rai::processor_service processor;
	rai::logging logging;
	rai::node_init init;
	rai::work_pool work;
	std::shared_ptr <rai::node> node;
};

int main (int argc, char * const * argv)
{
	boost::program_options::options_description description ("Command line options");
	description.add_options ()
		("account_base58", boost::program_options::value <std::string> (), "Get base58 account number for the <key>")
		("account_key", "Get the public key for the <account>")
		("daemon", "Start node daemon")
		("key_create", "Generates a random keypair")
		("key_expand", "Derive public key and account number from <key>")
		("wallet_add", "Insert <key> in to <wallet>")
		("wallet_list", "Dumps wallet IDs and public keys")
		("wallet_remove", "Remove <account> from <wallet>")
		("wallet_representative_get", "Prints default representative for <wallet>")
		("wallet_representative_set", "Set <account> as default representative for <wallet>")
		("account", boost::program_options::value <std::string> (), "Defines <account> for other commands, base58")
		("key", boost::program_options::value <std::string> (), "Defines the <key> for other commands, hex")
		("password", boost::program_options::value <std::string> (), "Defines <password> for other commands")
		("wallet", boost::program_options::value <std::string> (), "Defines <wallet> for other commands")
		("debug_bootstrap_generate", "Generate bootstrap sequence of blocks")
		("debug_mass_activity", "Generates fake debug activity")
		("debug_profile_generate", "Profile work generation")
		("debug_profile_verify", "Profile work verification")
		("debug_profile_kdf", "Profile kdf function")
		("debug_verify_profile", "Profile signature verification")
		("debug_xorshift_profile", "Profile xorshift algorithms")
		("help", "Print out options");
	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::parse_command_line(argc, argv, description), vm);
	boost::program_options::notify (vm);
	int result (0);
    if (vm.count ("account_base58") > 0)
    {
		if (vm.count ("key") == 1)
		{
			rai::uint256_union pub;
			pub.decode_hex (vm ["key"].as <std::string> ());
			std::cout << "Account: " << pub.to_base58check () << std::endl;
		}
		else
		{
			std::cerr << "account_base58 comand requires one <key> option";
			result = -1;
		}
    }
	else if (vm.count ("account_key") > 0)
	{
		if (vm.count ("account") == 1)
		{
			rai::uint256_union account;
			account.decode_base58check (vm ["account"].as <std::string> ());
			std::cout << "Hex: " << account.to_string () << std::endl;
		}
		else
		{
			std::cerr << "account_key command requires one <account> option";
			result = -1;
		}
	}
	else if (vm.count ("daemon") > 0)
	{
        rai_daemon::daemon daemon;
        daemon.run ();
	}
    else if (vm.count ("key_create"))
    {
        rai::keypair pair;
        std::cout << "Private: " << pair.prv.to_string () << std::endl << "Public: " << pair.pub.to_string () << std::endl << "Account: " << pair.pub.to_base58check () << std::endl;
    }
	else if (vm.count ("key_expand"))
	{
		if (vm.count ("key") == 1)
		{
			rai::uint256_union prv;
			prv.decode_hex (vm ["key"].as <std::string> ());
			rai::uint256_union pub;
			ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
			std::cout << "Private: " << prv.to_string () << std::endl << "Public: " << pub.to_string () << std::endl << "Account: " << pub.to_base58check () << std::endl;
		}
		else
		{
			std::cerr << "key_expand command requires one <key> option";
			result = -1;
		}
	}
	else if (vm.count ("wallet_add"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm ["wallet"].as <std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm ["password"].as <std::string> ();
				}
				inactive_node node;
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					rai::transaction transaction (wallet->store.environment, nullptr, true);
					wallet->store.enter_password (transaction, password);
					if (wallet->store.valid_password (transaction))
					{
						wallet->store.insert (transaction, vm ["key"].as <std::string> ());
					}
					else
					{
						std::cerr << "Invalid password\n";
						result = -1;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				result = -1;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option";
			result = -1;
		}
	}
	else if (vm.count ("wallet_list"))
	{
		inactive_node node;
		for (auto i (node.node->wallets.items.begin ()), n (node.node->wallets.items.end ()); i != n; ++i)
		{
			std::cout << boost::str (boost::format ("Wallet ID: %1%\n") % i->first.to_string ());
			rai::transaction transaction (i->second->store.environment, nullptr, false);
			for (auto j (i->second->store.begin (transaction)), m (i->second->store.end ()); j != m; ++j)
			{
				std::cout << rai::uint256_union (j->first).to_base58check () << '\n';
			}
		}
	}
	else if (vm.count ("wallet_remove"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("account") == 1)
		{
			inactive_node node;
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm ["wallet"].as <std::string> ()))
			{
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					rai::account account_id;
					if (!account_id.decode_base58check (vm ["account"].as <std::string> ()))
					{
						rai::transaction transaction (wallet->second->store.environment, nullptr, true);
						auto account (wallet->second->store.find (transaction, account_id));
						if (account != wallet->second->store.end ())
						{
							wallet->second->store.erase (transaction, account_id);
						}
						else
						{
							std::cerr << "Account not found in wallet\n";
							result = -1;
						}
					}
					else
					{
						std::cerr << "Invalid account id\n";
						result = -1;
					}
				}
				else
				{
					std::cerr << "Wallet not found\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				result = -1;
			}
		}
		else
		{
			std::cerr << "wallet_remove command requires one <wallet> and one <account> option\n";
			result = -1;
		}
	}
	else if (vm.count ("wallet_representative_get"))
	{
		if (vm.count ("wallet") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm ["wallet"].as <std::string> ()))
			{
				inactive_node node;
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					rai::transaction transaction (wallet->second->store.environment, nullptr, false);
					auto representative (wallet->second->store.representative (transaction));
					std::cout << boost::str (boost::format ("Representative: %1%\n") % representative.to_base58check ());
				}
				else
				{
					std::cerr << "Wallet not found\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				result = -1;
			}
		}
		else
		{
			std::cerr << "wallet_representative_get requires one <wallet> option\n";
			result = -1;
		}
	}
	else if (vm.count ("wallet_representative_set"))
	{
		if (vm.count ("wallet") == 1)
		{
			if (vm.count ("account") == 1)
			{
				rai::uint256_union wallet_id;
				if (!wallet_id.decode_hex (vm ["wallet"].as <std::string> ()))
				{
					rai::account account;
					if (!account.decode_base58check (vm ["account"].as <std::string> ()))
					{
						inactive_node node;
						auto wallet (node.node->wallets.items.find (wallet_id));
						if (wallet != node.node->wallets.items.end ())
						{
							rai::transaction transaction (wallet->second->store.environment, nullptr, true);
							wallet->second->store.representative_set (transaction, account);
						}
						else
						{
							std::cerr << "Wallet not found\n";
							result = -1;
						}
					}
					else
					{
						std::cerr << "Invalid account\n";
						result = -1;
					}
				}
				else
				{
					std::cerr << "Invalid wallet id\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "wallet_representative_set requires one <account> option\n";
				result = -1;
			}
		}
		else
		{
			std::cerr << "wallet_representative_set requires one <wallet> option\n";
			result = -1;
		}
	}
	else if (vm.count ("debug_bootstrap_generate"))
	{
		rai::work_pool work;
        rai::keypair genesis;
        std::cout << "Genesis: " << genesis.prv.to_string () << std::endl << "Public: " << genesis.pub.to_string () << std::endl << "Account: " << genesis.pub.to_base58check () << std::endl;
		rai::keypair landing;
		std::cout << "Landing: " << landing.prv.to_string () << std::endl << "Public: " << landing.pub.to_string () << std::endl << "Account: " << landing.pub.to_base58check () << std::endl;
		for (auto i (0); i != 32; ++i)
		{
			rai::keypair rep;
			std::cout << "Rep" << i << ": " << rep.prv.to_string () << std::endl << "Public: " << rep.pub.to_string () << std::endl << "Account: " << rep.pub.to_base58check () << std::endl;
		}
		rai::uint128_t balance (std::numeric_limits <rai::uint128_t>::max ());
		rai::open_block genesis_block (genesis.pub, genesis.pub, genesis.pub, genesis.prv, genesis.pub, work.generate (genesis.pub));
		std::cout << genesis_block.to_json ();
		rai::block_hash previous (genesis_block.hash ());
		for (auto i (0); i != 8; ++i)
		{
			rai::uint128_t yearly_distribution (rai::uint128_t (1) << (127 - (i == 7 ? 6 : i)));
			auto weekly_distribution (yearly_distribution / 52);
			for (auto j (0); j != 52; ++j)
			{
				assert (balance > weekly_distribution);
				balance = balance < (weekly_distribution * 2) ? 0 : balance - weekly_distribution;
				rai::send_block send (landing.pub, previous, balance, genesis.prv, genesis.pub, work.generate (previous));
				previous = send.hash ();
				std::cout << send.to_json ();
				std::cout.flush ();
			}
		}
	}
    else if (vm.count ("debug_mass_activity"))
    {
        rai::system system (24000, 1);
        system.wallet (0)->insert (rai::test_genesis_key.prv);
        size_t count (10000);
        system.generate_mass_activity (count, *system.nodes [0]);
    }
    else if (vm.count ("debug_profile_kdf"))
    {
		rai::uint256_union result;
		rai::uint256_union salt (0);
		std::string password ("");
        for (; true;)
        {
            auto begin1 (std::chrono::high_resolution_clock::now ());
			auto success (PHS (result.bytes.data (), result.bytes.size (), password.data (), password.size (), salt.bytes.data (), salt.bytes.size (), 1, rai::wallet_store::kdf_work));
			auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
    else if (vm.count ("debug_profile_generate"))
    {
		rai::work_pool work;
        rai::change_block block (0, 0, 0, 0, 0);
        std::cerr << "Starting generation profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords [0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            work.generate (block);
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
    else if (vm.count ("debug_profile_verify"))
    {
		rai::work_pool work;
        rai::change_block block (0, 0, 0, 0, 0);
        std::cerr << "Starting verification profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords [0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            work.work_validate (block);
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
    else if (vm.count ("debug_verify_profile"))
    {
        rai::keypair key;
        rai::uint256_union message;
        rai::uint512_union signature;
        signature = rai::sign_message (key.prv, key.pub, message);
        auto begin (std::chrono::high_resolution_clock::now ());
        for (auto i (0u); i < 1000; ++i)
        {
            rai::validate_message (key.pub, key.prv, signature);
        }
        auto end (std::chrono::high_resolution_clock::now ());
        std::cerr << "Signature verifications " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
    }
#if 0
    else if (vm.count ("debug_xorshift_profile"))
    {
        auto unaligned (new uint8_t [64 * 1024 * 1024 + 16]);
        auto aligned (reinterpret_cast <void *> (reinterpret_cast <uintptr_t> (unaligned) & ~uintptr_t (0xfu)));
        {
            memset (aligned, 0x0, 64 * 1024 * 1024);
            auto begin (std::chrono::high_resolution_clock::now ());
            for (auto i (0u); i < 1000; ++i)
            {
                fill_zero (aligned);
            }
            auto end (std::chrono::high_resolution_clock::now ());
            std::cerr << "Memset " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
        }
        {
            memset (aligned, 0x0, 64 * 1024 * 1024);
            auto begin (std::chrono::high_resolution_clock::now ());
            for (auto i (0u); i < 1000; ++i)
            {
                fill_128_reference (aligned);
            }
            auto end (std::chrono::high_resolution_clock::now ());
            std::cerr << "Ref fill 128 " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
        }
        {
            memset (aligned, 0x0, 64 * 1024 * 1024);
            auto begin (std::chrono::high_resolution_clock::now ());
            for (auto i (0u); i < 1000; ++i)
            {
                fill_1024_reference (aligned);
            }
            auto end (std::chrono::high_resolution_clock::now ());
            std::cerr << "Ref fill 1024 " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
        }
        {
            memset (aligned, 0x0, 64 * 1024 * 1024);
            auto begin (std::chrono::high_resolution_clock::now ());
            for (auto i (0u); i < 1000; ++i)
            {
                fill_128_sse (aligned);
            }
            auto end (std::chrono::high_resolution_clock::now ());
            std::cerr << "SSE fill 128 " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
        }
        {
            memset (aligned, 0x0, 64 * 1024 * 1024);
            auto begin (std::chrono::high_resolution_clock::now ());
            for (auto i (0u); i < 1000; ++i)
            {
                fill_1024_sse (aligned);
            }
            auto end (std::chrono::high_resolution_clock::now ());
            std::cerr << "SSE fill 1024 " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
        }
    }
#endif // 0
    else
    {
		std::cout << description << std::endl;
		result = -1;
    }
    return result;
}
