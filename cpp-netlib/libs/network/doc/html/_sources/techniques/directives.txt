Directives
==========

The :mod:`cpp-netlib` uses a technique for allowing message-passing
semantics in a chainable fashion in the form of directives. The basic
concept for directives is, in a general sense, an encapsulated
transformation that can be applied to objects that abide by the
directive protocol.

Using the object-oriented notion of message passing, where an object
accepts a message (usually a function call) we define a simple DSEL in
order for the protocol to be supported by certain object types. In the
:mod:`cpp-netlib` the protocol implemented is similar to that of the
standard iostream formatting system:

.. code-block:: c++

    object << directive1(...)
           << directive2(...)
           ...
           << directiveN(...);

In :mod:`cpp-netlib` the directives are simple function objects that
take a target object as reference and returns a reference to the same
object as a result. In code the directive pattern looks like the
following:

.. code-block:: c++

    struct directive_type {
        template <class Input>
        Input & operator()(Input & input) const {
            // do something to input
            return input;
        }
    };

To simplify directive creation, usually factory or generator functions
are defined to return concrete objects of the directive's type.

.. code-block:: c++

    inline
    directive_type directive(...) {
        return directive_type();
    }

The trivial implementation of the directive protocol then boils down
to the specialization of the shift-left operator on the target type.

.. code-block:: c++

    template <class Directive>
    inline target_type & operator<< 
    (target_type & x, Directive const & f) {
        return f(x);
    }

.. todo::

    An example using a directive.

The rationale for implementing directives include the following:

  * **Encapsulation** - by moving logic into the directive types the
    target object's interface can remain rudimentary and even hidden
    to the user's immediate attention. Adding this layer of
    indirection also allows for changing the underlying
    implementations while maintaining the same syntactic and semantic
    properties.
  * **Flexibility** - by allowing the creation of directives that are
    independent from the target object's type, generic operations can
    be applied based on the concept being modeled by the target
    type. The flexibility also afforded comes in the directive's
    generator function, which can also generate different concrete
    directive specializations based on parameters to the function.
  * **Extensibility** - because the directives are independent of the
    target object's type, new directives can be added and supported
    without having to change the target object at all.
  * **Reuse** - truly generic directives can then be used for a broad
    set of target object types that model the same concepts supported
    by the directive.  Because the directives are self-contained
    objects, the state and other object references it keeps are only
    accessible to it and can be re-used in different contexts as well.

Extending a system that uses directives is trivial in header-only
systems because new directives are simply additive. The protocol is
simple and can be applied to a broad class of situations.

In a header-only library, the static nature of the wiring and chaining
of the operations lends itself to compiler abuse. A deep enough
nesting of the directives can lead to prolonged compilation times.
