Tag metafunctions
=================

Sometimes you want to vary a function or a type's behavior based on a
static parameter. In the :mod:`cpp-netlib` there are a number of
things you might want to change based on some such parameter -- like
what the underlying string type should be and how large a buffer
should be, among other things. The primary way to define this in a
header-only manner is to use tag-based metafunctions.

The skeleton of the approach is based on a similar technique for
defining type traits. In the :mod:`cpp-netlib` however the type traits
are defined on opaque tag types which serve to associate results to a
family of metafunctions.

Template Specialization
-----------------------

To illustrate this point, let's define a tag ``default_`` which we use
to denote the default implementation of a certain type ``foo``. For
instance we decide that the default string type we will use for
``default_`` tagged ``foo`` specializations will be an
``std::string``.

In the :mod:`cpp-netlib` this is done by defining a ``string``
metafunction type that is specialized on the tag ``default_`` whose
nested ``type`` result is the type ``std::string``. In code this would
translate to:

.. code-block:: c++

    template <class Tag>
    struct string {
        typedef void type;
    };

    struct default_;

    template <>
    struct string<default_> {
        typedef std::string type;
    };

Template Metaprogramming
------------------------

Starting with version 0.7, the tag dispatch mechanism changed slightly to use
Boost.MPL_. The idea is still the same, although we can get a little smarter
than just using template specializations. Instead of just defining an opaque
type ``default_``, we use the Boost.MPL equivalent of a vector to define which
root types of properties this ``default_`` tag supports. The idea is to make the
opaque type ``default_`` inherit property tags which the library supports
internally as definite extension points.

.. _Boost.MPL: http://www.boost.org/libs/mpl/index.html

Our definition of the ``default_`` tag will then look something like the
following:

.. code-block:: c++

    typedef mpl::vector<default_string> default_tags;

    template <class Tag>
    struct components;

    typedef mpl::inherit_linearly<
        default_tags,
        mpl::inherit<mpl::placeholders::_1, mpl::placeholders::_2>
        >::type default_;

    template <class Tag>
    struct components<default_> {
        typedef default_tags type;
    };

In the above listing, ``default_string`` is what we call a "root" tag which is
meant to be combined with other "root" tags to form composite tags. In this case
our composite tag is the tag ``default_``. There are a number of these "root"
tags that :mod:`cpp-netlib` provides. These are in the namespace
``boost::network::tags`` and are defined in ``boost/network/tags.hpp``.

Using this technique we change slightly our definition of the ``string``
metafunction class into this:

.. code-block:: c++

    template <class Tag>
    struct unsupported_tag;

    template <class Tag>
    struct string :
        mpl::if_<
            is_base_of<
                tags::default_string,
                Tag
            >,
            std::string,
            unsupported_tag<Tag>
        >
    {};

Notice that we don't have the typedef for ``type`` in the body of ``string``
anymore, but we do inherit from ``mpl::if_``. Since ``mpl::if_`` is a template
metafunction itself, it contains a definition of the resulting ``type`` which
``string`` inherits.

You can see the real definition of the ``string`` metafunction in
``boost/network/traits/string.hpp``.

Using Tags
----------

Once we have the defined tag, we can then use this in the definition of our
types. In the definition of the type ``foo`` we use this type function
``string`` and pass the tag type parameter to determine what to use as
the string type in the context of the type ``foo``. In code this would
translate into:

.. code-block:: c++

    template <class Tag>
    struct foo {
        typedef typename string<Tag>::type string_type;

        // .. use string_type where you need a string.
    };

Using this approach we can support different types of strings for
different tags on the type ``foo``. In case we want to use a different
type of string for the tag ``default_`` we only change the
composition of the ``string_tags`` MPL vector. For example, in :mod:`cpp-netlib`
there is a root tag ``default_wstring`` which causes the ``string`` metafunction 
to define ``std::wstring`` as the resulting type.

The approach also allows for the control of the structure and features
of types like ``foo`` based on the specialization of the tag. Whole
type function families can be defined on tags where they are supported
and ignored in cases where they are not.

To illustrate let's define a new tag ``swappable``. Given the above
definition of ``foo``, we want to make the ``swappable``-tagged
``foo`` define a ``swap`` function that extends the original
``default_``-tagged ``foo``. In code this would look like:

.. code-block:: c++

    struct swappable;

    template <>
    struct foo<swappable> : foo<default_> {
        void swap(foo<swappable> & other) {
            // ...
        }
    };

We also for example want to enable an ADL-reachable ``swap`` function:

.. code-block:: c++

    struct swappable;

    inline
    void swap(foo<swappable> & left, foo<swappable> & right) {
        left.swap(right);
    }

Overall what the tag-based definition approach allows is for static
definition of extension points that ensures type-safety and
invariants. This keeps the whole extension mechanism static and yet
flexible.

