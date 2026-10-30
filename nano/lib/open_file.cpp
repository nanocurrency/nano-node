#include <nano/lib/open_file.hpp>

#include <cerrno>
#include <system_error>

nano::result<std::ifstream> nano::open_file (std::filesystem::path const & path)
{
	std::ifstream file{ path };
	if (!file)
	{
		// The open underneath leaves its reason in errno
		std::error_code const reason{ errno, std::generic_category () };
		return nano::error{ reason }.set_message ("Could not open " + path.string () + ": " + reason.message ());
	}
	return file;
}
