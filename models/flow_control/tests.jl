# Testing less-than comparison on transactions
function test_comparisons(t)
    T = transaction{t}
    function first(values)
        flow_control.first(bucket(ds.SortedSet{T}(values), 0))
    end

    # Highest tally first
    tally_values = [transaction(9, 1, 1, 1, 1, type = t), transaction(4, 1, 1, 1, 1, type = t)]
    @Test.test isless(tally_values[1], tally_values[2])
    @Test.test first(tally_values) == first(reverse(tally_values))
    # Then highest balance or amount
    balance_values = [transaction(1, 9, 1, 1, 1, type = t), transaction(1, 4, 1, 1, 1, type = t)]
    @Test.test isless(balance_values[1], balance_values[2])
    @Test.test first(balance_values) == first(reverse(balance_values))
    amount_values = [transaction(1, 1, 9, 1, 1, type = t), transaction(1, 1, 4, 1, 1, type = t)]
    @Test.test isless(amount_values[1], amount_values[2])
    @Test.test first(amount_values) == first(reverse(amount_values))
    # Then LRU account
    lru_values = [transaction(1, 1, 1, 4, 1, type = t), transaction(1, 1, 1, 9, 1, type = t)]
    @Test.test isless(lru_values[1], lru_values[2])
    @Test.test first(lru_values) == first(reverse(lru_values))
    # Then PoW difficulty
    difficulty_values = [transaction(1, 1, 1, 1, 9, type = t), transaction(1, 1, 1, 1, 4, type = t)]
    @Test.test isless(difficulty_values[1], difficulty_values[2])
    @Test.test first(difficulty_values) == first(reverse(difficulty_values))
end

function test_copy_malleability(t)
    t1 = transaction(1, 1, 1, 1, 1, type=t)
    t2 = copy(t1)

    @Test.test t1.tx == t2.tx
    @Test.test t1.amount == t2.amount
    @Test.test t1.balance == t2.balance
    @Test.test t1.difficulty == t2.difficulty

    @Test.test t1.lru ≠ t2.lru
    @Test.test t1.tally ≠ t2.tally
end

function test_malleability(t)
    test_copy_malleability(t)
end

function test_transitive(t)
    n = node(type = t)
    insert!(n, transaction(1, 1, 1, 1, 1, type = t))
    @Test.test !isempty(transactions(n) ∩ transactions(n))
end

function test_transaction(t)
    test_transitive(t)
    test_comparisons(t)
    test_malleability(t)
end

function test_bucket_max(t)
    b = bucket(type = t, bucket_max = 1)
    tx1 = transaction(1, 1, 1, 1, 1, type = t)
    tx2 = transaction(2, 2, 2, 2, 2, type = t)
    @Test.test !full(b)
    insert!(b, tx1)
    @Test.test length(b) == 1
    @Test.test full(b)
    insert!(b, tx2)
    @Test.test length(b) == 1
    @Test.test tx2 ∈ b
end

function test_bucket_transactions(t)
    b = bucket(type = t)
    @Test.test isempty(transactions(b))
    tx = transaction(1, 1, 1, 1, 1, type = t)
    insert!(b, tx)
    @Test.test !isempty(transactions(b))
    @Test.test tx ∈ transactions(b)
end

function test_bucket(t)
    # Test that 4 buckets divides the transaction_type keyspace in to expected values
    @Test.test collect(node_buckets(Int8, 4)) == [0, 31, 62, 93]

    n = node(type = Int8, bucket_count = 4)
    #Test that the bucket function finds the correct bucket for various values
    @Test.test bucket_range(n, transaction(1, 1, 1, 1, 1)) == 0
    @Test.test bucket_range(n, transaction(1, 31, 1, 1, 1)) == 31
    @Test.test bucket_range(n, transaction(1, 1, 31, 1, 1)) == 31
    @Test.test bucket_range(n, transaction(1, 1, 127, 1, 1)) == 93
    @Test.test element_type(bucket(type = Int32)) == Int32
    test_bucket_max(t)
    test_bucket_transactions(t)
end

function test_working_set(t)
    n = node(type = t)
    w1 = working_set(n)
    # Working set initially contains nothing since all buckets are empty
    @Test.test isempty(w1)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    # Put something in a bucket
    insert!(n, tx)
    # Now working set should contain the item inserted
    w2 = working_set(n)
    @Test.test !isempty(w2)
    @Test.test tx in w2
end

function test_in_node(t)
    n = node(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    @Test.test tx ∉ n
    insert!(n, tx)
    @Test.test tx ∈ n
end

function test_delete!(t)
    n = node(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    insert!(n, tx)
    @Test.test !isempty(working_set(n))
    delete!(n, tx)
    @Test.test isempty(working_set(n))
end

function test_node_transactions(t)
    n = node(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    @Test.test isempty(transactions(n))
    insert!(n, tx)
    @Test.test !isempty(transactions(n))
end

function test_node_full_count(t)
    n = node(type = t, bucket_max = 1)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    @Test.test load_factor(n) ≈ 0.0
    insert!(n, tx)
    @Test.test load_factor(n) ≈ 1.0 / length(n.buckets)
end

function test_node(t)
    test_working_set(t)
    test_in_node(t)
    test_delete!(t)
    test_node_transactions(t)
    test_node_full_count(t)
end

function test_confirmed_set(t)
    n = network(node_count = 4, type = t)
    # Network with no transactions starts out empty
    @Test.test isempty(confirmed_set(n))
    tx = transaction(1, 1, 1, 1, 1, type = t)
    push!(n, tx)
    for node in n.nodes
        copy_global!(n, node)
    end
    s = confirmed_set(n)
    #=print(n.transactions)
    for node in n.nodes
        print(node.buckets)
    end=#
    @Test.test !isempty(s)
    @Test.test tx in s
end

function test_delete!_network(t)
    n = network(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    push!(n, tx)
    for node in n.nodes
        insert!(node, tx)
    end
    @Test.test all(node -> tx ∈ node, n.nodes)
    delete!(n, tx)
    @Test.test all(node -> tx ∉ node, n.nodes)
end

function test_drain(t)
    n = network(type = t)
    push_rand!(n)
    @Test.test !isempty(n.transactions)
    drain(n)
    @Test.test isempty(n.transactions)
end

function test_live_set(t)
    n = network(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    push!(n, tx)
    @Test.test isempty(live_set(n))
    insert!(n.nodes[1], tx)
    live = live_set(n)
    @Test.test !isempty(live)
    @Test.test tx ∈ live
end

function test_abandoned_set(t)
    n = network(type = t, bucket_max = 1)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    @Test.test isempty(abandoned_set(n))
    push!(n, tx)
    abandoned = abandoned_set(n)
    @Test.test !isempty(abandoned)
    @Test.test tx ∈ abandoned
    node = n.nodes[1]
    insert!(node, tx)
    @Test.test isempty(abandoned_set(n))
end

function test_network_full_count(t)
    n = network(type = t, bucket_max = 1)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    @Test.test load_factor(n) ≈ 0.0
    insert!(n.nodes[1], tx)
    @Test.test load_factor(n) > 0.0 
end

function test_network(t)
    network1 = network(type = UInt8, node_count = 1)
    @Test.test keytype(network1.nodes[1].buckets) == UInt8
    @Test.test size(network1.nodes)[1] == 1
    @Test.test length(network1.nodes[1].buckets) == bucket_count_default
    network16 = network(node_count = 16)
    @Test.test size(network16.nodes)[1] == 16
    @Test.test length(network16.nodes[1].buckets) == bucket_count_default
    network1_1 = network(node_count = 1, bucket_count = 1)
    @Test.test size(network1_1.nodes)[1] == 1
    @Test.test length(network1_1.nodes[1].buckets) == 1
    # Test network construction with a wider value type
    network_big = network(type = Int16, node_count = 1, bucket_count = 1)
    @Test.test keytype(network_big.nodes[1].buckets) == Int16
    test_confirmed_set(t)
    test_delete!_network(t)
    test_drain(t)
    test_live_set(t)
    test_abandoned_set(t)
    test_network_full_count(t)
end

function test_network_push!_in(t)
    n = network(type = t)
    tx1 = transaction(1, 1, 1, 1, 1, type = t)
    tx2 = transaction(2, 2, 2, 2, 2, type = t)
    push!(n, tx1)
    @Test.test tx1 in n
    @Test.test !in(tx2, n)
end

function test_copy_global(t)
    n = network(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    push!(n, tx)
    node = n.nodes[1]
    intersection() = intersect(transactions(node), n.transactions)
    @Test.test isempty(intersection())
    copy_global!(n, node)
    i = intersection()
    @Test.test !isempty(i)
    @Test.test i == n.transactions
    nothing
end

function test_copy_peer(t)
    n = network(node_count = 1, type = t)
    node_source = n.nodes[1]
    node_destination = node(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    # Populate the working set of node1
    insert!(node_source, tx)
    # The destination working set starts out empty
    @Test.test isempty(working_set(node_destination))
    # Copy the working set from node1 to node2
    # node1 is the only node inside n
    copy_peer!(n, node_destination)
    # Retrieve the updated working set inside destination
    w = working_set(node_destination)
    @Test.test !isempty(w)
    # Ensure the working set contains what was inserted on node1
    @Test.test tx in w
end

function test_delete_confirmed(t)
    n = network(type = t)
    tx = transaction(1, 1, 1, 1, 1, type = t)
    push!(n.transactions, tx)
    insert!(n.nodes[1], tx)
    # Transaction starts out in the network
    @Test.test tx ∈ n
    delete_confirmed!(n)
    # Transaction is not removed because it is not in the confirmed_set i.e. it's not in a quorum of nodes
    for n in n.nodes
        insert!(n, tx)
    end
    delete_confirmed!(n)
    # Transaction is finally removed since it's on enough nodes.
    @Test.test tx ∉ n
end

function test_state_transitions(t)
    test_network_push!_in(t)
    test_copy_global(t)
    test_copy_peer(t)
    test_delete_confirmed(t)
end

# Perform all of the random state transitions
function test_rand_all(t)
    n = network(type = t)
    for op in mutate_ops
        op(n)
    end
end

function test_all(t)
    test_transaction(t)
    test_bucket(t)
    test_node(t)
    test_network(t)
    test_state_transitions(t)
    test_rand_all(t)
end

function test()
    for t in [UInt8, UInt64, UInt128]
        test_all(t)
    end
end
