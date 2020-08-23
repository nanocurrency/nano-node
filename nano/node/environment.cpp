#include <nano/node/environment.hpp>

#include <algorithm>
#include <thread>

nano::environment::environment (boost::filesystem::path const & path_a) :
path{ path_a },
alarm{ ctx },
work_impl{ std::make_unique<nano::work_pool> (std::max (std::thread::hardware_concurrency (), 1u)) },
work{ *work_impl }
{
	/*
	 * @warning May throw a filesystem exception
	 */
	boost::system::error_code error_chmod;
	boost::filesystem::create_directories (path_a);
	nano::set_secure_perm_directory (path_a, error_chmod);
}
