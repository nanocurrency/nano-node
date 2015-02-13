#include <rai/node.hpp>
#include <rai/cli/daemon.hpp>

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

int main (int argc, char * const * argv)
{
    boost::program_options::options_description description ("Command line options");
    description.add_options ()
        ("help", "Print out options")
        ("debug_activity", "Generates fake debug activity")
        ("profile_work", "Profile the work function")
        ("profile_kdf", "Profile kdf function")
        ("generate_key", "Generates a random keypair")
		("expand_key", boost::program_options::value <std::string> (), "Derive public key and account number from private key")
        ("get_account", boost::program_options::value <std::string> (), "Get base58check encoded account from public key")
        ("xorshift_profile", "Profile xorshift algorithms")
        ("verify_profile", "Profile signature verification");
    boost::program_options::variables_map vm;
    boost::program_options::store (boost::program_options::parse_command_line(argc, argv, description), vm);
    boost::program_options::notify (vm);
    int result (0);
    if (vm.count ("help"))
    {
        std::cout << description << std::endl;
        result = -1;
    }
    else if (vm.count ("debug_activity"))
    {
        rai::system system (24000, 1);
        system.wallet (0)->store.insert (rai::test_genesis_key.prv);
        size_t count (10000);
        system.generate_mass_activity (count, *system.nodes [0]);
    }
    else if (vm.count ("generate_key"))
    {
        rai::keypair pair;
        std::cout << "Private: " << pair.prv.to_string () << std::endl << "Public: " << pair.pub.to_string () << std::endl << "Account: " << pair.pub.to_base58check () << std::endl;
    }
	else if (vm.count ("expand_key"))
	{
		rai::uint256_union prv;
		prv.decode_hex (vm ["expand_key"].as <std::string> ());
		rai::uint256_union pub;
		ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
		std::cout << "Private: " << prv.to_string () << std::endl << "Public: " << pub.to_string () << std::endl << "Account: " << pub.to_base58check () << std::endl;
	}
    else if (vm.count ("get_account"))
    {
        rai::uint256_union pub;
        pub.decode_hex (vm ["get_account"].as <std::string> ());
        std::cout << "Account: " << pub.to_base58check () << std::endl;
    }
    else if (vm.count ("profile_work"))
    {
        rai::change_block block (0, 0, 0, 0);
        std::cerr << "Starting\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords [0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            rai::work_generate (block);
            auto end1 (std::chrono::high_resolution_clock::now ());
            rai::work_validate (block);
            auto end2 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("Generation time: %1%us validation time: %2%us\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count () % std::chrono::duration_cast <std::chrono::microseconds> (end2 - end1).count ());
        }
    }
    else if (vm.count ("profile_kdf"))
    {
        rai::kdf kdf (rai::wallet_store::kdf_work);
        for (auto i (kdf.data.get ()), n (kdf.data.get () + kdf.entries); i != n; ++i)
        {
            *i = 0;
        }
        for (uint64_t i (0); true; ++i)
        {
            auto begin1 (std::chrono::high_resolution_clock::now ());
            auto value (kdf.generate ("", i));
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
#if 0
    else if (vm.count ("xorshift_profile"))
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
    else if (vm.count ("verify_profile"))
    {
        rai::keypair key;
        rai::uint256_union message;
        rai::uint512_union signature;
        rai::sign_message (key.prv, key.pub, message, signature);
        auto begin (std::chrono::high_resolution_clock::now ());
        for (auto i (0u); i < 1000; ++i)
        {
            rai::validate_message (key.pub, key.prv, signature);
        }
        auto end (std::chrono::high_resolution_clock::now ());
        std::cerr << "Signature verifications " << std::chrono::duration_cast <std::chrono::microseconds> (end - begin).count () << std::endl;
    }
    else
    {
        rai_daemon::daemon daemon;
        daemon.run ();
    }
    return result;
}