# Testing less-than comparison on transactions
function test_comparisons()
    T = transaction{transaction_type_default}
    function first(values)
        flow_control.first(bucket(ds.SortedSet{T}(values), 0))
    end

    # Highest tally first
    tally_values = [transaction(9, 1, 1, 1, 1), transaction(4, 1, 1, 1, 1)]
    @Test.test isless(tally_values[1], tally_values[2])
    @Test.test first(tally_values) == first(reverse(tally_values))
    # Then highest balance or amount
    balance_values = [transaction(1, 9, 1, 1, 1), transaction(1, 4, 1, 1, 1)]
    @Test.test isless(balance_values[1], balance_values[2])
    @Test.test first(balance_values) == first(reverse(balance_values))
    amount_values = [transaction(1, 1, 9, 1, 1), transaction(1, 1, 4, 1, 1)]
    @Test.test isless(amount_values[1], amount_values[2])
    @Test.test first(amount_values) == first(reverse(amount_values))
    # Then LRU account
    lru_values = [transaction(1, 1, 1, 4, 1), transaction(1, 1, 1, 9, 1)]
    @Test.test isless(lru_values[1], lru_values[2])
    @Test.test first(lru_values) == first(reverse(lru_values))
    # Then PoW difficulty
    difficulty_values = [transaction(1, 1, 1, 1, 9), transaction(1, 1, 1, 1, 4)]
    @Test.test isless(difficulty_values[1], difficulty_values[2])
    @Test.test first(difficulty_values) == first(reverse(difficulty_values))
end

function test_copy_malleable()
    t1 = transaction(1, 1, 1, 1, 1, type=UInt8)
    t2 = copy_malleable(t1)

    @Test.test t1.tx == t2.tx
    @Test.test t1.amount == t2.amount
    @Test.test t1.balance == t2.balance
    @Test.test t1.difficulty == t2.difficulty

    @Test.test t1.lru ≠ t2.lru
    @Test.test t1.tally ≠ t2.tally
end

function test_malleability()
    test_copy_malleable()
end

function test_transaction()
    test_comparisons()
    test_malleability()
end

function test_bucket()
    # Test that 4 buckets divides the transaction_type keyspace in to expected values
    @Test.test collect(node_buckets(Int8, 4)) == [0, 31, 62, 93]

    n = node(type = Int8, bucket_count = 4)
    #Test that the bucket function finds the correct bucket for various values
    @Test.test bucket_range(n, transaction(1, 1, 1, 1, 1)) == 0
    @Test.test bucket_range(n, transaction(1, 31, 1, 1, 1)) == 31
    @Test.test bucket_range(n, transaction(1, 1, 31, 1, 1)) == 31
    @Test.test bucket_range(n, transaction(1, 1, 127, 1, 1)) == 93
    @Test.test element_type(bucket(type = Int32)) == Int32
end

function test_working_set()
    n = node()
    w1 = working_set(n)
    # Working set initially contains nothing since all buckets are empty
    @Test.test isempty(w1)
    tx = transaction(1, 1, 1, 1, 1)
    # Put something in a bucket
    insert!(n, tx)
    # Now working set should contain the item inserted
    w2 = working_set(n)
    @Test.test !isempty(w2)
    @Test.test tx in w2
end

function test_in_node()
    n = node()
    tx = transaction(1, 1, 1, 1, 1)
    @Test.test tx ∉ n
    insert!(n, tx)
    @Test.test tx ∈ n
end

function test_delete!()
    n = node()
    tx = transaction(1, 1, 1, 1, 1)
    insert!(n, tx)
    @Test.test !isempty(working_set(n))
    delete!(n, tx)
    @Test.test isempty(working_set(n))
end

function test_node()
    test_working_set()
    test_in_node()
    test_delete!()
end

function test_confirmed_set()
    n = network(node_count = 4)
    # Network with no transactions starts out empty
    @Test.test isempty(confirmed_set(n))
    tx = transaction(1, 1, 1, 1, 1)
    push!(n, tx)
    for node in n.nodes
        copy_global!(n, node)
    end
    s = confirmed_set(n)
    @Test.test !isempty(s)
    @Test.test tx in s
end

function test_delete!_network()
    n = network()
    tx = transaction(1, 1, 1, 1, 1)
    push!(n, tx)
    for node in n.nodes
        insert!(node, tx)
    end
    @Test.test all(node -> tx ∈ node, n.nodes)
    delete!(n, tx)
    @Test.test all(node -> tx ∉ node, n.nodes)
end

function test_network()
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
    test_confirmed_set()
    test_delete!_network()
end

function test_network_push!_in()
    n = network()
    tx1 = transaction(1, 1, 1, 1, 1)
    tx2 = transaction(2, 2, 2, 2, 2)
    push!(n, tx1)
    @Test.test tx1 in n
    @Test.test !in(tx2, n)
end

function test_copy_global()
    n = network()
    tx = transaction(1, 1, 1, 1, 1)
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

function test_copy_peer()
    n = network(node_count = 1)
    node_source = n.nodes[1]
    node_destination = node()
    tx = transaction(1, 1, 1, 1, 1)
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

function test_delete_confirmed()
    n = network()
    tx = transaction(1, 1, 1, 1, 1)
    insert!(n.transactions, tx)
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

function test_state_transitions()
    test_network_push!_in()
    test_copy_global()
    test_copy_peer()
    test_delete_confirmed()
end

# Perform all of the random state transitions
function test_rand_all()
    n = network()
    for (name, op) in all_ops
        op(n)
    end
end

function test()
    test_transaction()
    test_bucket()
    test_node()
    test_network()
    test_state_transitions()
    test_rand_all()
end