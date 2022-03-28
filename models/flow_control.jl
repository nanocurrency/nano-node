module flow_control
import Pkg; Pkg.add("DataStructures");
import DataStructures as ds
import Base.isless
import Base.lt
import Base.insert!
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

const transaction_type = Int8

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
    items::ds.SortedSet{transaction{T}}
end

function first(b::bucket)::transaction
    ds.first(b.items)
end

function bucket(type)
    bucket(ds.SortedSet{transaction{type}}())
end

function bucket()
    bucket(transaction_type)
end

struct node{T}
    items::ds.SortedDict{T, bucket}
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
    node(bucket_count)
end

function bucket(n::node, t::transaction)
    ds.deref_key((n.items, ds.searchsortedlast(n.items, weight(t))))
end

function insert!(n::node, t::transaction)
    insert!(n.items[bucket(n, t)], t)
end

struct network{T}
    nodes::Vector{node{T}}
end

const network_node_count = 4

function network(type, count, bucket_size)
    nodes = []
    for i = 0:count - 1
        push!(nodes, node(type, bucket_size))
    end
    network{type}(nodes)
end

function network(count, bucket_size)
    network(transaction_type, count, bucket_size)
end

function network(count::Integer = 4)
    network(count, node_bucket_count)
end

function test_comparisons()
    T = transaction{transaction_type}
    function first(values)
        flow_control.first(bucket(ds.SortedSet{transaction{transaction_type}}(values)))
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
    @Test.test collect(node_buckets(4)) == [0, 31, 62, 93]
    
    n = node(4)
    #Test that the bucket function finds the correct bucket for various values
    @Test.test bucket(n, T(1, 1, 1, 1, 1)) == 0
    @Test.test bucket(n, T(1, 31, 1, 1, 1)) == 31
    @Test.test bucket(n, T(1, 1, 31, 1, 1)) == 31
    @Test.test bucket(n, T(1, 1, 127, 1, 1)) == 93
end

function test_network()
    network1 = network(1)
    @Test.test keytype(network1.items[1].items) == Int8
    @Test.test size(network1.items)[1] == 1
    @Test.test length(network1.items[1].items) == node_bucket_count
    network16 = network(16)
    @Test.test size(network16.items)[1] == 16
    @Test.test length(network16.items[1].items) == node_bucket_count
    network1_1 = network(1, 1)
    @Test.test size(network1_1.items)[1] == 1
    @Test.test length(network1_1.items[1].items) == 1
    # Test network construction with a wider value type
    network_big = network(Int16, 1, 1)
    @Test.test keytype(network_big.items[1].items) == Int16
end

function test()
    test_comparisons()
    test_bucket()
    test_network()
end

end #module

flow_control.test()

