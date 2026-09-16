#pragma once

#include <nano/lib/configbase.hpp>
#include <nano/lib/utility.hpp>

#include <boost/lexical_cast.hpp>

#include <filesystem>
#include <optional>
#include <unordered_set>

#include <cpptoml.h>

namespace boost
{
namespace asio
{
	namespace ip
	{
		class address_v6;
	}
}
}

namespace nano
{
class error;

/**
 * Manages a table in a toml configuration table hierarchy.
 * Every value, array or child table handed out through the getters is recorded as read, so that after
 * deserialization the keys nobody asked for can be listed with unknown_keys.
 */
class tomlconfig : public nano::configbase
{
public:
	using consumed_set = std::unordered_set<cpptoml::base const *>;

	tomlconfig ();
	tomlconfig (std::shared_ptr<cpptoml::table> const & tree_a, std::shared_ptr<nano::error> const & error_a = nullptr, std::shared_ptr<consumed_set> const & consumed_a = nullptr);

	void doc (std::string const & key, std::string const & doc);
	/** Parses the file at \p path_a, which must exist; the file is never created */
	nano::error & read (std::filesystem::path const & path_a);
	nano::error & read (std::istream & stream_a);
	/**
	 * Applies a `key=value` override on top of the document. Dotted keys address nested tables, which are created
	 * as needed, and an existing value at the key is replaced. Arrays are written as `key=[a,b]`.
	 */
	nano::error & apply_override (std::string const & override_a);
	nano::error & apply_overrides (std::vector<std::string> const & overrides_a);
	/** Writes the document to \p path_a, replacing any previous content; a new file gets restricted permissions */
	void write (std::filesystem::path const & path_a);
	void write (std::ostream & stream_a) const;
	/** Raw access to the table; the whole subtree counts as read since the caller may inspect any of it */
	std::shared_ptr<cpptoml::table> get_tree ();
	bool empty () const;
	/**
	 * Dotted paths, relative to this table, of the entries no getter has read. A table none of whose entries
	 * were read is listed as a whole rather than entry by entry.
	 */
	std::vector<std::string> unknown_keys () const;
	std::optional<tomlconfig> get_optional_child (std::string const & key_a);
	tomlconfig get_required_child (std::string const & key_a);
	tomlconfig & put_child (std::string const & key_a, nano::tomlconfig & conf_a);
	tomlconfig & replace_child (std::string const & key_a, nano::tomlconfig & conf_a);
	bool has_key (std::string const & key_a);
	tomlconfig & erase (std::string const & key_a);
	std::shared_ptr<cpptoml::array> create_array (std::string const & key, std::optional<char const *> documentation_a);
	void erase_default_values (tomlconfig & defaults_a);
	std::string to_string (bool comment_values);
	std::string merge_defaults (nano::tomlconfig & current_config, nano::tomlconfig & default_config);

	/** Set value for the given key. Any existing value will be overwritten. */
	template <typename T>
	tomlconfig & put (std::string const & key, T const & value, std::optional<char const *> documentation_a = std::nullopt)
	{
		tree->insert (key, value);
		if (documentation_a)
		{
			doc (key, *documentation_a);
		}
		return *this;
	}

	/**
	 * Push array element
	 * @param key Array element key. Qualified (dotted) keys are not supported for arrays so this must be called on the correct tomlconfig node.
	 */
	template <typename T>
	tomlconfig & push (std::string const & key, T const & value)
	{
		if (!has_key (key))
		{
			auto arr = cpptoml::make_array ();
			tree->insert (key, arr);
		}
		auto arr = tree->get_qualified (key)->as_array ();
		arr->push_back (value);
		return *this;
	}

	/**
	 * Iterate array entries.
	 * @param key Array element key. Qualified (dotted) keys are not supported for arrays so this must be called on the correct tomlconfig node.
	 */
	template <typename T>
	tomlconfig & array_entries_required (std::string const & key, std::function<void (T)> callback)
	{
		if (tree->contains_qualified (key))
		{
			mark_read (key);
			auto items = tree->get_qualified_array_of<T> (key);
			if (!items)
			{
				conditionally_set_error<T> (nano::error_config::invalid_value, false, key);
				return *this;
			}
			for (auto & item : *items)
			{
				callback (item);
			}
		}
		else
		{
			conditionally_set_error<T> (nano::error_config::missing_value, false, key);
		}
		return *this;
	}

	/** Get optional, using \p default_value if \p key is missing. */
	template <typename T>
	tomlconfig & get_optional (std::string const & key, T & target, T default_value)
	{
		get_config (true, key, target, default_value);
		return *this;
	}

	/**
	 * Get optional value, using the current value of \p target as the default if \p key is missing.
	 * @return May return nano::error_config::invalid_value
	 */
	template <typename T>
	tomlconfig & get_optional (std::string const & key, T & target)
	{
		get_config (true, key, target, target);
		return *this;
	}

	/** Return a std::optional<T> for the given key */
	template <typename T>
	std::optional<T> get_optional (std::string const & key)
	{
		std::optional<T> res;
		if (has_key (key))
		{
			T target{};
			get_config (true, key, target, target);
			res = target;
		}
		return res;
	}

	/** Get value, using the current value of \p target as the default if \p key is missing. */
	template <typename T>
	tomlconfig & get (std::string const & key, T & target)
	{
		get_config (true, key, target, target);
		return *this;
	}

	/** Get chrono duration */
	template <typename Duration>
	tomlconfig & get_duration (std::string const & key, Duration & target)
	{
		uint64_t value = target.count ();
		get (key, value);
		target = Duration{ value };
		return *this;
	}

	/**
	 * Get value of optional key. Use default value of data type if missing.
	 */
	template <typename T>
	T get (std::string const & key)
	{
		T target{};
		get_config (true, key, target, target);
		return target;
	}

	/**
	 * Get required value.
	 * @note May set nano::error_config::missing_value if \p key is missing, nano::error_config::invalid_value if value is invalid.
	 */
	template <typename T>
	tomlconfig & get_required (std::string const & key, T & target)
	{
		get_config (false, key, target);
		return *this;
	}

	template <typename T>
	tomlconfig & get_required (std::string const & key, T & target, T const & default_value)
	{
		get_config (false, key, target, default_value);
		return *this;
	}

	template <typename T>
	std::vector<std::pair<std::string, T>> get_values ()
	{
		std::vector<std::pair<std::string, T>> result;
		for (auto & entry : *tree)
		{
			T target{};
			get_config (true, entry.first, target, target);
			result.push_back ({ entry.first, target });
		}
		return result;
	}

protected:
	/** Records the entry at \p key as read; qualified (dotted) keys are resolved */
	void mark_read (std::string const & key);
	/**
	 * The stringified value at \p key, or nullopt if the key is missing or holds a table or an array.
	 * Every value is parsed as a string and converted on access by the callers.
	 */
	std::optional<std::string> get_value (std::string const & key);

	template <typename T, typename = std::enable_if_t<nano::is_lexical_castable<T>::value>>
	tomlconfig & get_config (bool optional, std::string const & key, T & target, T default_value = T ())
	{
		try
		{
			if (tree->contains_qualified (key))
			{
				auto val = get_value (key);
				if (!val || !boost::conversion::try_lexical_convert<T> (*val, target))
				{
					conditionally_set_error<T> (nano::error_config::invalid_value, optional, key);
				}
			}
			else if (!optional)
			{
				conditionally_set_error<T> (nano::error_config::missing_value, optional, key);
			}
			else
			{
				target = default_value;
			}
		}
		catch (std::runtime_error & ex)
		{
			conditionally_set_error<T> (ex, optional, key);
		}

		return *this;
	}

	tomlconfig & get_config (bool optional, std::string const & key, uint8_t & target, uint8_t default_value = uint8_t ());
	tomlconfig & get_config (bool optional, std::string const & key, bool & target, bool default_value = false);
	tomlconfig & get_config (bool optional, std::string key, boost::asio::ip::address_v6 & target, boost::asio::ip::address_v6 const & default_value);

private:
	/** The config node being managed */
	std::shared_ptr<cpptoml::table> tree;
	/** Entries read so far, shared with every child table handed out */
	std::shared_ptr<consumed_set> consumed;

	/** Compare two stringified configs, remove keys where values are equal */
	void erase_defaults (std::shared_ptr<cpptoml::table> const & base, std::shared_ptr<cpptoml::table> const & other, std::shared_ptr<cpptoml::table> const & update_target);
};
}
