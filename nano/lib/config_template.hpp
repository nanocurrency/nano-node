#pragma once

#include <nano/lib/tomlconfig.hpp>

#include <functional>
#include <string>

namespace nano
{
/**
 * Renders a configuration document as an annotated file: one header per table, the doc comment above every
 * value, and each value commented out when `commented (path)` returns true for its dotted path. Tables are
 * written after the values of their parent, both in key order.
 */
std::string render_config_template (nano::tomlconfig & config, std::function<bool (std::string const & path)> const & commented);

/** Renders every value commented out (`comment_values`) or every value active */
std::string render_config_template (nano::tomlconfig & config, bool comment_values);

/**
 * Renders `current` with every value that equals its counterpart in `defaults` commented out, so that only the
 * operator's changes stay active. Values without a counterpart stay active.
 */
std::string render_config_update (nano::tomlconfig & current, nano::tomlconfig & defaults);
}
