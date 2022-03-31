module flow_control
import Pkg; Pkg.add("DataStructures");
import DataStructures as ds
import Base.first, Base.delete!, Base.in, Base.isempty, Base.isless, Base.length, Base.lt, Base.insert!, Base.push!
import Test

# Transaction properties used to bucket and sort transactions
struct transaction{T<:Integer}
    # Unique transaction id
    tx::UInt64
    tally::T
    balance::T
    amount::T
    lru::T
    difficulty::T
    transaction{T}(tally, balance, amount, lru, difficulty) where{T<:Integer} = new{T}(rand(UInt64), tally, balance, amount, lru, difficulty)
end

const transaction_type = UInt8

function isless(lhs::flow_control.transaction, rhs::flow_control.transaction)
    lhs_w = flow_control.weight(lhs)
    rhs_w = flow_control.weight(rhs)
    (lhs.tally != rhs.tally) ? 
        # First sort based on highest tally
        (lhs.tally > rhs.tally) :
         (lhs_w != rhs_w) ? 
            # Then sort by highest weight
            (lhs_w > rhs_w) :
            (lhs.lru != rhs.lru) ?
                # Then sort by LRU account
                lhs.lru < rhs.lru :
                (lhs.difficulty != rhs.difficulty) ?
                    # Then sort by difficulty
                    (lhs.difficulty > rhs.difficulty) :
                    false
end

struct transaction_sort <: Base.Order.Ordering
end

function weight(t::transaction)
    max(t.amount, t.balance)
end

struct bucket{T}
    transactions::ds.SortedSet{transaction{T}}
end

function first(b::bucket)::transaction
    ds.first(b.transactions)
end

function transaction_type(b::bucket{T}) where{T}
    T
end

function isempty(b::bucket)
    isempty(b.transactions)
end

function length(b::bucket)
    length(b.items)
end

function bucket(type)
    bucket(ds.SortedSet{transaction{type}}())
end

function bucket()
    bucket(transaction_type)
end

struct node{T}
    buckets::ds.SortedDict{T, bucket}
end

function node_buckets(type, count)
    result = ds.SortedSet{type}()
    for i = 0:count - 1
        insert!(result, i * (typemax(type) ÷ count))
    end
    result
end

function node_buckets(count)
    node_buckets(transaction_type, count)
end

# Divide the keyspace of transaction_type in to count buckets
function node(type, count)
    init = ds.SortedDict{type, bucket}()
    for k in node_buckets(type, count)
        push!(init, k => bucket())
    end
    node(init)
end

function node(count)
    node(transaction_type, count)
end

const node_bucket_count = 4

function node()
    node(node_bucket_count)
end

# node operations

function bucket(n::node, t::transaction)
    ds.deref_key((n.buckets, ds.searchsortedlast(n.buckets, weight(t))))
end

const bucket_max = 64

function insert!(n::node, t::transaction)
    b = n.buckets[bucket(n, t)]
    insert!(b.transactions, t)
    if length(b.transactions) > bucket_max
        delete!(b.transactions, last(b.transactions))
    end
end

function sizes(n::node)
    result = Dict{transaction_type(n), UInt16}()
    for i in n.buckets
        l = length(i.second.transactions)
        result[i.first] = l
    end
    result
end

function delete!(n::node, transaction)
    delete!(n.buckets[bucket(n, transaction)].transactions, transaction)
end

function in(transaction, n::node)
    any(b -> transaction ∈ b.second.transactions, n.buckets)
end

function transactions(node)
    result = copy(first(node.buckets).second.transactions)
    for (k, v) = node.buckets
        result = union(result, v.transactions)
    end
    result
end

function transaction_type(n::node{T}) where{T}
    return T
end

function working_set(node)
    result = ds.Set{transaction{transaction_type(node)}}()
    # Insert the highest priority transaction from each bucket
    for (k, v) = node.buckets
        if !isempty(v)
            push!(result, first(v))
        end
    end
    result
end

#node operations end

mutable struct stat_struct
    deleted::UInt
end

struct network{T}
    nodes::Vector{node{T}}
    transactions::ds.SortedSet{transaction{T}}
    stats::stat_struct
end

const network_node_count = 4

function network(type, node_count, bucket_size)
    nodes = []
    for i = 0:node_count - 1
        push!(nodes, node(type, bucket_size))
    end
    transactions = ds.SortedSet{transaction{type}}()
    network{type}(nodes, transactions, stat_struct(0))
end

function network(count, bucket_size)
    network(transaction_type, count, bucket_size)
end

function network(count)
    network(count, node_bucket_count)
end

function network()
    network(network_node_count)
end

function in(transaction, n::network)
    transaction in n.transactions
end

function quorum(n::network)
    ((2 * length(n.nodes)) ÷ 3) + 1
end

function confirmed_set(n::network)
    weights = Dict{transaction, UInt}()
    for node in n.nodes
        s = working_set(node)
        for tx in s
            w = get(weights, tx, 0)
            weights[tx] = w + 1
        end
    end
    result = Set{transaction}()
    for (tx, w) in weights
        if w > quorum(n)
            push!(result, tx)
        end
    end
    result
end

function delete!(n::network, transaction)
    @assert transaction ∈ n.transactions
    delete!(n.transactions, transaction)
    for node in n.nodes
        delete!(node, transaction)
    end
end

function bucket_histogram(n::network)
    result = Dict{transaction_type(n), UInt}()
    for i in n.nodes
        s = sizes(i)
        for (b, l) = s
            result[b] = Base.get(result, b, 0) + l
        end
    end
    result
end

function transaction_type(n::network{T}) where{T}
    T
end

# ------------------------------------------------------------
# Flow control state transitions

# Add a transaction to the network via adding it to the network's global set of transactions
function push!(n::network, transaction)
    push!(n.transactions, transaction)
end

# Copy a transaction from the global network transactions to node
function copy_global!(n::network, node)
    if !isempty(n.transactions)
        insert!(node, rand(n.transactions))
    end
end

# Copy the working set from another random peer to node
function copy_peer!(n::network, node)
    peer = rand(collect(n.nodes))
    for (k, b) in peer.buckets
        if !isempty(b)
            insert!(node, first(b))
        end
    end
end

function delete_confirmed!(n::network)
    c = confirmed_set(n)
    if !isempty(c)
        tx = rand(c)
        delete!(n, tx)
        n.stats.deleted += 1
    end
end

# State transitions end
# ------------------------------------------------------------

# Testing less-than comparison on transactions
function test_comparisons()
    T = transaction{transaction_type}
    function first(values)
        flow_control.first(bucket(ds.SortedSet{T}(values)))
    end

    # Highest tally first
    tally_values = [T(9, 1, 1, 1, 1), T(4, 1, 1, 1, 1)]
    @Test.test isless(tally_values[1], tally_values[2])
    @Test.test first(tally_values) == first(reverse(tally_values))
    # Then highest balance or amount
    balance_values = [T(1, 9, 1, 1, 1), T(1, 4, 1, 1, 1)]
    @Test.test isless(balance_values[1], balance_values[2])
    @Test.test first(balance_values) == first(reverse(balance_values))
    amount_values = [T(1, 1, 9, 1, 1), T(1, 1, 4, 1, 1)]
    @Test.test isless(amount_values[1], amount_values[2])
    @Test.test first(amount_values) == first(reverse(amount_values))
    # Then LRU account
    lru_values = [T(1, 1, 1, 4, 1), T(1, 1, 1, 9, 1)]
    @Test.test isless(lru_values[1], lru_values[2])
    @Test.test first(lru_values) == first(reverse(lru_values))
    # Then PoW difficulty
    difficulty_values = [T(1, 1, 1, 1, 9), T(1, 1, 1, 1, 4)]
    @Test.test isless(difficulty_values[1], difficulty_values[2])
    @Test.test first(difficulty_values) == first(reverse(difficulty_values))
end

function test_bucket()
    T = transaction{Int8}
    # Test that 4 buckets divides the transaction_type keyspace in to expected values
    @Test.test collect(node_buckets(Int8, 4)) == [0, 31, 62, 93]

    n = node(Int8, 4)
    #Test that the bucket function finds the correct bucket for various values
    @Test.test bucket(n, T(1, 1, 1, 1, 1)) == 0
    @Test.test bucket(n, T(1, 31, 1, 1, 1)) == 31
    @Test.test bucket(n, T(1, 1, 31, 1, 1)) == 31
    @Test.test bucket(n, T(1, 1, 127, 1, 1)) == 93
    @Test.test transaction_type(bucket(Int32)) == Int32
end

function test_confirmed_set()
    n = network(4)
    t = transaction{transaction_type(n)}
    # Network with no transactions starts out empty
    @Test.test isempty(confirmed_set(n))
    tx = t(1, 1, 1, 1, 1)
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
    t = transaction{transaction_type(n)}
    tx = t(1, 1, 1, 1, 1)
    push!(n, tx)
    for node in n.nodes
        insert!(node, tx)
    end
    @Test.test all(node -> tx ∈ node, n.nodes)
    delete!(n, tx)
    @Test.test all(node -> tx ∉ node, n.nodes)
end

function test_network()
    network1 = network(1)
    @Test.test keytype(network1.nodes[1].buckets) == UInt8
    @Test.test size(network1.nodes)[1] == 1
    @Test.test length(network1.nodes[1].buckets) == node_bucket_count
    network16 = network(16)
    @Test.test size(network16.nodes)[1] == 16
    @Test.test length(network16.nodes[1].buckets) == node_bucket_count
    network1_1 = network(1, 1)
    @Test.test size(network1_1.nodes)[1] == 1
    @Test.test length(network1_1.nodes[1].buckets) == 1
    # Test network construction with a wider value type
    network_big = network(Int16, 1, 1)
    @Test.test keytype(network_big.nodes[1].buckets) == Int16
    test_confirmed_set()
    test_delete!_network()
end

function test_working_set()
    n = node()
    T = transaction{transaction_type(n)}
    w1 = working_set(n)
    # Working set initially contains nothing since all buckets are empty
    @Test.test isempty(w1)
    tx = T(1, 1, 1, 1, 1)
    # Put something in a bucket
    insert!(n, tx)
    # Now working set should contain the item inserted
    w2 = working_set(n)
    @Test.test !isempty(w2)
    @Test.test tx in w2
end

function test_delete!()
    n = node()
    T = transaction{transaction_type(n)}
    tx = T(1, 1, 1, 1, 1)
    insert!(n, tx)
    @Test.test !isempty(working_set(n))
    delete!(n, tx)
    @Test.test isempty(working_set(n))
end

function test_in_node()
    n = node()
    t = transaction{transaction_type(n)}
    tx = t(1, 1, 1, 1, 1)
    @Test.test tx ∉ n
    insert!(n, tx)
    @Test.test tx ∈ n
end

function test_node()
    test_working_set()
    test_in_node()
    test_delete!()
end

function test_network_push!_in()
    type = transaction{transaction_type}
    n = network()
    tx1 = type(1, 1, 1, 1, 1)
    tx2 = type(2, 2, 2, 2, 2)
    push!(n, tx1)
    @Test.test tx1 in n
    @Test.test !in(tx2, n)
end

function test_copy_global()
    type = transaction{transaction_type}
    n = network()
    tx = type(1, 1, 1, 1, 1)
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
    type = transaction{transaction_type}
    n = network(1)
    node_source = n.nodes[1]
    node_destination = node()
    tx = type(1, 1, 1, 1, 1)
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
    t = transaction{transaction_type(n)}
    tx = t(1, 1, 1, 1, 1)
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

function op_push!(n::network)
    t = transaction_type(n)
    v = () -> rand(typemin(t):typemax(t))
    push!(n, transaction{t}(v(), v(), v(), v(), v()))
end

function op_push_bucket0!(n::network)
    t = transaction_type(n)
    v = () -> rand(typemin(t):typemax(t))
    push!(n, transaction{t}(v(), 0, 0, v(), v()))
end

function op_copy_global!(n::network)
    if !isempty(n.nodes)
        copy_global!(n, rand(n.nodes))
    end
end

function op_copy_peer!(n::network)
    if !isempty(n.nodes)
        copy_peer!(n, rand(n.nodes))
    end
end

function op_delete_confirmed!(n::network)
    delete_confirmed!(n)
end

all_ops = [op_push!, op_push_bucket0!, op_copy_global!, op_copy_peer!#=, op_delete_confirmed!=#]

# Perform all of the random state transitions
function test_rand_all()
    n = network()
    for op in all_ops
        op(n)
    end
end

function test()
    test_comparisons()
    test_bucket()
    test_node()
    test_network()
    test_state_transitions()
    test_rand_all()
end

function stress()
    test()
    n = network()
    for i = 1:50_000
        if i % 10000 == 0
            print(i, '\n', bucket_histogram(n), '\n')
        end
        rand(all_ops)(n)
    end
    n.stats
end

end #module

#for _ = 1:5
#flow_control.test()
#end

flow_control.stress()

