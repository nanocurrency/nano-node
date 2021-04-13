#pragma once

#include <functional>
#include <initializer_list>

namespace nano
{

/**
 * Manages signal handling and allows to register custom handlers for any signal
 */
class signal_handler final
{
public:
    /**
     * Represents the type of a handler for a signal.
     * The parameter it takes is the signal being handled.
     */
    using handler = std::function<void(int)>;

    /**
     * Returns the (singleton) instance of the `signal_handler`.
     */
    static signal_handler& get_instance ();

    /**
     * Registers a new handler to be executed when any of the signals specified in the list occurs.
     */
    void register_handler (const std::initializer_list<int>&, const handler&);

    /**
     * Registers default handlers for the signals that a nano process is usually interested in handling.
     * Those are usually SIGINT, SIGTERM, SIGABRT and SIGSEGV.
     */
    void register_default_handlers ();

private:
    signal_handler ();
};
}
