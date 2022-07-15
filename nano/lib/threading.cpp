#include <nano/boost/asio/post.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/threading.hpp>

#include <boost/format.hpp>

#include <future>
#include <iostream>
#include <thread>

namespace
{
thread_local nano::thread_role::name current_thread_role = nano::thread_role::name::unknown;
}

nano::thread_role::name nano::thread_role::get ()
{
	return current_thread_role;
}

std::string nano::thread_role::get_string (nano::thread_role::name role)
{
	std::string thread_role_name_string;

	switch (role)
	{
		case nano::thread_role::name::unknown:
			thread_role_name_string = "<unknown>";
			break;
		case nano::thread_role::name::io:
			thread_role_name_string = "I/O";
			break;
		case nano::thread_role::name::work:
			thread_role_name_string = "Work pool";
			break;
		case nano::thread_role::name::packet_processing:
			thread_role_name_string = "Pkt processing";
			break;
		case nano::thread_role::name::vote_processing:
			thread_role_name_string = "Vote processing";
			break;
		case nano::thread_role::name::block_processing:
			thread_role_name_string = "Blck processing";
			break;
		case nano::thread_role::name::request_loop:
			thread_role_name_string = "Request loop";
			break;
		case nano::thread_role::name::wallet_actions:
			thread_role_name_string = "Wallet actions";
			break;
		case nano::thread_role::name::bootstrap_initiator:
			thread_role_name_string = "Bootstrap init";
			break;
		case nano::thread_role::name::bootstrap_connections:
			thread_role_name_string = "Bootstrap conn";
			break;
		case nano::thread_role::name::voting:
			thread_role_name_string = "Voting";
			break;
		case nano::thread_role::name::signature_checking:
			thread_role_name_string = "Signature check";
			break;
		case nano::thread_role::name::rpc_request_processor:
			thread_role_name_string = "RPC processor";
			break;
		case nano::thread_role::name::rpc_process_container:
			thread_role_name_string = "RPC process";
			break;
		case nano::thread_role::name::confirmation_height_processing:
			thread_role_name_string = "Conf height";
			break;
		case nano::thread_role::name::worker:
			thread_role_name_string = "Worker";
			break;
		case nano::thread_role::name::request_aggregator:
			thread_role_name_string = "Req aggregator";
			break;
		case nano::thread_role::name::state_block_signature_verification:
			thread_role_name_string = "State block sig";
			break;
		case nano::thread_role::name::epoch_upgrader:
			thread_role_name_string = "Epoch upgrader";
			break;
		case nano::thread_role::name::db_parallel_traversal:
			thread_role_name_string = "DB par traversl";
			break;
		case nano::thread_role::name::election_scheduler:
			thread_role_name_string = "Election Sched";
			break;
		case nano::thread_role::name::unchecked:
			thread_role_name_string = "Unchecked";
			break;
		case nano::thread_role::name::backlog_population:
			thread_role_name_string = "Backlog";
			break;
		default:
			debug_assert (false && "nano::thread_role::get_string unhandled thread role");
	}

	/*
	 * We want to constrain the thread names to 15
	 * characters, since this is the smallest maximum
	 * length supported by the platforms we support
	 * (specifically, Linux)
	 */
	debug_assert (thread_role_name_string.size () < 16);
	return (thread_role_name_string);
}

std::string nano::thread_role::get_string ()
{
	return get_string (current_thread_role);
}

void nano::thread_role::set (nano::thread_role::name role)
{
	auto thread_role_name_string (get_string (role));

	nano::thread_role::set_os_name (thread_role_name_string);

	current_thread_role = role;
}

void nano::thread_attributes::set (boost::thread::attributes & attrs)
{
	auto attrs_l (&attrs);
	attrs_l->set_stack_size (8000000); // 8MB
}

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a) :
	io_guard (boost::asio::make_work_guard (io_ctx_a))
{
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	for (auto i (0u); i < service_threads_a; ++i)
	{
		threads.emplace_back (attrs, [&io_ctx_a] () {
			nano::thread_role::set (nano::thread_role::name::io);
			try
			{
#if NANO_ASIO_HANDLER_TRACKING == 0
				io_ctx_a.run ();
#else
				nano::timer<> timer;
				timer.start ();
				while (true)
				{
					timer.restart ();
					// Run at most 1 completion handler and record the time it took to complete (non-blocking)
					auto count = io_ctx_a.poll_one ();
					if (count == 1 && timer.since_start ().count () >= NANO_ASIO_HANDLER_TRACKING)
					{
						auto timestamp = std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
						std::cout << (boost::format ("[%1%] io_thread held for %2%ms") % timestamp % timer.since_start ().count ()).str () << std::endl;
					}
					// Sleep for a bit to give more time slices to other threads
					std::this_thread::sleep_for (std::chrono::milliseconds (5));
					std::this_thread::yield ();
				}
#endif
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw;
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
				 * In a release build, catch and swallow the
				 * io_context exception, in debug mode pass it
				 * on
				 */
				throw;
#endif
			}
		});
	}
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void nano::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}

nano::thread_pool::thread_pool (unsigned num_threads, nano::thread_role::name thread_name) :
	num_threads (num_threads),
	thread_pool_m (std::make_unique<boost::asio::thread_pool> (num_threads))
{
	set_thread_names (num_threads, thread_name);
}

nano::thread_pool::~thread_pool ()
{
	stop ();
}

void nano::thread_pool::stop ()
{
	nano::unique_lock<nano::mutex> lk (mutex);
	if (!stopped)
	{
		stopped = true;
#if defined(BOOST_ASIO_HAS_IOCP)
		// A hack needed for Windows to prevent deadlock during destruction, described here: https://github.com/chriskohlhoff/asio/issues/431
		boost::asio::use_service<boost::asio::detail::win_iocp_io_context> (*thread_pool_m).stop ();
#endif
		lk.unlock ();
		thread_pool_m->stop ();
		thread_pool_m->join ();
		lk.lock ();
		thread_pool_m = nullptr;
	}
}

void nano::thread_pool::push_task (std::function<void ()> task)
{
	++num_tasks;
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped)
	{
		boost::asio::post (*thread_pool_m, [this, task] () {
			task ();
			--num_tasks;
		});
	}
}

void nano::thread_pool::add_timed_task (std::chrono::steady_clock::time_point const & expiry_time, std::function<void ()> task)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped && thread_pool_m)
	{
		auto timer = std::make_shared<boost::asio::steady_timer> (thread_pool_m->get_executor (), expiry_time);
		timer->async_wait ([this, task, timer] (boost::system::error_code const & ec) {
			if (!ec)
			{
				push_task (task);
			}
		});
	}
}

unsigned nano::thread_pool::get_num_threads () const
{
	return num_threads;
}

uint64_t nano::thread_pool::num_queued_tasks () const
{
	return num_tasks;
}

// Set the names of all the threads in the thread pool for easier identification
void nano::thread_pool::set_thread_names (unsigned num_threads, nano::thread_role::name thread_name)
{
	std::vector<std::promise<void>> promises (num_threads);
	std::vector<std::future<void>> futures;
	futures.reserve (num_threads);
	std::transform (promises.begin (), promises.end (), std::back_inserter (futures), [] (auto & promise) {
		return promise.get_future ();
	});

	for (auto i = 0u; i < num_threads; ++i)
	{
		boost::asio::post (*thread_pool_m, [&promise = promises[i], thread_name] () {
			nano::thread_role::set (thread_name);
			promise.set_value ();
		});
	}

	// Wait until all threads have finished
	for (auto & future : futures)
	{
		future.wait ();
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (thread_pool & thread_pool, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "count", thread_pool.num_queued_tasks (), sizeof (std::function<void ()>) }));
	return composite;
}
