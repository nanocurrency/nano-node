#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/assert.hpp>
#include <nano/lib/keypair.hpp>

// Create a new random keypair
nano::keypair::keypair ()
{
	random_pool::generate_block (prv.bytes.data (), prv.bytes.size ());
	pub = nano::pub_key (prv);
}

// Create a keypair given a private key
nano::keypair::keypair (nano::raw_key && prv_a) :
	prv (std::move (prv_a))
{
	pub = nano::pub_key (prv);
}

// Create a keypair given a hex string of the private key
nano::keypair::keypair (std::string const & prv_a)
{
	[[maybe_unused]] auto error (prv.decode_hex (prv_a));
	debug_assert (!error);
	pub = nano::pub_key (prv);
}
