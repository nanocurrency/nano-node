#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <fstream>

nano::tomlconfig::tomlconfig () :
	tree (cpptoml::make_table ())
{
	error = std::make_shared<nano::error> ();
}

nano::tomlconfig::tomlconfig (std::shared_ptr<cpptoml::table> const & tree_a, std::shared_ptr<nano::error> const & error_a) :
	nano::configbase (error_a), tree (tree_a)
{
	if (!error)
	{
		error = std::make_shared<nano::error> ();
	}
}

void nano::tomlconfig::doc (std::string const & key, std::string const & doc)
{
	tree->document (key, doc);
}

nano::error & nano::tomlconfig::read (std::filesystem::path const & path_a)
{
	std::stringstream stream_override_empty;
	stream_override_empty << std::endl;
	return read (stream_override_empty, path_a);
}

nano::error & nano::tomlconfig::read (std::istream & stream_overrides, std::filesystem::path const & path_a)
{
	std::ifstream stream{ path_a };
	if (!stream)
	{
		error->set ("Could not open config file: " + path_a.string ());
		return *error;
	}
	return read (stream_overrides, stream);
}

nano::error & nano::tomlconfig::read (std::istream & stream_a)
{
	std::stringstream stream_override_empty;
	stream_override_empty << std::endl;
	return read (stream_override_empty, stream_a);
}

/** Read from two streams where keys in the first will take precedence over those in the second stream. */
nano::error & nano::tomlconfig::read (std::istream & stream_first_a, std::istream & stream_second_a)
{
	try
	{
		tree = cpptoml::parse_base_and_override_files (stream_first_a, stream_second_a, cpptoml::parser::merge_type::ignore, true);
	}
	catch (std::runtime_error const & ex)
	{
		*error = ex;
	}
	return *error;
}

void nano::tomlconfig::write (std::filesystem::path const & path_a)
{
	if (!std::filesystem::exists (path_a))
	{
		// Create the file first and restrict its permissions before writing, otherwise Windows only grants read permissions
		{
			std::ofstream create{ path_a };
		}
		nano::set_secure_perm_file (path_a);
	}
	std::ofstream stream{ path_a };
	write (stream);
}

void nano::tomlconfig::write (std::ostream & stream_a) const
{
	cpptoml::toml_writer writer{ stream_a, "" };
	tree->accept (writer);
}

/** Returns the table managed by this instance */
std::shared_ptr<cpptoml::table> nano::tomlconfig::get_tree ()
{
	return tree;
}

/** Returns true if the toml table is empty */
bool nano::tomlconfig::empty () const
{
	return tree->empty ();
}

void nano::tomlconfig::set_error_once (nano::error_config code_a, std::string const & message_a)
{
	// The first error is the one that explains the rest, see configbase::conditionally_set_error
	if (!*error)
	{
		*error = code_a;
		error->set_message (message_a);
	}
}

std::optional<nano::tomlconfig> nano::tomlconfig::get_optional_child (std::string const & key_a)
{
	if (!tree->contains (key_a))
	{
		return std::nullopt;
	}
	auto child = tree->get_table (key_a);
	if (!child)
	{
		set_error_once (nano::error_config::invalid_value, "Configuration node is not a table: " + key_a);
		return std::nullopt;
	}
	return tomlconfig (child, error);
}

nano::tomlconfig nano::tomlconfig::get_required_child (std::string const & key_a)
{
	if (!tree->contains (key_a))
	{
		set_error_once (nano::error_config::missing_value, "Missing configuration node: " + key_a);
		return tomlconfig (cpptoml::make_table (), error);
	}
	auto child = tree->get_table (key_a);
	if (!child)
	{
		set_error_once (nano::error_config::invalid_value, "Configuration node is not a table: " + key_a);
		return tomlconfig (cpptoml::make_table (), error);
	}
	return tomlconfig (child, error);
}

nano::tomlconfig & nano::tomlconfig::put_child (std::string const & key_a, nano::tomlconfig & conf_a)
{
	tree->insert (key_a, conf_a.get_tree ());
	return *this;
}

nano::tomlconfig & nano::tomlconfig::replace_child (std::string const & key_a, nano::tomlconfig & conf_a)
{
	tree->erase (key_a);
	put_child (key_a, conf_a);
	return *this;
}

/** Returns true if \p key_a is present */
bool nano::tomlconfig::has_key (std::string const & key_a)
{
	return tree->contains (key_a);
}

/** Erase the property of given key */
nano::tomlconfig & nano::tomlconfig::erase (std::string const & key_a)
{
	tree->erase (key_a);
	return *this;
}

std::shared_ptr<cpptoml::array> nano::tomlconfig::create_array (std::string const & key, std::optional<char const *> documentation_a)
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

// boost's lexical cast doesn't handle (u)int8_t
nano::tomlconfig & nano::tomlconfig::get_config (bool optional, std::string const & key, uint8_t & target, uint8_t default_value)
{
	try
	{
		if (tree->contains_qualified (key))
		{
			int64_t tmp;
			auto val (tree->get_qualified_as<std::string> (key));
			if (!boost::conversion::try_lexical_convert<int64_t> (*val, tmp) || tmp < 0 || tmp > 255)
			{
				conditionally_set_error<uint8_t> (nano::error_config::invalid_value, optional, key);
			}
			else
			{
				target = static_cast<uint8_t> (tmp);
			}
		}
		else if (!optional)
		{
			conditionally_set_error<uint8_t> (nano::error_config::missing_value, optional, key);
		}
		else
		{
			target = default_value;
		}
	}
	catch (std::runtime_error & ex)
	{
		conditionally_set_error<uint8_t> (ex, optional, key);
	}

	return *this;
}

nano::tomlconfig & nano::tomlconfig::get_config (bool optional, std::string const & key, bool & target, bool default_value)
{
	auto bool_conv = [this, &target, &key, optional] (std::string val) {
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
			conditionally_set_error<bool> (nano::error_config::invalid_value, optional, key);
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
			conditionally_set_error<bool> (nano::error_config::missing_value, optional, key);
		}
		else
		{
			target = default_value;
		}
	}
	catch (std::runtime_error & ex)
	{
		conditionally_set_error<bool> (ex, optional, key);
	}
	return *this;
}

nano::tomlconfig & nano::tomlconfig::get_config (bool optional, std::string key, boost::asio::ip::address_v6 & target, boost::asio::ip::address_v6 const & default_value)
{
	try
	{
		if (tree->contains_qualified (key))
		{
			auto address_l (tree->get_qualified_as<std::string> (key));
			boost::system::error_code bec;
			target = boost::asio::ip::make_address_v6 (address_l.value_or (""), bec);
			if (bec)
			{
				conditionally_set_error<boost::asio::ip::address_v6> (nano::error_config::invalid_value, optional, key);
			}
		}
		else if (!optional)
		{
			conditionally_set_error<boost::asio::ip::address_v6> (nano::error_config::missing_value, optional, key);
		}
		else
		{
			target = default_value;
		}
	}
	catch (std::runtime_error & ex)
	{
		conditionally_set_error<boost::asio::ip::address_v6> (ex, optional, key);
	}

	return *this;
}