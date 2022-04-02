module flow_control
import Pkg; Pkg.add("DataStructures");
import DataStructures as ds
import Base.first, Base.delete!, Base.in, Base.isempty, Base.isless, Base.length, Base.lt, Base.insert!, Base.print, Base.push!
import Test
import Plots

const transaction_type_default = UInt64
const bucket_max_default = 16
const bucket_count_default = 32
const node_count_default = 4

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
    max
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

function bucket(; type = transaction_type_default, bucket_max = bucket_max_default)
    bucket(ds.SortedSet{transaction{type}}(), bucket_max)
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
function node(; type = transaction_type_default, bucket_count = bucket_count_default, bucket_max = bucket_max_default)
    init = ds.SortedDict{type, bucket}()
    for k in node_buckets(type, bucket_count)
        push!(init, k => bucket(type = type, bucket_max = bucket_max))
    end
    node(init)
end

# node operations

function bucket_range(n::node, t::transaction)
    ds.deref_key((n.buckets, ds.searchsortedlast(n.buckets, weight(t))))
end

function insert!(n::node, t::transaction)
    b = n.buckets[bucket_range(n, t)]
    insert!(b.transactions, t)
    if length(b.transactions) > b.max
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
    delete!(n.buckets[bucket_range(n, transaction)].transactions, transaction)
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
    inserted::UInt
end

struct network{T}
    nodes::Vector{node{T}}
    transactions::ds.SortedSet{transaction{T}}
    stats::stat_struct
end

function network(; type = transaction_type_default, node_count = node_count_default, bucket_count = bucket_count_default, bucket_max = bucket_max_default)
    nodes = []
    for i = 0:node_count - 1
        push!(nodes, node(type = type, bucket_count = bucket_count, bucket_max = bucket_max))
    end
    transactions = ds.SortedSet{transaction{type}}()
    network{type}(nodes, transactions, stat_struct(0, 0))
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
    result = ds.SortedDict{transaction_type(n), UInt32}()
    for i in n.nodes
        s = sizes(i)
        for (b, l) = s
            result[b] = Base.get(result, b, 0) + l
        end
    end
    result
end

function print(n::network)
    h = bucket_histogram(n)
    print("l:", length(n.transactions), " d:", n.stats.deleted, ' ', h, '\n')
end

function transaction_type(n::network{T}) where{T}
    T
end

# ------------------------------------------------------------
# Flow control state transitions

# Add a transaction to the network via adding it to the network's global set of transactions
function push!(n::network, transaction)
    push!(n.transactions, transaction)
    n.stats.inserted += 1
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

function normalize_for_weight(val)
    balance = val ≠ 0 ? rand(0:(val - 1)) : 0
    (balance, val - balance)
end

function op_push!(n::network)
    t = transaction_type(n)
    randval = () -> rand(typemin(t):typemax(t))
    (balance, amount) = normalize_for_weight(randval())
    tx = transaction{t}(randval(), balance, amount, randval(), randval())
    push!(n, tx)
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

all_ops = ['i' => op_push!, 'g' => op_copy_global!, 'p' => op_copy_peer!, 'd' => delete_confirmed!]
no_insert_ops = ['g' => op_copy_global!, 'p' => op_copy_peer!, 'd' => delete_confirmed!]

function mutate(n::network)
    rand(all_ops).second(n)
end

function drain(n::network)
    # Run all ops except generating new transactions and the network should empty eventually
    while !isempty(n.transactions)
        rand(no_insert_ops).second(n)
    end
end

include("tests.jl")

function stress(node_count, bucket_count, bucket_max; type = transaction_type_default)
    series = []
    n = network(node_count = node_count, bucket_count = bucket_count, bucket_max = bucket_max, type = type)
    for i = iterations
        mutate(n)
        push!(series, n.stats.deleted)
    end
    (n, series)
end

function stress_bucket_count()
    y = []
    #x = map((val) -> 2^val, 2:8)
    x = 1:64
    iteration_count = 5_000
    for bucket_count = x
        print(bucket_count, ' ')
        n = network(bucket_count = bucket_count)
        count = 0
        while n.stats.deleted < iteration_count
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmations(" * string(iteration_count) * ") by bucket count", xlabel = "Bucket max", ylabel = "Operations")
    # Asymptote should drive a value for bucket_count_default. Smaller gives better simulation throughput.

end

function stress_bucket_max()
    y = []
    #x = map((val) -> 2^val, 2:6)
    x = 1:16
    iteration_count = 1_000
    for bucket_max = x
        print(bucket_max, ' ')
        n = network(bucket_max = bucket_max)
        count = 0
        while n.stats.deleted < iteration_count
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmations(" * string(iteration_count) * ") by bucket max", xlabel = "Bucket max", ylabel = "Operations")
    # Asymptote should drive a value for bucket_max_default. Smaller gives better simulation throughput.
end

function stress_node_count_iterations()
    y = []
    #x = collect(2^val for val = 2:5)
    #x = collect(2^val for val = 2:8)
    x = 4:64
    for i = x
        n = network(node_count = i)
        count = 0
        print(i, ' ')
        while n.stats.deleted == 0
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmation by node count", xlabel = "Nodes", ylabel = "Operations")
end

function stress_type()
    types = [UInt8, UInt16, UInt32, UInt64]
    ys = []
    x = 1:10_000
    labels = []
    for type = types
        n = network(type = type)
        series = []
        for _ in x
            mutate(n)
            push!(series, n.stats.deleted)
        end
        push!(ys, series)
        push!(labels, string(type))
    end
    labels = permutedims(labels)
    Plots.plot(x, ys, label = labels, xlabel = "Operations", ylabel = "Confirmed")
    # Asymptote should drive a type for transaction_type_default.
end

function stress()
    test()
 
    #stress_type()
    stress_node_count_iterations()
    #stress_bucket_max()
    #stress_bucket_count()
end

end #module

flow_control.test()

#flow_control.stress()

