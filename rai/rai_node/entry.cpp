#include <rai/node/node.hpp>
#include <rai/node/testing.hpp>
#include <rai/rai_node/daemon.hpp>

#include <argon2.h>

#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>

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

int main (int argc, char * const * argv)
{
	boost::program_options::options_description description ("Command line options");
	rai::add_node_options (description);
	description.add_options ()
		("help", "Print out options")
		("daemon", "Start node daemon")
		("debug_block_count", "Display the number of block")
		("debug_bootstrap_generate", "Generate bootstrap sequence of blocks")
		("debug_dump_representatives", "List representatives and weights")
		("debug_frontier_count", "Display the number of accounts")
		("debug_mass_activity", "Generates fake debug activity")
		("debug_profile_generate", "Profile work generation")
		("debug_opencl", "OpenCL work generation")
		("debug_profile_verify", "Profile work verification")
		("debug_profile_kdf", "Profile kdf function")
		("debug_verify_profile", "Profile signature verification")
		("debug_xorshift_profile", "Profile xorshift algorithms")
		("platform", boost::program_options::value <std::string> (), "Defines the <platform> for OpenCL commands")
		("device", boost::program_options::value <std::string> (), "Defines <device> for OpenCL command")
		("threads", boost::program_options::value <std::string> (), "Defines <threads> count for OpenCL command");
	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::parse_command_line(argc, argv, description), vm);
	boost::program_options::notify (vm);
	int result (0);
	if (!rai::handle_node_options (vm))
	{
	}
	else if (vm.count ("daemon") > 0)
	{
		boost::filesystem::path data_path;
		if (vm.count ("data_path"))
		{
			data_path = boost::filesystem::path (vm ["data_path"].as <std::string> ());
		}
		else
		{
			data_path = rai::working_path ();
		}
        rai_daemon::daemon daemon;
        daemon.run (data_path);
	}
	else if (vm.count ("debug_block_count"))
	{
		rai::inactive_node node;
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		std::cout << boost::str (boost::format ("Block count: %1%\n") % node.node->store.block_count (transaction).sum ());
	}
	else if (vm.count ("debug_bootstrap_generate"))
	{
		if (vm.count ("key") == 1)
		{
			rai::uint256_union key;
			if (!key.decode_hex (vm ["key"].as <std::string> ()))
			{
				rai::keypair genesis (key.to_string ());
				rai::work_pool work (std::numeric_limits <unsigned>::max (), nullptr);
				std::cout << "Genesis: " << genesis.prv.data.to_string () << std::endl << "Public: " << genesis.pub.to_string () << std::endl << "Account: " << genesis.pub.to_account () << std::endl;
				rai::keypair landing;
				std::cout << "Landing: " << landing.prv.data.to_string () << std::endl << "Public: " << landing.pub.to_string () << std::endl << "Account: " << landing.pub.to_account () << std::endl;
				for (auto i (0); i != 32; ++i)
				{
					rai::keypair rep;
					std::cout << "Rep" << i << ": " << rep.prv.data.to_string () << std::endl << "Public: " << rep.pub.to_string () << std::endl << "Account: " << rep.pub.to_account () << std::endl;
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
						rai::send_block send (previous, landing.pub, balance, genesis.prv, genesis.pub, work.generate (previous));
						previous = send.hash ();
						std::cout << send.to_json ();
						std::cout.flush ();
					}
				}
			}
			else
			{
				std::cerr << "Invalid key\n";
				result = -1;
			}
		}
		else
		{
			std::cerr << "Bootstrapping requires one <key> option\n";
			result = -1;
		}
	}
	else if (vm.count ("debug_dump_representatives"))
	{
		rai::inactive_node node;
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		rai::uint128_t total;
		for (auto i(node.node->store.representation_begin(transaction)), n(node.node->store.representation_end()); i != n; ++i)
		{
			rai::account account(i->first);
			auto amount (node.node->store.representation_get(transaction, account));
			total += amount;
			std::cout << boost::str(boost::format("%1% %2% %3%\n") % account.to_account () % amount.convert_to <std::string> () % total.convert_to<std::string> ());
		}
		std::map <rai::account, rai::uint128_t> calculated;
		for (auto i (node.node->store.latest_begin (transaction)), n (node.node->store.latest_end ()); i != n; ++i)
		{
			rai::account account (i->first);
			rai::account_info info (i->second);
			rai::block_hash rep_block (node.node->ledger.representative_calculated (transaction, info.head));
			std::unique_ptr <rai::block> block (node.node->store.block_get (transaction, rep_block));
			calculated [block->representative()] += info.balance.number();
		}
		total = 0;
		for (auto i (calculated.begin ()), n (calculated.end ()); i != n; ++i)
		{
			total += i->second;
			std::cout << boost::str(boost::format("%1% %2% %3%\n") % i->first.to_account () % i->second.convert_to <std::string> () % total.convert_to<std::string> ());
		}
	}
	else if (vm.count ("debug_frontier_count"))
	{
		rai::inactive_node node;
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		std::cout << boost::str (boost::format ("Frontier count: %1%\n") % node.node->store.frontier_count (transaction));
	}
    else if (vm.count ("debug_mass_activity"))
    {
        rai::system system (24000, 1);
        size_t count (1000000);
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
		rai::work_pool work (std::numeric_limits <unsigned>::max (), nullptr);
        rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
        std::cerr << "Starting generation profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords [0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            block.block_work_set (work.generate (block.root ()));
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
    else if (vm.count ("debug_opencl"))
    {
		bool error (false);
		rai::opencl_environment environment (error);
		if (!error)
		{
			unsigned short platform (0);
			if (vm.count ("platform") == 1)
			{
				try {
					platform = boost::lexical_cast <unsigned short> (vm ["platform"].as <std::string> ());
				}
				catch (boost::bad_lexical_cast & e ) {
					std::cerr << "Invalid platform id\n";
					result = -1;
				}
			}
			unsigned short device (0);
			if (vm.count ("device") == 1)
			{
				try {
					device = boost::lexical_cast <unsigned short> (vm ["device"].as <std::string> ());
				}
				catch (boost::bad_lexical_cast & e ) {
					std::cerr << "Invalid device id\n";
					result = -1;
				}
			}
			unsigned threads (1024 * 1024);
			if (vm.count ("threads") == 1)
			{
				try {
					threads = boost::lexical_cast <unsigned> (vm ["threads"].as <std::string> ());
				}
				catch (boost::bad_lexical_cast & e ) {
					std::cerr << "Invalid threads count\n";
					result = -1;
				}
			}
			if (!result)
			{
				error |= platform >= environment.platforms.size ();
				if (!error)
				{
					error |= device >= environment.platforms[platform].devices.size ();
					if (!error)
					{
						rai::logging logging;
						logging.init (rai::unique_path ());
						auto work (rai::opencl_work::create (true, {platform, device, threads}, logging));
						rai::work_pool work_pool (std::numeric_limits <unsigned>::max (), std::move (work));
						rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
						std::cerr << boost::str (boost::format ("Starting OpenCL generation profiling. Platform: %1%. Device: %2%. Threads: %3%\n") % platform % device % threads);
						for (uint64_t i (0); true; ++i)
						{
							block.hashables.previous.qwords [0] += 1;
							auto begin1 (std::chrono::high_resolution_clock::now ());
							block.block_work_set (work_pool.generate (block.root ()));
							auto end1 (std::chrono::high_resolution_clock::now ());
							std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
						}
					}
					else
					{
						std::cout << "Not available device id\n" << std::endl;
						result = -1;
					}
				}
				else
				{
					std::cout << "Not available platform id\n" << std::endl;
					result = -1;
				}
			}
		}
		else
		{
			std::cout << "Error initializing OpenCL" << std::endl;
			result = -1;
		}
    }
    else if (vm.count ("debug_profile_verify"))
    {
		rai::work_pool work (std::numeric_limits <unsigned>::max (), nullptr);
        rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
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
            rai::validate_message (key.pub, message, signature);
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
