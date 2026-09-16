#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <fstream>

nano::tomlconfig::tomlconfig () :
	tree (cpptoml::make_table ()),
	consumed (std::make_shared<consumed_set> ())
{
	error = std::make_shared<nano::error> ();
}

nano::tomlconfig::tomlconfig (std::shared_ptr<cpptoml::table> const & tree_a, std::shared_ptr<nano::error> const & error_a, std::shared_ptr<consumed_set> const & consumed_a) :
	nano::configbase (error_a),
	tree (tree_a),
	consumed (consumed_a)
{
	if (!error)
	{
		error = std::make_shared<nano::error> ();
	}
	if (!consumed)
	{
		consumed = std::make_shared<consumed_set> ();
	}
}

void nano::tomlconfig::doc (std::string const & key, std::string const & doc)
{
	tree->document (key, doc);
}

nano::error & nano::tomlconfig::read (std::filesystem::path const & path_a)
{
	std::ifstream stream{ path_a };
	if (!stream)
	{
		error->set ("Could not open config file: " + path_a.string ());
		return *error;
	}
	return read (stream);
}

nano::error & nano::tomlconfig::read (std::istream & stream_a)
{
	try
	{
		// Every value is parsed as a string and converted on access, see get_config
		cpptoml::parser parser{ stream_a, cpptoml::parser::merge_type::none, /* stringify_values */ true };
		tree = parser.parse ();
	}
	catch (std::runtime_error const & ex)
	{
		*error = ex;
	}
	return *error;
}

namespace
{
/** Copies every entry of \p source into \p target, descending into tables both sides have and replacing anything else */
void merge_into (cpptoml::table & target, cpptoml::table const & source)
{
	for (auto const & [key, value] : source)
	{
		if (value->is_table () && target.contains (key) && target.get (key)->is_table ())
		{
			merge_into (*target.get_table (key), *value->as_table ());
		}
		else
		{
			target.erase (key);
			target.insert (key, value);
		}
	}
}
}

nano::error & nano::tomlconfig::apply_override (std::string const & override_a)
{
	try
	{
		std::stringstream stream{ override_a };
		cpptoml::parser parser{ stream, cpptoml::parser::merge_type::none, /* stringify_values */ true };
		merge_into (*tree, *parser.parse ());
	}
	catch (std::runtime_error const & ex)
	{
		error->set ("Invalid config override \"" + override_a + "\": " + ex.what ());
	}
	return *error;
}

nano::error & nano::tomlconfig::apply_overrides (std::vector<std::string> const & overrides_a)
{
	for (auto const & override : overrides_a)
	{
		if (apply_override (override))
		{
			break;
		}
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

namespace
{
void mark_subtree (nano::tomlconfig::consumed_set & consumed, cpptoml::table const & table)
{
	for (auto const & [key, value] : table)
	{
		consumed.insert (value.get ());
		if (value->is_table ())
		{
			mark_subtree (consumed, *value->as_table ());
		}
	}
}

bool any_read (nano::tomlconfig::consumed_set const & consumed, cpptoml::table const & table)
{
	for (auto const & [key, value] : table)
	{
		if (consumed.contains (value.get ()))
		{
			return true;
		}
		if (value->is_table () && any_read (consumed, *value->as_table ()))
		{
			return true;
		}
	}
	return false;
}

void collect_unknown (nano::tomlconfig::consumed_set const & consumed, cpptoml::table const & table, std::string const & prefix, std::vector<std::string> & result)
{
	for (auto const & [key, value] : table)
	{
		auto const path = prefix + key;
		if (value->is_table ())
		{
			// A table that was handed out or partially read is inspected entry by entry, an untouched one is reported as a whole
			if (consumed.contains (value.get ()) || any_read (consumed, *value->as_table ()))
			{
				collect_unknown (consumed, *value->as_table (), path + ".", result);
			}
			else
			{
				result.push_back (path);
			}
		}
		else if (!consumed.contains (value.get ()))
		{
			result.push_back (path);
		}
	}
}
}

std::shared_ptr<cpptoml::table> nano::tomlconfig::get_tree ()
{
	mark_subtree (*consumed, *tree);
	return tree;
}

std::vector<std::string> nano::tomlconfig::unknown_keys () const
{
	std::vector<std::string> result;
	collect_unknown (*consumed, *tree, "", result);
	return result;
}

void nano::tomlconfig::mark_read (std::string const & key)
{
	if (auto node = tree->get_qualified (key))
	{
		consumed->insert (node.get ());
	}
}

std::optional<std::string> nano::tomlconfig::get_value (std::string const & key)
{
	auto node = tree->get_qualified (key);
	if (!node)
	{
		return std::nullopt;
	}
	consumed->insert (node.get ());
	if (auto value = node->as<std::string> ())
	{
		return value->get ();
	}
	return std::nullopt;
}

/** Returns true if the toml table is empty */
bool nano::tomlconfig::empty () const
{
	return tree->empty ();
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
		*error = nano::error_config::invalid_value;
		error->set_message ("Configuration node is not a table: " + key_a);
		return std::nullopt;
	}
	consumed->insert (child.get ());
	return tomlconfig (child, error, consumed);
}

nano::tomlconfig nano::tomlconfig::get_required_child (std::string const & key_a)
{
	if (!tree->contains (key_a))
	{
		*error = nano::error_config::missing_value;
		error->set_message ("Missing configuration node: " + key_a);
		return tomlconfig (cpptoml::make_table (), error);
	}
	auto child = tree->get_table (key_a);
	if (!child)
	{
		*error = nano::error_config::invalid_value;
		error->set_message ("Configuration node is not a table: " + key_a);
		return tomlconfig (cpptoml::make_table (), error);
	}
	consumed->insert (child.get ());
	return tomlconfig (child, error, consumed);
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
			auto val = get_value (key);
			if (!val || !boost::conversion::try_lexical_convert<int64_t> (*val, tmp) || tmp < 0 || tmp > 255)
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
		else
		{
			conditionally_set_error<bool> (nano::error_config::invalid_value, optional, key);
		}
	};
	try
	{
		if (tree->contains_qualified (key))
		{
			bool_conv (get_value (key).value_or (""));
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
			auto address_l = get_value (key);
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