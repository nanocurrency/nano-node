Static and dynamic polymorphism
===============================


With a header only library, you can only do so much with static
polymorphism alone. There are some situations where you have to handle
dynamic polymorphism because of unavoidable runtime-based decision
making. Although you can deal with the base types that remain static,
behavior can vary greatly which derived type should be handling the
situation based on runtime values.

This situation comes up in the :mod:`cpp-netlib` when we decide what
kind of connection handler to use for a given HTTP URI -- whether it's
plain HTTP or HTTPS.  Although the HTTP semantics are the same for
HTTP and HTTPS the implementation of the connection handler greatly
varies on whether to use a plain TCP connection or an SSL-wrapped TCP
connection.

The general pattern or technique is to combine tag-based dispatch with
a strategy factory, all while not requiring any externally built
libraries. Doing it in a header-only library requires a little
creativity and additional layers of indirection that you otherwise
will not need for a library with externally built static/dynamic
libraries.

First we define the base type which we want to support dynamic
behavior with.  There's nothing special with the base type, except
that it supports the tag dispatch earlier defined and has a virtual
destructor. In code it looks like this:

.. code-block:: c++

    template <class Tag>
    struct base {
        virtual void foo() = 0; // make this an abstract base
        virtual ~base() {
            // do the base destructor thing here.
        }
    };

We then define a set of derived types that specialize the
implementation of the ``foo`` member function. To facilitate the
dispatch of the correct type based on an input, we create a strategy
factory function:

.. code-block:: c++

    template <class Tag>
    unique_ptr<base<Tag> > strategy(int input, Tag) {
        unique_ptr<base<Tag> > ptr;
        switch(input) {
            case 0: ptr.reset(new derived0()); break;
            case 1: ptr.reset(new derived1()); break;
            // ...
            default: ptr.reset(0); break;
        }
        return ptr;
    }

    unique_ptr<base<default_> > ptr =
        strategy(input, default_()); // input is a runtime value

The strategy factory can be a standalone function, or a static member
of a factory class that is specialized by tag dispatch. This can be
done like the following:

.. code-block:: c++

    template <class Tag>
    struct strategy;

    template <>
    struct strategy<default_> {
        static unique_ptr<base<default_> > create(int input) {
            unique_ptr<base<default_> > ptr;
            switch(input) {
                case 0: ptr.reset(new derived0()); break;
                case 1: ptr.reset(new derived1()); break;
                //...
                default: ptr.reset(0); break;
            }
            return ptr;
        }
    };

This approach allows the header-only libraries to define new dynamic
types in subsequent versions of the library while keeping the
static-dynamic bridge fluid. The only down-side to this is the
possibility of derived type explosion in case there are a lot of
different strategies or specializations available -- this though is
not unique to static-dynamic bridging, but is also a problem with pure
object oriented programming with dynamic polymorphism.
