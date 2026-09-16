#include <nano/lib/config_template.hpp>

#include <sstream>

namespace
{
/** Bare keys only need quoting when they contain characters outside the TOML bare key alphabet */
std::string quote_key (std::string const & key)
{
	if (key.find_first_not_of ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos)
	{
		return key;
	}
	return "\"" + cpptoml::toml_writer::escape_string (key) + "\"";
}

/** The TOML text of a value or array, as the library writer would emit it */
std::string render_value (cpptoml::base const & value)
{
	std::stringstream ss;
	cpptoml::toml_writer writer{ ss, "" };
	value.accept (writer, false);
	return ss.str ();
}

class template_writer
{
public:
	template_writer (std::ostream & out, std::function<bool (std::string const &)> const & commented) :
		out{ out },
		commented{ commented }
	{
	}

	void render (cpptoml::table const & table, std::string const & path)
	{
		std::vector<std::pair<std::string, std::shared_ptr<cpptoml::base>>> values, tables;
		for (auto const & entry : table)
		{
			(entry.second->is_table () ? tables : values).push_back (entry);
		}

		// A table holding nothing but other tables gets no header of its own
		if (!path.empty () && !values.empty ())
		{
			out << "\n[" << path << "]\n";
		}

		for (auto const & [key, value] : values)
		{
			auto const key_path = path.empty () ? key : path + "." + key;
			if (table.has_doc (key))
			{
				out << "\n";
				std::istringstream doc{ table.doc (key) };
				for (std::string line; std::getline (doc, line);)
				{
					out << "\t# " << line << "\n";
				}
			}
			out << "\t" << (commented (key_path) ? "# " : "") << quote_key (key) << " = " << render_value (*value) << "\n";
		}

		for (auto const & [key, value] : tables)
		{
			render (*value->as_table (), path.empty () ? quote_key (key) : path + "." + quote_key (key));
		}
	}

private:
	std::ostream & out;
	std::function<bool (std::string const &)> const & commented;
};
}

std::string nano::render_config_template (nano::tomlconfig & config, std::function<bool (std::string const & path)> const & commented)
{
	std::stringstream ss;
	template_writer writer{ ss, commented };
	writer.render (*config.get_tree (), "");
	return ss.str ();
}

std::string nano::render_config_template (nano::tomlconfig & config, bool comment_values)
{
	return render_config_template (config, [comment_values] (std::string const &) {
		return comment_values;
	});
}

std::string nano::render_config_update (nano::tomlconfig & current, nano::tomlconfig & defaults)
{
	auto current_tree = current.get_tree ();
	auto defaults_tree = defaults.get_tree ();
	return render_config_template (current, [&] (std::string const & path) {
		if (!current_tree->contains_qualified (path) || !defaults_tree->contains_qualified (path))
		{
			return false;
		}
		return render_value (*current_tree->get_qualified (path)) == render_value (*defaults_tree->get_qualified (path));
	});
}
