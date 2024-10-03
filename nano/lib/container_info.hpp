#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

namespace nano
{
/* These containers are used to collect information about sequence containers.
 * It makes use of the composite design pattern to collect information
 * from sequence containers and sequence containers inside member variables.
 */
struct container_info_entry
{
	std::string name;
	size_t count;
	size_t sizeof_element;
};

class container_info_component
{
public:
	virtual ~container_info_component () = default;
	virtual bool is_composite () const = 0;
};

class container_info_composite : public container_info_component
{
public:
	container_info_composite (std::string name);
	bool is_composite () const override;
	void add_component (std::unique_ptr<container_info_component> child);
	std::vector<std::unique_ptr<container_info_component>> const & get_children () const;
	std::string const & get_name () const;

private:
	std::string name;
	std::vector<std::unique_ptr<container_info_component>> children;
};

class container_info_leaf : public container_info_component
{
public:
	container_info_leaf (container_info_entry);
	bool is_composite () const override;
	container_info_entry const & get_info () const;

private:
	container_info_entry info;
};
}

/*
 * New version
 */
namespace nano
{
class container_info;

template <typename T>
concept sized_container = requires (T a) {
	typename T::value_type;
	{
		a.size ()
	} -> std::convertible_to<std::size_t>;
};

template <typename T>
concept container_info_collectable = requires (T a) {
	{
		a.collect_info ()
	} -> std::convertible_to<container_info>;
};

class container_info
{
public:
	// Child represented as <name, container_info>
	// Using pair to avoid problems with incomplete types
	using child = std::pair<std::string, container_info>;

	struct entry
	{
		std::string name;
		std::size_t size;
		std::size_t sizeof_element;
	};

public:
	/**
	 * Adds a subcontainer
	 */
	void add (std::string const & name, container_info const & info)
	{
		children_m.emplace_back (name, info);
	}

	template <container_info_collectable T>
	void add (std::string const & name, T const & container)
	{
		add (name, container.collect_info ());
	}

	/**
	 * Adds an entry to this container
	 */
	void put (std::string const & name, std::size_t size, std::size_t sizeof_element = 0)
	{
		entries_m.emplace_back (entry{ name, size, sizeof_element });
	}

	template <class T>
	void put (std::string const & name, std::size_t size)
	{
		put (name, size, sizeof (T));
	}

	template <sized_container T>
	void put (std::string const & name, T const & container)
	{
		put (name, container.size (), sizeof (typename T::value_type));
	}

public:
	bool children_empty () const
	{
		return children_m.empty ();
	}

	auto const & children () const
	{
		return children_m;
	}

	bool entries_empty () const
	{
		return entries_m.empty ();
	}

	auto const & entries () const
	{
		return entries_m;
	}

public:
	// Needed to convert to legacy container_info_component during transition period
	std::unique_ptr<nano::container_info_component> to_legacy (std::string const & name) const
	{
		auto composite = std::make_unique<nano::container_info_composite> (name);

		// Add entries as leaf components
		for (const auto & entry : entries_m)
		{
			nano::container_info_entry info{ entry.name, entry.size, entry.sizeof_element };
			composite->add_component (std::make_unique<nano::container_info_leaf> (info));
		}

		// Recursively convert children to composites and add them
		for (const auto & [child_name, child] : children_m)
		{
			composite->add_component (child.to_legacy (child_name));
		}

		return composite;
	}

private:
	std::list<child> children_m;
	std::list<entry> entries_m;
};
}