#include <nano/node/ssl/ssl_recognize_rep_keys.hpp>

namespace nano::ssl
{
bool is_ca_public_key_valid (const nano::ssl::BufferView & public_key)
{
	// TODO: obviously this has to be plugged differently into the SSL code,
	//       but I separated it out in this way so that it is visible where
	//       the decision is made whether a public key is known or not

	return true;
}

}
