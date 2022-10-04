#include <nano/node/ssl/ssl_classes.hpp>
#include <nano/node/ssl/ssl_error.hpp>
#include <nano/node/ssl/ssl_functions.hpp>
#include <nano/node/ssl/ssl_ptr.hpp>

#include <exception>

#include <openssl/err.h>

namespace nano::ssl
{
std::string getLastOpenSslError () // NOLINT(misc-no-recursion)
{
	static thread_local bool hasBeenCalled = false;

	try
	{
		RecursiveCallGuard recursiveCallGuard{ hasBeenCalled };

		const auto bioMethod = ConstBioMethodPtr::make (BIO_s_mem ());
		const auto bio = BioPtr::make (BIO_new (*bioMethod));

		ERR_print_errors (*bio);

		auto result = readFromBio (bio);
		if (result.empty ())
		{
			return "getLastOpenSslError: no error";
		}

		return result;
	}
	catch (const std::exception & ex)
	{
		return std::string{ "getLastOpenSslError: " } + ex.what ();
	}
}

}
