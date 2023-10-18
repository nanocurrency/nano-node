#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/locks.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cpptoml
{
class table;
}

namespace nano
{
class tomlconfig;
namespace ipc
{
	struct enum_hash
	{
		template <typename T>
		constexpr typename std::enable_if<std::is_enum<T>::value, std::size_t>::type
		operator() (T s) const noexcept
		{
			return static_cast<std::size_t> (s);
		}
	};

	/**
	 * Permissions come in roughly two forms: api permissions (one for every api we expose) and
	 * higher level resource permissions. We define a permission per api because a common use case is to
	 * allow a specific set of RPCs. The higher level resource permissions makes it easier to
	 * grant access to groups of operations or resources. An API implementation will typically check
	 * against the corresponding api permission (such as api_account_weight), but may also allow
	 * resource permissions (such as account_query).
	 */
	enum class access_permission
	{
		invalid,
		/** Unrestricted access to the node, suitable for debugging and development */
		unrestricted,
		api_account_weight,
		api_service_register,
		api_service_stop,
		api_topic_service_stop,
		api_topic_confirmation,
		/** Query account information */
		account_query,
		/** Epoch upgrade */
		epoch_upgrade,
		/** All service operations */
		service,
		/** All wallet operations */
		wallet,
		/** Non-mutable wallet operations */
		wallet_read,
		/** Mutable wallet operations */
		wallet_write,
		/** Seed change */
		wallet_seed_change
	};

	/** A subject is a user or role with a set of permissions */
	class access_subject
	{
	public:
		std::unordered_set<nano::ipc::access_permission, enum_hash> permissions;
		virtual ~access_subject () = default;
		virtual void clear ();
	};

	/** Permissions can be organized into roles */
	class access_role final : public access_subject
	{
	public:
		std::string id;
	};

	/** A user with credentials and a set of permissions (either directly or through roles) */
	class access_user final : public access_subject
	{
	public:
		/* User credentials, serving as the id */
		std::string id;
		std::vector<nano::ipc::access_role> roles;
		void clear () override;
	};

	/**
	 * Constructs a user/role/permission domain model from config-access.toml, and
	 * allows permissions for a user to be checked.
	 * @note This class is thread safe
	 */
	class access final
	{
	public:
		bool has_access (std::string const & credentials_a, nano::ipc::access_permission permission_a) const;
		bool has_access_to_all (std::string const & credentials_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const;
		bool has_access_to_oneof (std::string const & credentials_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const;
		nano::error deserialize_toml (nano::tomlconfig &);

	private:
		/** Process allow and deny entries for the given subject */
		void set_effective_permissions (nano::ipc::access_subject & subject_a, std::shared_ptr<cpptoml::table> const & config_subject_a);

		/** Clear current users, roles and default permissions */
		void clear ();

		std::unordered_map<std::string, nano::ipc::access_user> users;
		std::unordered_map<std::string, nano::ipc::access_role> roles;

		/**
		 * Default user with a basic set of permissions. Additional users will derive the permissions
		 * from the default user (unless "bare" is true in the access config file)
		 */
		access_user default_user;
		/** The config can be externally reloaded and concurrently accessed */
		mutable nano::mutex mutex;
	};

	nano::error read_access_config_toml (std::filesystem::path const & data_path_a, nano::ipc::access & config_a);
}
}
