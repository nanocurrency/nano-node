		while (true)
		{
			if (!node.active.mutex.try_lock ())
			{
				continue;
			}
			if (!node.alarm.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				continue;
			}
			if (!node.block_processor.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				node.alarm.mutex.unlock ();
				continue;
			}
			if (!node.bootstrap.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				node.alarm.mutex.unlock ();
				node.block_processor.mutex.unlock ();
				continue;
			}
			if (!node.bootstrap_initiator.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				node.alarm.mutex.unlock ();
				node.block_processor.mutex.unlock ();
				node.bootstrap.mutex.unlock ();
				continue;
			}
			if (!node.gap_cache.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				node.alarm.mutex.unlock ();
				node.block_processor.mutex.unlock ();
				node.bootstrap.mutex.unlock ();
				node.bootstrap_initiator.mutex.unlock ();
				continue;
			}
			if (!node.vote_processor.mutex.try_lock ())
			{
				node.active.mutex.unlock ();
				node.alarm.mutex.unlock ();
				node.block_processor.mutex.unlock ();
				node.bootstrap.mutex.unlock ();
				node.bootstrap_initiator.mutex.unlock ();
				node.gap_cache.mutex.unlock ();
				continue;
			}
			break;
		}
		response_l.put ("node.gap_cache.blocks", node.gap_cache.blocks.size ());
		response_l.put ("node.active.roots", node.active.roots.size ());
		response_l.put ("node.active.blocks", node.active.blocks.size ());
		response_l.put ("node.active.confirmed", node.active.confirmed.size ());
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.clients", node.bootstrap_initiator.attempt->clients.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.pulls", node.bootstrap_initiator.attempt->pulls.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.bulk_push_targets", node.bootstrap_initiator.attempt->bulk_push_targets.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.lazy_blocks", node.bootstrap_initiator.attempt->lazy_blocks.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.lazy_state_unknown", node.bootstrap_initiator.attempt->lazy_state_unknown.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.lazy_balances", node.bootstrap_initiator.attempt->lazy_balances.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.lazy_keys", node.bootstrap_initiator.attempt->lazy_keys.size ());
		}
		if (node.bootstrap_initiator.attempt)
		{
			response_l.put ("node.bootstrap_initiator.attempt.lazy_pulls", node.bootstrap_initiator.attempt->lazy_pulls.size ());
		}
		response_l.put ("node.bootstrap.connections", node.bootstrap.connections.size ());
		response_l.put ("node.vote_processor.votes", node.vote_processor.votes.size ());
		response_l.put ("node.vote_processor.representatives_1", node.vote_processor.representatives_1.size ());
		response_l.put ("node.vote_processor.representatives_2", node.vote_processor.representatives_2.size ());
		response_l.put ("node.vote_processor.representatives_3", node.vote_processor.representatives_3.size ());
		response_l.put ("node.block_processor.state_blocks", node.block_processor.state_blocks.size ());
		response_l.put ("node.block_processor.blocks", node.block_processor.blocks.size ());
		response_l.put ("node.block_processor.blocks_hashes", node.block_processor.blocks_hashes.size ());
		response_l.put ("node.block_processor.forced", node.block_processor.forced.size ());
		response_l.put ("node.alarm.operations", node.alarm.operations.size ());
		node.active.mutex.unlock ();
		node.alarm.mutex.unlock ();
		node.block_processor.mutex.unlock ();
		node.bootstrap.mutex.unlock ();
		node.bootstrap_initiator.mutex.unlock ();
		node.gap_cache.mutex.unlock ();
		node.vote_processor.mutex.unlock ();
