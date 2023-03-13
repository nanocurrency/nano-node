#pragma once

#include <nano/lib/configbase.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/utility.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>

#include <fstream>

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
/** Manages a node in a boost configuration tree. */
class jsonconfig : public nano::configbase
{
public:
	jsonconfig ();
	jsonconfig (boost::property_tree::ptree & tree_a, std::shared_ptr<nano::error> const & error_a = nullptr);
	nano::error & read (std::filesystem::path const & path_a);
	void write (std::filesystem::path const & path_a);
	void write (std::ostream & stream_a) const;
	void read (std::istream & stream_a);
	void open_or_create (std::fstream & stream_a, std::string const & path_a);
	void create_backup_file (std::filesystem::path const & filepath_a);
	boost::property_tree::ptree const & get_tree ();
	bool empty () const;
	boost::optional<jsonconfig> get_optional_child (std::string const & key_a);
	jsonconfig get_required_child (std::string const & key_a);
	jsonconfig & put_child (std::string const & key_a, nano::jsonconfig & conf_a);
	jsonconfig & replace_child (std::string const & key_a, nano::jsonconfig & conf_a);
	bool has_key (std::string const & key_a);
	jsonconfig & erase (std::string const & key_a);

	/** Set value for the given key. Any existing value will be overwritten. */
	template <typename T>
	jsonconfig & put (std::string const & key, T const & value)
	{
		tree.put (key, value);
		return *this;
	}

	/** Push array element */
	template <typename T>
	jsonconfig & push (T const & value)
	{
		boost::property_tree::ptree entry;
		entry.put ("", value);
		tree.push_back (std::make_pair ("", entry));
		return *this;
	}

	/** Iterate array entries */
	template <typename T>
	jsonconfig & array_entries (std::function<void (T)> callback)
	{
		for (auto & entry : tree)
		{
			callback (entry.second.get<T> (""));
		}
		return *this;
	}

	/** Get optional, using \p default_value if \p key is missing. */
	template <typename T>
	jsonconfig & get_optional (std::string const & key, T & target, T default_value)
	{
		get_config (true, key, target, default_value);
		return *this;
	}

	/**
	 * Get optional value, using the current value of \p target as the default if \p key is missing.
	 * @return May return nano::error_config::invalid_value
	 */
	template <typename T>
	jsonconfig & get_optional (std::string const & key, T & target)
	{
		get_config (true, key, target, target);
		return *this;
	}

	/** Return a boost::optional<T> for the given key */
	template <typename T>
	boost::optional<T> get_optional (std::string const & key)
	{
		boost::optional<T> res;
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
	jsonconfig & get (std::string const & key, T & target)
	{
		get_config (true, key, target, target);
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
	jsonconfig & get_required (std::string const & key, T & target)
	{
		get_config (false, key, target);
		return *this;
	}

	template <typename T>
	jsonconfig & get_required (std::string const & key, T & target, T const & default_value)
	{
		get_config (false, key, target, default_value);
		return *this;
	}

protected:
	template <typename T, typename = std::enable_if_t<nano::is_lexical_castable<T>::value>>
	jsonconfig & get_config (bool optional, std::string key, T & target, T default_value = T ())
	{
		try
		{
			auto val (tree.get<std::string> (key));
			if (!boost::conversion::try_lexical_convert<T> (val, target))
			{
				conditionally_set_error<T> (nano::error_config::invalid_value, optional, key);
			}
		}
		catch (boost::property_tree::ptree_bad_path const &)
		{
			if (!optional)
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

	// boost's lexical cast doesn't handle (u)int8_t
	jsonconfig & get_config (bool optional, std::string key, uint8_t & target, uint8_t default_value = uint8_t ());
	jsonconfig & get_config (bool optional, std::string key, bool & target, bool default_value = false);
	jsonconfig & get_config (bool optional, std::string key, boost::asio::ip::address_v6 & target, boost::asio::ip::address_v6 const & default_value);

private:
	/** The property node being managed */
	boost::property_tree::ptree & tree;
	boost::property_tree::ptree tree_default;

	void write_json (std::fstream & stream);
};
}
