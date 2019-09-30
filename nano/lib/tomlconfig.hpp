#pragma once

#include <nano/boost/asio.hpp>
#include <nano/lib/configbase.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/utility.hpp>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <cpptoml.h>

namespace nano
{
/** Manages a table in a toml configuration table hierarchy */
class tomlconfig : public nano::configbase
{
public:
	tomlconfig () :
	tree (cpptoml::make_table ())
	{
		error = std::make_shared<nano::error> ();
	}

	tomlconfig (std::shared_ptr<cpptoml::table> const & tree_a, std::shared_ptr<nano::error> const & error_a = nullptr) :
	nano::configbase (error_a), tree (tree_a)
	{
		if (!error)
		{
			error = std::make_shared<nano::error> ();
		}
	}

	void doc (std::string const & key, std::string const & doc)
	{
		tree->document (key, doc);
	}

	/**
	 * Reads a json object from the stream
	 * @return nano::error&, including a descriptive error message if the config file is malformed.
	 */
	nano::error & read (boost::filesystem::path const & path_a)
	{
		std::stringstream stream_override_empty;
		stream_override_empty << std::endl;
		return read (stream_override_empty, path_a);
	}

	nano::error & read (std::istream & stream_overrides, boost::filesystem::path const & path_a)
	{
		std::fstream stream;
		open_or_create (stream, path_a.string ());
		if (!stream.fail ())
		{
			try
			{
				read (stream_overrides, stream);
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
		}
		return *error;
	}

	/** Read from two streams where keys in the first will take precedence over those in the second stream. */
	void read (std::istream & stream_first_a, std::istream & stream_second_a)
	{
		tree = cpptoml::parse_base_and_override_files (stream_first_a, stream_second_a, cpptoml::parser::merge_type::ignore, true);
	}

	void read (std::istream & stream_a)
	{
		std::stringstream stream_override_empty;
		stream_override_empty << std::endl;
		tree = cpptoml::parse_base_and_override_files (stream_override_empty, stream_a, cpptoml::parser::merge_type::ignore, true);
	}

	void write (boost::filesystem::path const & path_a)
	{
		std::fstream stream;
		open_or_create (stream, path_a.string ());
		write (stream);
	}

	void write (std::ostream & stream_a) const
	{
		cpptoml::toml_writer writer{ stream_a, "" };
		tree->accept (writer);
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

	/** Returns the table managed by this instance */
	std::shared_ptr<cpptoml::table> get_tree ()
	{
		return tree;
	}

	/** Returns true if the toml table is empty */
	bool empty () const
	{
		return tree->empty ();
	}

	boost::optional<tomlconfig> get_optional_child (std::string const & key_a)
	{
		boost::optional<tomlconfig> child_config;
		if (tree->contains (key_a))
		{
			return tomlconfig (tree->get_table (key_a), error);
		}
		return child_config;
	}

	tomlconfig get_required_child (std::string const & key_a)
	{
		if (!tree->contains (key_a))
		{
			*error = nano::error_config::missing_value;
			error->set_message ("Missing configuration node: " + key_a);
			return *this;
		}
		else
		{
			return tomlconfig (tree->get_table (key_a), error);
		}
	}

	tomlconfig & put_child (std::string const & key_a, nano::tomlconfig & conf_a)
	{
		tree->insert (key_a, conf_a.get_tree ());
		return *this;
	}

	tomlconfig & replace_child (std::string const & key_a, nano::tomlconfig & conf_a)
	{
		tree->erase (key_a);
		put_child (key_a, conf_a);
		return *this;
	}

	/** Set value for the given key. Any existing value will be overwritten. */
	template <typename T>
	tomlconfig & put (std::string const & key, T const & value, boost::optional<const char *> documentation_a = boost::none)
	{
		tree->insert (key, value);
		if (documentation_a)
		{
			doc (key, *documentation_a);
		}
		return *this;
	}

	/** Returns true if \p key_a is present */
	bool has_key (std::string const & key_a)
	{
		return tree->contains (key_a);
	}

	/** Erase the property of given key */
	tomlconfig & erase (std::string const & key_a)
	{
		tree->erase (key_a);
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

	auto create_array (std::string const & key, boost::optional<const char *> documentation_a)
	{
		if (!has_key (key))
		{
			auto arr = cpptoml::make_array ();
			tree->insert (key, arr);
			if (documentation_a)
			{
				doc (key, *documentation_a);
			}
		}

		return tree->get_qualified (key)->as_array ();
	}

	/**
	 * Iterate array entries.
	 * @param key Array element key. Qualified (dotted) keys are not supported for arrays so this must be called on the correct tomlconfig node.
	 */
	template <typename T>
	tomlconfig & array_entries_required (std::string const & key, std::function<void(T)> callback)
	{
		if (tree->contains_qualified (key))
		{
			auto items = tree->get_qualified_array_of<T> (key);
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
		get_config<T> (true, key, target, default_value);
		return *this;
	}

	/**
	 * Get optional value, using the current value of \p target as the default if \p key is missing.
	 * @return May return nano::error_config::invalid_value
	 */
	template <typename T>
	tomlconfig & get_optional (std::string const & key, T & target)
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
	tomlconfig & get (std::string const & key, T & target)
	{
		get_config<T> (true, key, target, target);
		return *this;
	}

	/**
	 * Get value of optional key. Use default value of data type if missing.
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
	tomlconfig & get_required (std::string const & key, T & target)
	{
		get_config<T> (false, key, target);
		return *this;
	}

	/**
	 * Erase keys whose values are equal to the one in \p defaults
	 */
	void erase_default_values (tomlconfig & defaults_a)
	{
		std::shared_ptr<cpptoml::table> clone = std::dynamic_pointer_cast<cpptoml::table> (tree->clone ());
		tomlconfig self (clone);

		// The toml library doesn't offer a general way to compare values, so let the diff run on a stringified parse
		std::stringstream ss_self;
		write (ss_self);
		self.read (ss_self);

		tomlconfig defaults_l;
		std::stringstream ss;
		defaults_a.write (ss);
		defaults_l.read (ss);

		erase_defaults (defaults_l.get_tree (), self.get_tree (), get_tree ());
	}

	std::string to_string ()
	{
		std::stringstream ss;
		cpptoml::toml_writer writer{ ss, "" };
		tree->accept (writer);
		return ss.str ();
	}

	std::string to_string_commented_entries ()
	{
		std::stringstream ss, ss_processed;
		cpptoml::toml_writer writer{ ss, "" };
		tree->accept (writer);
		std::string line;
		while (std::getline (ss, line, '\n'))
		{
			if (!line.empty () && line[0] != '#' && line[0] != '[')
			{
				line = "#" + line;
			}
			ss_processed << line << std::endl;
		}
		return ss_processed.str ();
	}

protected:
	template <typename T, typename = std::enable_if_t<nano::is_lexical_castable<T>::value>>
	tomlconfig & get_config (bool optional, std::string const & key, T & target, T default_value = T ())
	{
		try
		{
			if (tree->contains_qualified (key))
			{
				auto val (tree->get_qualified_as<std::string> (key));
				if (!boost::conversion::try_lexical_convert<T> (*val, target))
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

	// boost's lexical cast doesn't handle (u)int8_t
	template <typename T, typename = std::enable_if_t<std::is_same<T, uint8_t>::value>>
	tomlconfig & get_config (bool optional, std::string const & key, uint8_t & target, uint8_t default_value = T ())
	{
		try
		{
			if (tree->contains_qualified (key))
			{
				int64_t tmp;
				auto val (tree->get_qualified_as<std::string> (key));
				if (!boost::conversion::try_lexical_convert<int64_t> (*val, tmp) || tmp < 0 || tmp > 255)
				{
					conditionally_set_error<T> (nano::error_config::invalid_value, optional, key);
				}
				else
				{
					target = static_cast<uint8_t> (tmp);
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

	template <typename T, typename = std::enable_if_t<std::is_same<T, bool>::value>>
	tomlconfig & get_config (bool optional, std::string const & key, bool & target, bool default_value = false)
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
			if (tree->contains_qualified (key))
			{
				auto val (tree->get_qualified_as<std::string> (key));
				bool_conv (*val);
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

	template <typename T, typename = std::enable_if_t<std::is_same<T, boost::asio::ip::address_v6>::value>>
	tomlconfig & get_config (bool optional, std::string key, boost::asio::ip::address_v6 & target, boost::asio::ip::address_v6 default_value = T ())
	{
		try
		{
			if (tree->contains_qualified (key))
			{
				auto address_l (tree->get_qualified_as<std::string> (key));
				boost::system::error_code bec;
				target = boost::asio::ip::address_v6::from_string (address_l.value_or (""), bec);
				if (bec)
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

private:
	/** The config node being managed */
	std::shared_ptr<cpptoml::table> tree;

	/** Compare two stringified configs, remove keys where values are equal */
	void erase_defaults (std::shared_ptr<cpptoml::table> base, std::shared_ptr<cpptoml::table> other, std::shared_ptr<cpptoml::table> update_target)
	{
		std::vector<std::string> erased;
		assert (other != nullptr);
		for (auto & item : *other)
		{
			std::string const & key = item.first;
			if (other->contains (key) && base->contains (key))
			{
				auto value = item.second;
				if (value->is_table ())
				{
					auto child_base = base->get_table (key);
					auto child_other = other->get_table (key);
					auto child_target = update_target->get_table (key);
					erase_defaults (child_base, child_other, child_target);
					if (child_target->empty ())
					{
						erased.push_back (key);
					}
				}
				else if (value->is_array ())
				{
					auto arr_other = other->get_array (key)->get ();
					auto arr_base = base->get_array (key)->get ();

					if (arr_other.size () == arr_base.size ())
					{
						bool equal = std::equal (arr_other.begin (), arr_other.end (), arr_base.begin (),
						[](auto const & item1, auto const & item2) -> bool {
							return (item1->template as<std::string> ()->get () == item2->template as<std::string> ()->get ());
						});

						if (equal)
						{
							erased.push_back (key);
						}
					}
				}
				else if (value->is_value ())
				{
					auto val_other = std::dynamic_pointer_cast<cpptoml::value<std::string>> (other->get (key));
					auto val_base = std::dynamic_pointer_cast<cpptoml::value<std::string>> (base->get (key));

					if (val_other->get () == val_base->get ())
					{
						erased.push_back (key);
					}
				}
			}
		}
		for (auto & key : erased)
		{
			update_target->erase (key);
		}
	}
};
}
