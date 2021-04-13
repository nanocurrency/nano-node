#include <nano/lib/signal_handler.hpp>
#include <nano/lib/utility.hpp>

nano::signal_handler& nano::signal_handler::get_instance ()
{
    static signal_handler instance {};
    return instance;
}

void nano::signal_handler::register_handler (const std::initializer_list<int>& signals, const handler& handler)
{
    for (const auto signal : signals)
    {
        // do things with signal
    }
}

void nano::signal_handler::register_default_handlers ()
{
    // for SIGSEGV, do the following which might help post-mortem:
    // nano::dump_crash_stacktrace ();
    // nano::create_load_memory_address_files ();
}

nano::signal_handler::signal_handler()
{

}
