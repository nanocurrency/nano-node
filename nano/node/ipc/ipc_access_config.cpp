#include <nano/lib/tomlconfig.hpp>
#include <nano/node/ipc/ipc_access_config.hpp>

#include <boost/algorithm/string.hpp>

namespace
{
/** Convert string to permission */
nano::ipc::access_permission from_string (std::string permission)
{
	if (permission == "unrestricted")
		return nano::ipc::access_permission::unrestricted;
	if (permission == "api_account_weight")
		return nano::ipc::access_permission::api_account_weight;
	if (permission == "api_service_register")
		return nano::ipc::access_permission::api_service_register;
	if (permission == "api_service_stop")
		return nano::ipc::access_permission::api_service_stop;
	if (permission == "api_topic_service_stop")
		return nano::ipc::access_permission::api_topic_service_stop;
	if (permission == "api_topic_confirmation")
		return nano::ipc::access_permission::api_topic_confirmation;
	if (permission == "account_query")
		return nano::ipc::access_permission::account_query;
	if (permission == "epoch_upgrade")
		return nano::ipc::access_permission::epoch_upgrade;
	if (permission == "service")
		return nano::ipc::access_permission::service;
	if (permission == "wallet")
		return nano::ipc::access_permission::wallet;
	if (permission == "wallet_read")
		return nano::ipc::access_permission::wallet_read;
	if (permission == "wallet_write")
		return nano::ipc::access_permission::wallet_write;
	if (permission == "wallet_seed_change")
		return nano::ipc::access_permission::wallet_seed_change;

	return nano::ipc::access_permission::invalid;
}
}

void nano::ipc::access::set_effective_permissions (nano::ipc::access_subject & subject_a, std::shared_ptr<cpptoml::table> const & config_subject_a)
{
	std::string allow_l (config_subject_a->get_as<std::string> ("allow").value_or (""));
	std::vector<std::string> allow_strings_l;
	boost::split (allow_strings_l, allow_l, boost::is_any_of (","));
	for (auto const & permission : allow_strings_l)
	{
		if (!permission.empty ())
		{
			auto permission_enum = from_string (boost::trim_copy (permission));
			if (permission_enum != nano::ipc::access_permission::invalid)
			{
				subject_a.permissions.insert (permission_enum);
			}
		}
	}

	std::string deny_l (config_subject_a->get_as<std::string> ("deny").value_or (""));
	std::vector<std::string> deny_strings_l;
	boost::split (deny_strings_l, deny_l, boost::is_any_of (","));
	for (auto const & permission : deny_strings_l)
	{
		if (!permission.empty ())
		{
			auto permission_enum = from_string (boost::trim_copy (permission));
			if (permission_enum != nano::ipc::access_permission::invalid)
			{
				subject_a.permissions.erase (permission_enum);
			}
		}
	}
}

void nano::ipc::access::clear ()
{
	users.clear ();
	roles.clear ();

	// Create default user. The node operator can add additional roles
	// and permissions to the default user by adding a toml [[user]] entry
	// without an id (or set it to the empty string).
	// The default permissions can be overriden by marking the default user
	// as bare, and then set specific permissions.
	default_user.clear ();
	default_user.id = "";

	// The default set of permissions. A new insert should be made as new safe
	// api's or resource permissions are made.
	default_user.permissions.insert (nano::ipc::access_permission::api_account_weight);
}

nano::error nano::ipc::access::deserialize_toml (nano::tomlconfig & toml)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	clear ();

	nano::error error;
	if (toml.has_key ("role"))
	{
		auto get_role = [this] (std::shared_ptr<cpptoml::table> const & role_a) {
			nano::ipc::access_role role;
			std::string id_l (role_a->get_as<std::string> ("id").value_or (""));
			role.id = id_l;
			set_effective_permissions (role, role_a);
			return role;
		};

		auto role_l = toml.get_tree ()->get ("role");
		if (role_l->is_table ())
		{
			auto role = get_role (role_l->as_table ());
			if (role_l->as_table ()->contains ("deny"))
			{
				error.set ("Only users can have deny entries");
			}
			else
			{
				roles.emplace (role.id, role);
			}
		}
		else if (role_l->is_table_array ())
		{
			for (auto & table : *role_l->as_table_array ())
			{
				if (table->contains ("deny"))
				{
					error.set ("Only users can have deny entries");
				}

				auto role = get_role (table);
				roles.emplace (role.id, role);
			}
		}
	}

	if (!error && toml.has_key ("user"))
	{
		auto get_user = [this, &error] (std::shared_ptr<cpptoml::table> const & user_a) {
			nano::ipc::access_user user;
			user.id = user_a->get_as<std::string> ("id").value_or ("");
			// Check bare flag. The tomlconfig parser stringifies values, so we must retrieve as string.
			bool is_bare = user_a->get_as<std::string> ("bare").value_or ("false") == "true";

			// Adopt all permissions from the roles. This must be done before setting user permissions, since
			// the user config may add deny-entries.
			std::string roles_l (user_a->get_as<std::string> ("roles").value_or (""));
			std::vector<std::string> role_strings_l;
			boost::split (role_strings_l, roles_l, boost::is_any_of (","));
			for (auto const & role : role_strings_l)
			{
				auto role_id (boost::trim_copy (role));
				if (!role_id.empty ())
				{
					auto match = roles.find (role_id);
					if (match != roles.end ())
					{
						user.permissions.insert (match->second.permissions.begin (), match->second.permissions.end ());
					}
					else
					{
						error.set ("Unknown role: " + role_id);
					}
				}
			}

			// A user with the bare flag does not inherit default permissions
			if (!is_bare)
			{
				user.permissions.insert (default_user.permissions.begin (), default_user.permissions.end ());
			}

			set_effective_permissions (user, user_a);

			return user;
		};

		auto user_l = toml.get_tree ()->get ("user");
		if (user_l->is_table ())
		{
			auto user = get_user (user_l->as_table ());
			users.emplace (user.id, user);
		}
		else if (user_l->is_table_array ())
		{
			for (auto & table : *user_l->as_table_array ())
			{
				auto user = get_user (table);
				if (user.id.empty () && users.size () > 0)
				{
					// This is a requirement because other users inherit permissions from the default user
					error.set ("Changes to the default user must appear before other users in the access config file");
					break;
				}
				users.emplace (user.id, user);
			}
		}
	}

	// Add default user if it wasn't present in the config file
	if (users.find ("") == users.end ())
	{
		users.emplace (default_user.id, default_user);
	}

	return error;
}

bool nano::ipc::access::has_access (std::string const & credentials_a, nano::ipc::access_permission permssion_a) const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	bool permitted = false;
	auto user = users.find (credentials_a);
	if (user != users.end ())
	{
		permitted = user->second.permissions.find (permssion_a) != user->second.permissions.end ();
		if (!permitted)
		{
			permitted = user->second.permissions.find (nano::ipc::access_permission::unrestricted) != user->second.permissions.end ();
		}
	}
	return permitted;
}

bool nano::ipc::access::has_access_to_all (std::string const & credentials_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	bool permitted = false;
	auto user = users.find (credentials_a);
	if (user != users.end ())
	{
		for (auto permission : permissions_a)
		{
			permitted = user->second.permissions.find (permission) != user->second.permissions.end ();
			if (!permitted)
			{
				break;
			}
		}
	}
	return permitted;
}

bool nano::ipc::access::has_access_to_oneof (std::string const & credentials_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	bool permitted = false;
	auto user = users.find (credentials_a);
	if (user != users.end ())
	{
		for (auto permission : permissions_a)
		{
			permitted = user->second.permissions.find (permission) != user->second.permissions.end ();
			if (permitted)
			{
				break;
			}
		}
		if (!permitted)
		{
			permitted = user->second.permissions.find (nano::ipc::access_permission::unrestricted) != user->second.permissions.end ();
		}
	}
	return permitted;
}

void nano::ipc::access_subject::clear ()
{
	permissions.clear ();
}

void nano::ipc::access_user::clear ()
{
	access_subject::clear ();
	roles.clear ();
}

namespace nano
{
namespace ipc
{
	nano::error read_access_config_toml (std::filesystem::path const & data_path_a, nano::ipc::access & config_a)
	{
		nano::error error;
		auto toml_config_path = nano::get_access_toml_config_path (data_path_a);

		nano::tomlconfig toml;
		if (std::filesystem::exists (toml_config_path))
		{
			error = toml.read (toml_config_path);
		}
		else
		{
			std::stringstream config_overrides_stream;
			config_overrides_stream << std::endl;
			toml.read (config_overrides_stream);
		}

		if (!error)
		{
			error = config_a.deserialize_toml (toml);
		}

		return error;
	}
}
}
