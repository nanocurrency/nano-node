#pragma once

#include <nano/node/repcrawler.hpp>

#include <unordered_map>

namespace nano
{
class election;
class node;
/** This class accepts elections that need further votes before they can be confirmed and bundles them in to single confirm_req packets */
class confirmation_solicitor final
{
	class request
	{
	public:
		std::shared_ptr<nano::transport::channel> channel;
		std::shared_ptr<nano::election> election;
		bool operator== (nano::confirmation_solicitor::request const & other_a) const
		{
			return *channel == *other_a.channel && election == other_a.election;
		}
	};
	class request_hash
	{
	public:
		size_t operator() (nano::confirmation_solicitor::request const & item_a) const
		{
			return std::hash<std::shared_ptr<nano::election>> () (item_a.election) ^ std::hash<nano::transport::channel> () (*item_a.channel);
		}
	};

public:
	confirmation_solicitor (nano::node &);
	/** Prepare object for batching election confirmation requests*/
	void prepare ();
	/** Add an election that needs to be confirmed */
	void add (std::shared_ptr<nano::election>);
	/** Bundle hashes together for identical channels in to a single confirm_req by hash packet */
	void flush ();

private:
	static size_t constexpr max_confirm_req_batches = 20;
	static size_t constexpr max_confirm_req = 5;
	static size_t constexpr max_block_broadcasts = 30;
	int rebroadcasted{ 0 };
	nano::node & node;
	std::vector<nano::representative> representatives;
	/** Unique channel/hash to be requested */
	std::unordered_set<request, nano::confirmation_solicitor::request_hash> requests;
	bool prepared{ false };
};
}
