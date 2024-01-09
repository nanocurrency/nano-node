#include <nano/node/election.hpp>
#include <nano/node/ipc/action_handler.hpp>
#include <nano/node/ipc/flatbuffers_handler.hpp>
#include <nano/node/ipc/flatbuffers_util.hpp>
#include <nano/node/ipc/ipc_broker.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/node.hpp>

nano::ipc::broker::broker (nano::node & node_a) :
	node (node_a)
{
}

std::shared_ptr<flatbuffers::Parser> nano::ipc::subscriber::get_parser (nano::ipc::ipc_config const & ipc_config_a)
{
	if (!parser)
	{
		parser = nano::ipc::flatbuffers_handler::make_flatbuffers_parser (ipc_config_a);
	}
	return parser;
}

void nano::ipc::broker::start ()
{
	node.observers.blocks.add ([this_l = shared_from_this ()] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a) {
		debug_assert (status_a.type != nano::election_status_type::ongoing);

		try
		{
			// The subscriber(s) may be gone after the count check, but the only consequence
			// is that broadcast is called only to not find any live sessions.
			if (this_l->confirmation_subscriber_count () > 0)
			{
				auto confirmation (std::make_shared<nanoapi::EventConfirmationT> ());

				confirmation->account = account_a.to_account ();
				confirmation->amount = amount_a.to_string_dec ();
				switch (status_a.type)
				{
					case nano::election_status_type::active_confirmed_quorum:
						confirmation->confirmation_type = nanoapi::TopicConfirmationType::TopicConfirmationType_active_quorum;
						break;
					case nano::election_status_type::active_confirmation_height:
						confirmation->confirmation_type = nanoapi::TopicConfirmationType::TopicConfirmationType_active_confirmation_height;
						break;
					case nano::election_status_type::inactive_confirmation_height:
						confirmation->confirmation_type = nanoapi::TopicConfirmationType::TopicConfirmationType_inactive;
						break;
					default:
						debug_assert (false);
						break;
				};
				confirmation->confirmation_type = nanoapi::TopicConfirmationType::TopicConfirmationType_active_quorum;
				confirmation->block = nano::ipc::flatbuffers_builder::block_to_union (*status_a.winner, amount_a, is_state_send_a, is_state_epoch_a);
				confirmation->election_info = std::make_unique<nanoapi::ElectionInfoT> ();
				confirmation->election_info->duration = status_a.election_duration.count ();
				confirmation->election_info->time = status_a.election_end.count ();
				confirmation->election_info->tally = status_a.tally.to_string_dec ();
				confirmation->election_info->block_count = status_a.block_count;
				confirmation->election_info->voter_count = status_a.voter_count;
				confirmation->election_info->request_count = status_a.confirmation_request_count;

				this_l->broadcast (confirmation);
			}
		}
		catch (nano::error const & err)
		{
			this_l->node.nlogger.error (nano::log::type::ipc, "Could not broadcast message: {}", err.get_message ());
		}
	});
}

template <typename COLL, typename TOPIC_TYPE>
void subscribe_or_unsubscribe (nano::nlogger & nlogger, COLL & subscriber_collection, std::weak_ptr<nano::ipc::subscriber> const & subscriber_a, TOPIC_TYPE topic_a)
{
	// Evict subscribers from dead sessions. Also remove current subscriber if unsubscribing.
	subscriber_collection.erase (std::remove_if (subscriber_collection.begin (), subscriber_collection.end (),
								 [&nlogger = nlogger, topic_a, subscriber_a] (auto & sub) {
									 bool remove = false;
									 auto subscriber_l = sub.subscriber.lock ();
									 if (subscriber_l)
									 {
										 if (auto calling_subscriber_l = subscriber_a.lock ())
										 {
											 remove = topic_a->unsubscribe && subscriber_l->get_id () == calling_subscriber_l->get_id ();
											 if (remove)
											 {
												 nlogger.info (nano::log::type::ipc, "Subscriber ubsubscribed #{}", calling_subscriber_l->get_id ());
											 }
										 }
									 }
									 else
									 {
										 remove = true;
									 }
									 return remove;
								 }),
	subscriber_collection.end ());

	if (!topic_a->unsubscribe)
	{
		subscriber_collection.emplace_back (subscriber_a, topic_a);
	}
}

void nano::ipc::broker::subscribe (std::weak_ptr<nano::ipc::subscriber> const & subscriber_a, std::shared_ptr<nanoapi::TopicConfirmationT> const & confirmation_a)
{
	auto subscribers = confirmation_subscribers.lock ();
	subscribe_or_unsubscribe (node.nlogger, subscribers.get (), subscriber_a, confirmation_a);
}

void nano::ipc::broker::broadcast (std::shared_ptr<nanoapi::EventConfirmationT> const & confirmation_a)
{
	using Filter = nanoapi::TopicConfirmationTypeFilter;
	decltype (confirmation_a->election_info) election_info;
	nanoapi::BlockUnion block;
	auto itr (confirmation_subscribers->begin ());
	while (itr != confirmation_subscribers->end ())
	{
		if (auto subscriber_l = itr->subscriber.lock ())
		{
			auto should_filter = [this, &itr, confirmation_a] () {
				debug_assert (itr->topic->options != nullptr);
				auto conf_filter (itr->topic->options->confirmation_type_filter);

				bool should_filter_conf_type_l (true);
				bool all_filter = conf_filter == Filter::TopicConfirmationTypeFilter_all;
				bool inactive_filter = conf_filter == Filter::TopicConfirmationTypeFilter_inactive;
				bool active_filter = conf_filter == Filter::TopicConfirmationTypeFilter_active || conf_filter == Filter::TopicConfirmationTypeFilter_active_quorum || conf_filter == Filter::TopicConfirmationTypeFilter_active_confirmation_height;

				if ((confirmation_a->confirmation_type == nanoapi::TopicConfirmationType::TopicConfirmationType_active_quorum || confirmation_a->confirmation_type == nanoapi::TopicConfirmationType::TopicConfirmationType_active_confirmation_height) && (all_filter || active_filter))
				{
					should_filter_conf_type_l = false;
				}
				else if (confirmation_a->confirmation_type == nanoapi::TopicConfirmationType::TopicConfirmationType_inactive && (all_filter || inactive_filter))
				{
					should_filter_conf_type_l = false;
				}

				bool should_filter_account_l (itr->topic->options->all_local_accounts || !itr->topic->options->accounts.empty ());
				auto state (confirmation_a->block.AsBlockState ());
				if (state && !should_filter_conf_type_l)
				{
					if (itr->topic->options->all_local_accounts)
					{
						auto transaction_l (this->node.wallets.tx_begin_read ());
						nano::account source_l{};
						nano::account destination_l{};
						auto decode_source_ok_l (!source_l.decode_account (state->account));
						auto decode_destination_ok_l (!destination_l.decode_account (state->link_as_account));
						(void)decode_source_ok_l;
						(void)decode_destination_ok_l;
						debug_assert (decode_source_ok_l && decode_destination_ok_l);
						if (this->node.wallets.exists (transaction_l, source_l) || this->node.wallets.exists (transaction_l, destination_l))
						{
							should_filter_account_l = false;
						}
					}

					if (std::find (itr->topic->options->accounts.begin (), itr->topic->options->accounts.end (), state->account) != itr->topic->options->accounts.end () || std::find (itr->topic->options->accounts.begin (), itr->topic->options->accounts.end (), state->link_as_account) != itr->topic->options->accounts.end ())
					{
						should_filter_account_l = false;
					}
				}

				return should_filter_conf_type_l || should_filter_account_l;
			};
			// Apply any filters
			auto & options (itr->topic->options);
			if (options)
			{
				if (!options->include_election_info)
				{
					election_info = std::move (confirmation_a->election_info);
					confirmation_a->election_info = nullptr;
				}
				if (!options->include_block)
				{
					block = confirmation_a->block;
					confirmation_a->block.Reset ();
				}
			}
			if (!options || !should_filter ())
			{
				auto fb (nano::ipc::flatbuffer_producer::make_buffer (*confirmation_a));

				if (subscriber_l->get_active_encoding () == nano::ipc::payload_encoding::flatbuffers_json)
				{
					auto parser (subscriber_l->get_parser (node.config.ipc_config));

					// Convert response to JSON
					auto json (std::make_shared<std::string> ());
					if (!flatbuffers::GenerateText (*parser, fb->GetBufferPointer (), json.get ()))
					{
						throw nano::error ("Couldn't serialize response to JSON");
					}

					subscriber_l->async_send_message (reinterpret_cast<uint8_t const *> (json->data ()), json->size (), [json] (nano::error const & err) {});
				}
				else
				{
					subscriber_l->async_send_message (fb->GetBufferPointer (), fb->GetSize (), [fb] (nano::error const & err) {});
				}
			}

			// Restore full object, the next subscriber may request it
			if (election_info)
			{
				confirmation_a->election_info = std::move (election_info);
			}
			if (block.type != nanoapi::Block::Block_NONE)
			{
				confirmation_a->block = block;
			}

			++itr;
		}
		else
		{
			itr = confirmation_subscribers->erase (itr);
		}
	}
}

std::size_t nano::ipc::broker::confirmation_subscriber_count () const
{
	return confirmation_subscribers->size ();
}

void nano::ipc::broker::service_register (std::string const & service_name_a, std::weak_ptr<nano::ipc::subscriber> const & subscriber_a)
{
	if (auto subscriber_l = subscriber_a.lock ())
	{
		subscriber_l->set_service_name (service_name_a);
	}
}

void nano::ipc::broker::service_stop (std::string const & service_name_a)
{
	auto subscribers = service_stop_subscribers.lock ();
	for (auto & subcription : subscribers.get ())
	{
		if (auto subscriber_l = subcription.subscriber.lock ())
		{
			if (subscriber_l->get_service_name () == service_name_a)
			{
				nanoapi::EventServiceStopT event_stop;
				auto fb (nano::ipc::flatbuffer_producer::make_buffer (event_stop));
				subscriber_l->async_send_message (fb->GetBufferPointer (), fb->GetSize (), [fb] (nano::error const & err) {});

				break;
			}
		}
	}
}

void nano::ipc::broker::subscribe (std::weak_ptr<nano::ipc::subscriber> const & subscriber_a, std::shared_ptr<nanoapi::TopicServiceStopT> const & service_stop_a)
{
	auto subscribers = service_stop_subscribers.lock ();
	subscribe_or_unsubscribe (node.nlogger, subscribers.get (), subscriber_a, service_stop_a);
}
