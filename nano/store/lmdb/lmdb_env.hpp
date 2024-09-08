#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/store/component.hpp>
#include <nano/store/lmdb/options.hpp>
#include <nano/store/lmdb/transaction_impl.hpp>

namespace nano::store::lmdb
{
/**
 * RAII wrapper for MDB_env
 */
class env final
{
public:
	env (bool &, std::filesystem::path const &, options options_a = options::make ());
	void init (bool &, std::filesystem::path const &, options options_a = options::make ());
	~env ();
	operator MDB_env * () const;
	MDB_env * environment;
};
} // namespace nano::store::lmdb
