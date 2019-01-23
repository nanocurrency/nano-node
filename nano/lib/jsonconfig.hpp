#pragma once

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <nano/lib/errors.hpp>
#include <nano/lib/utility.hpp>

namespace nano
{
/** Type trait to determine if T is compatible with boost's lexical_cast */
template <class T>
struct is_lexical_castable : std::integral_constant<bool,
                             (std::is_default_constructible<T>::value && (boost::has_right_shift<std::basic_istream<wchar_t>, T>::value || boost::has_right_shift<std::basic_istream<char>, T>::value))>
{
};

/* Type descriptions are used to automatically construct configuration error messages */
// clang-format off
template <typename T> inline std::string type_desc (void) { return "an unknown type"; }
template <> inline std::string type_desc<int8_t> (void) { return "an integer between -128 and 127"; }
template <> inline std::string type_desc<uint8_t> (void) { return "an integer between 0 and 255"; }
template <> inline std::string type_desc<int16_t> (void) { return "an integer between -32768 and 32767"; }
template <> inline std::string type_desc<uint16_t> (void) { return "an integer between 0 and 65535"; }
template <> inline std::string type_desc<int32_t> (void) { return "a 32-bit signed integer"; }
template <> inline std::string type_desc<uint32_t> (void) { return "a 32-bit unsigned integer"; }
template <> inline std::string type_desc<int64_t> (void) { return "a 64-bit signed integer"; }
template <> inline std::string type_desc<uint64_t> (void) { return "a 64-bit unsigned integer"; }
template <> inline std::string type_desc<float> (void) { return "a single precision floating point number"; }
template <> inline std::string type_desc<double> (void) { return "a double precison floating point number"; }
template <> inline std::string type_desc<char> (void) { return "a character"; }
template <> inline std::string type_desc<std::string> (void) { return "a string"; }
template <> inline std::string type_desc<bool> (void) { return "a boolean"; }
template <> inline std::string type_desc<boost::asio::ip::address_v6> (void) { return "an IP address"; }
// clang-format on

/** Manages a node in a boost configuration tree. */
class jsonconfig : public nano::error_aware<>
{
public:
	jsonconfig () :
	tree (tree_default)
	{
		error = std::make_shared<nano::error> ();
	}

	jsonconfig (boost::property_tree::ptree & tree_a, std::shared_ptr<nano::error> error_a = nullptr) :
	tree (tree_a), error (error_a)
	{
		if (!error)
		{
			error = std::make_shared<nano::error> ();
		}
	}

	/**
	 * Reads a json object from the stream and if it was changed, write the object back to the stream.
	 * @return nano::error&, including a descriptive error message if the config file is malformed.
	 */
	template <typename T>
	nano::error & read_and_update (T & object, boost::filesystem::path const & path_a)
	{
		std::fstream stream;
		open_or_create (stream, path_a.string ());
		if (!stream.fail ())
		{
			try
			{
				boost::property_tree::read_json (stream, tree);
			}
			catch (std::runtime_error const & ex)
			{
				auto pos (stream.tellg ());
				if (pos != std::streampos (0))
				{
					*error = ex;
				}
			}
			stream.close ();
			if (!*error)
			{
				auto updated (false);
				*error = object.deserialize_json (updated, *this);
				if (!*error && updated)
				{
					stream.open (path_a.string (), std::ios_base::out | std::ios_base::trunc);
					try
					{
						boost::property_tree::write_json (stream, tree);
					}
					catch (std::runtime_error const & ex)
					{
						*error = ex;
					}
					stream.close ();
				}
			}
		}
		return *error;
	}

	void write (boost::filesystem::path const & path_a)
	{
		std::fstream stream;
		open_or_create (stream, path_a.string ());
		write (stream);
	}

	void write (std::ostream & stream_a) const
	{
		boost::property_tree::write_json (stream_a, tree);
	}

	void read (std::istream & stream_a)
	{
		boost::property_tree::read_json (stream_a, tree);
	}

	/** Open configuration file, create if necessary */
	void open_or_create (std::fstream & stream_a, std::string const & path_a)
	{
		if (!boost::filesystem::exists (path_a))
		{
			// Create temp stream to first create the file
			std::ofstream stream (path_a);

			// Set permissions before opening otherwise Windows only has read permissions
			nano::set_secure_perm_file (path_a);
		}

		stream_a.open (path_a);
	}

	/** Returns the boost property node managed by this instance */
	boost::property_tree::ptree const & get_tree ()
	{
		return tree;
	}

	/** Returns true if the property tree node is empty */
	bool empty () const
	{
		return tree.empty ();
	}

	boost::optional<jsonconfig> get_optional_child (std::string const & key_a)
	{
		boost::optional<jsonconfig> child_config;
		auto child = tree.get_child_optional (key_a);
		if (child)
		{
			return jsonconfig (child.get (), error);
		}
		return child_config;
	}

	jsonconfig get_required_child (std::string const & key_a)
	{
		auto child = tree.get_child_optional (key_a);
		if (!child)
		{
			*error = nano::error_config::missing_value;
			error->set_message ("Missing configuration node: " + key_a);
		}
		return child ? jsonconfig (child.get (), error) : *this;
	}

	jsonconfig & put_child (std::string const & key_a, nano::jsonconfig & conf_a)
	{
		tree.add_child (key_a, conf_a.get_tree ());
		return *this;
	}

	jsonconfig & replace_child (std::string const & key_a, nano::jsonconfig & conf_a)
	{
		tree.erase (key_a);
		put_child (key_a, conf_a);
		return *this;
	}

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
	jsonconfig & array_entries (std::function<void(T)> callback)
	{
		for (auto & entry : tree)
		{
			callback (entry.second.get<T> (""));
		}
		return *this;
	}

	/** Returns true if \p key_a is present */
	bool has_key (std::string const & key_a)
	{
		return tree.find (key_a) != tree.not_found ();
	}

	/** Erase the property of given key */
	jsonconfig & erase (std::string const & key_a)
	{
		tree.erase (key_a);
		return *this;
	}

	/** Get optional, using \p default_value if \p key is missing. */
	template <typename T>
	jsonconfig & get_optional (std::string const & key, T & target, T default_value)
	{
		get_config<T> (true, key, target, default_value);
		return *this;
	}

	/**
	 * Get optional value, using the current value of \p target as the default if \p key is missing.
	 * @return May return nano::error_config::invalid_value
	 */
	template <typename T>
	jsonconfig & get_optional (std::string const & key, T & target)
	{
		get_config<T> (true, key, target, target);
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
			get_config<T> (true, key, target, target);
			res = target;
		}
		return res;
	}

	/** Get value, using the current value of \p target as the default if \p key is missing. */
	template <typename T>
	jsonconfig & get (std::string const & key, T & target)
	{
		get_config<T> (true, key, target, target);
		return *this;
	}

	/**
	 * Get value of required key
	 * @note May set nano::error_config::missing_value if \p key is missing, nano::error_config::invalid_value if value is invalid.
	 */
	template <typename T>
	T get (std::string const & key)
	{
		T target{};
		get_config<T> (true, key, target, target);
		return target;
	}

	/**
	 * Get required value.
	 * @note May set nano::error_config::missing_value if \p key is missing, nano::error_config::invalid_value if value is invalid.
	 */
	template <typename T>
	jsonconfig & get_required (std::string const & key, T & target)
	{
		get_config<T> (false, key, target);
		return *this;
	}

	/** Turn on or off automatic error message generation */
	void set_auto_error_message (bool auto_a)
	{
		auto_error_message = auto_a;
	}

	nano::error & get_error () override
	{
		return *error;
	}

protected:
	template <typename T>
	void construct_error_message (bool optional, std::string const & key)
	{
		if (auto_error_message && *error)
		{
			if (optional)
			{
				error->set_message (key + " is not " + type_desc<T> ());
			}
			else
			{
				error->set_message (key + " is required and must be " + type_desc<T> ());
			}
		}
	}

	/** Set error if not already set. That is, first error remains until get_error().clear() is called. */
	template <typename T, typename V>
	void conditionally_set_error (V error_a, bool optional, std::string const & key)
	{
		if (!*error)
		{
			*error = error_a;
			construct_error_message<T> (optional, key);
		}
	}
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
	template <typename T, typename = std::enable_if_t<std::is_same<T, uint8_t>::value>>
	jsonconfig & get_config (bool optional, std::string key, uint8_t & target, uint8_t default_value = T ())
	{
		int64_t tmp;
		try
		{
			auto val (tree.get<std::string> (key));
			if (!boost::conversion::try_lexical_convert<int64_t> (val, tmp) || tmp < 0 || tmp > 255)
			{
				conditionally_set_error<T> (nano::error_config::invalid_value, optional, key);
			}
			else
			{
				target = static_cast<uint8_t> (tmp);
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

	template <typename T, typename = std::enable_if_t<std::is_same<T, bool>::value>>
	jsonconfig & get_config (bool optional, std::string key, bool & target, bool default_value = false)
	{
		auto bool_conv = [this, &target, &key, optional](std::string val) {
			if (val == "true")
			{
				target = true;
			}
			else if (val == "false")
			{
				target = false;
			}
			else if (!*error)
			{
				conditionally_set_error<T> (nano::error_config::invalid_value, optional, key);
			}
		};
		try
		{
			auto val (tree.get<std::string> (key));
			bool_conv (val);
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

	template <typename T, typename = std::enable_if_t<std::is_same<T, boost::asio::ip::address_v6>::value>>
	jsonconfig & get_config (bool optional, std::string key, boost::asio::ip::address_v6 & target, boost::asio::ip::address_v6 default_value = T ())
	{
		try
		{
			auto address_l (tree.get<std::string> (key));
			boost::system::error_code bec;
			target = boost::asio::ip::address_v6::from_string (address_l, bec);
			if (bec)
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
		return *this;
	}

private:
	/** The property node being managed */
	boost::property_tree::ptree & tree;
	boost::property_tree::ptree tree_default;
	/** If set, automatically construct error messages based on parameters and type information. */
	bool auto_error_message{ true };

	/** We're a nano::error_aware type. Child nodes share the error state. */
	std::shared_ptr<nano::error> error;
};
}
