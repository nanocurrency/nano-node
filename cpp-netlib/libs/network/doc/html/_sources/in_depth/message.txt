The message template
====================

One of the core components in the library is the concept and the
implementation of a common message type. In most (not all) network
protocols, the concept of a message is central to the definition of
the protocol. In HTTP, SMTP, XMPP, and even other protocols like SNMP
and ICMP, there is a common notion of a "packet" or a message. In
cpp-netlib we chose to implement the concept of a message that has the
following common parts:

  * **Source** - every message has a source identifier which varies
    from protocol to protocol.

  * **Destination** - every message has a destination identifier which
    varies from protocol to protocol.

  * **Headers** - each message is assumed to contain headers, which
    may be empty in cases where the protocol does not support it, but
    is nonetheless supported by cpp-netlib messages.

  * **Body** - the content area of a message which varies from
    protocol to protocol (also sometimes referred to as payload).

This division is purely logical -- in the underlying implementation,
the message type can choose to have different means of storing the
data, depending on the type used to tag the message. This section
covers the `Message Concept`_ as well as the `basic_message`_
implementation.

Message Concept
```````````````

.. warning:: The Message framework is deprecated in the 0.11 release, and will
             be removed in future versions of the library.

The Message Concept specifies what the valid operations on a message
are as well as what messages look like semantically. The following
table summarize the operations and syntactic as well as semantic
properties of messages.

**Legend**

:M: The message type.
:H: A headers container type.
:m,n: An instance of **M**.
:S: A string type.
:s,k,v: An instance of **S**.
:O: The source type.
:D: The destination type.
:B: The body type.
:T: The Tag type.

+----------------------------+----------------------+-----------------------------------------+
| Construct                  | Result               | Description                             |
+============================+======================+=========================================+
| ``typename M::tag``        | T                    | The nested tag type.                    |
+----------------------------+----------------------+-----------------------------------------+
| ``M()``                    | Instance of M        | Default constructible.                  |
+----------------------------+----------------------+-----------------------------------------+
| ``M(m)``                   | Instance of M        | Copy constructible.                     |
+----------------------------+----------------------+-----------------------------------------+
| ``m = n;``                 | Reference to m       | Assignable.                             |
+----------------------------+----------------------+-----------------------------------------+
| ``swap(m, n);``            | ``void``             | Swappable.                              |
+----------------------------+----------------------+-----------------------------------------+
| ``source(m);``             | Convertible to O     | Retrieve the source of ``m``.           |
+----------------------------+----------------------+-----------------------------------------+
| ``destination(m);``        | Convertible to D     | Retrieve the destination of ``m``.      |
+----------------------------+----------------------+-----------------------------------------+
| ``headers(m);``            | Convertible to H     | Retrieve the headers of ``m``.          |
+----------------------------+----------------------+-----------------------------------------+
| ``body(m);``               | Convertible to B     | Retrieve the body of ``m``.             |
+----------------------------+----------------------+-----------------------------------------+
| ``m << source(s);``        | ``M &``              | Set the source of ``m``.                |
+----------------------------+----------------------+-----------------------------------------+
| ``m << destination(s);``   | ``M &``              | Set the destination of ``m``.           |
+----------------------------+----------------------+-----------------------------------------+
| ``m << header(k, v);``     | ``M &``              | Add a header to ``m``.                  |
+----------------------------+----------------------+-----------------------------------------+
| ``m << remove_header(k);`` | ``M &``              | Remove a header from ``m``.             |
+----------------------------+----------------------+-----------------------------------------+
| ``m << body(s);``          | ``M &``              | Set the body of ``m``.                  |
+----------------------------+----------------------+-----------------------------------------+
| ``source(m,s);``           | ``void``             | Set the source of ``m``.                |
+----------------------------+----------------------+-----------------------------------------+
| ``destination(m,s);``      | ``void``             | Set the destination of ``m``.           |
+----------------------------+----------------------+-----------------------------------------+
| ``add_header(m, k, v);``   | ``void``             | Add a header to ``m``.                  |
+----------------------------+----------------------+-----------------------------------------+
| ``remove_header(m, k);``   | ``void``             | Remove a header from ``m``.             |
+----------------------------+----------------------+-----------------------------------------+
| ``clear_headers(m);``      | ``void``             | Clear the headers of ``m``.             |
+----------------------------+----------------------+-----------------------------------------+
| ``body(m,s);``             | ``M &``              | Set the body of ``m``.                  |
+----------------------------+----------------------+-----------------------------------------+

Types that model the Message Concept are meant to encapsulate data
that has a source, a destination, one or more named headers, and a
body/payload. Because the accessors and the directives are not
required to be part of the message type that models the Message
Concept, a message can be implemented as a POD type and have all
manipulations performed in the directive implementations, as well as
value transformations done in the accessors.

Directives, Modifiers, and Wrappers
```````````````````````````````````

In the Message Concept definition there are three basic constructs that follow a
certain pattern. These patterns are Directives_, Modifiers_, and Wrappers_.

Directives
~~~~~~~~~~

A directive is a function object that is applied to a Message. Directives
encapsulate a set of operations that apply to messages. The general requirement
for a Directive is that it should apply these operations on a message.

A directive may dispatch on the type of the message passed to it at the point of
the function call. Typically, directives are generated using a factory function
that returns the correct directive type.

For a given directive ``foo_directive`` a generator function called ``foo`` is
typically implemented:

.. code-block:: c++

    struct foo_directive {
        template <class Message>
        Message & operator()(Message & m) const {
            // do something to m
            return m;
        }
    };

    foo_directive const foo() {
        return foo_directive();
    }

    // to apply a directive, we use the << operator
    message m;
    m << foo();

Modifiers
~~~~~~~~~

A modifier is generally defined as a free function that takes a reference to a
non-const lvalue message as the first parameter, and any number of parameters.
In the concept definition of the Message Concept, a modifier follows the form:

.. code-block:: c++

    modifier(message, ...)

Modifiers are meant to imply modifications on a message, which also allows for
easier dispatch based on Argument Dependent Lookup (ADL_) on the type of the
message. Note that Directives_ can be implemented in terms of Modifiers and
vice versa, although that is not required nor specified.

.. _ADL: http://en.wikipedia.org/wiki/Argument-dependent_name_lookup

Wrappers
~~~~~~~~

A Wrapper is basically an implementation detail that ensures that a given
message, when wrapped, can be converted to the associated part of the message. A
wrapper has a type that encapsulates the conversion logic from a message to a
given type.

An example of a Wrapper would be ``source_wrapper`` which would be returned by a
call to the wrapper generator function ``source``. An example implementation of
the ``source_wrapper`` would look like:

.. code-block:: c++

    template <class Tag, template <class> class Message>
    struct source_wrapper {
        Message<Tag> const & m;
        explicit source_wrapper(Message<Tag> const & m)
        : m(m) {}
        typedef typename source<Tag>::type source_type;
        operator source_type const & () {
            return m.source;
        }
        operator source_type const () {
            return m.source;
        }
        operator source_type () {
            return m.source;
        }
    };

    template <class Tag, template <class> class Message>
    source_wrapper<Tag, Message> const
    source(Message<Tag> const & message) {
        return source_wrapper<Tag, Message>(message);
    }

This pattern is similar to an adapter, but the specific notion of wrapping a
data type (in this case, an object of a type that models the Message Concept)
using an intermediary wrapper is what is pertained to by the Wrapper pattern.
In this case, the Wrapper is ``source_wrapper`` while ``source`` is merely a
wrapper generator function.

``basic_message``
`````````````````

The default implementation of a simple type that models the Message
Concept is available in cpp-netlib. This default implementation is
named ``basic_message`` which supports a ``Tag`` template
parameter. The definition of ``basic_message`` looks like this:

.. code-block:: c++

    template <class Tag>
    class basic_message;

The ``basic_message`` template requires that the following
tag-dispatched metafunctions are defined for the type ``Tag``:

.. code-block:: c++

    template <class Tag>
    struct string;

    template <class Tag>
    struct headers_container;

All the operations defined by the message concept are implemented by
this basic message type. Other message implementations can either use
this common message type or specialize it according to whether they
want to use different containers or whether it's going to be just a
POD type.
