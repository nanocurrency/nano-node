#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>

#include <filesystem>
#include <memory>

namespace nano
{

// Returns the database path for the given backend type
std::filesystem::path database_path_for_backend (std::filesystem::path const & base_path, database_backend backend_type);

std::unique_ptr<nano::store::backend> make_backend (
nano::logger &,
std::filesystem::path const & path,
bool add_db_postfix,
nano::node_config const & node_config);

std::unique_ptr<nano::store::ledger_store> make_store (
nano::logger &,
nano::stats &,
std::filesystem::path const & path,
nano::ledger_constants & constants,
bool read_only,
bool add_db_postfix,
nano::node_config const & node_config);
}
