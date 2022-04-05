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

# Divide the keyspace of transaction_type in to count buckets
function node(; type = transaction_type_default, bucket_count = bucket_count_default, bucket_max = bucket_max_default)
    init = ds.SortedDict{type, bucket}()
    for k in node_buckets(type, bucket_count)
        push!(init, k => bucket(type = type, bucket_max = bucket_max))
    end
    node(init)
end

function bucket_range(n::node, tx)
    ds.deref_key((n.buckets, ds.searchsortedlast(n.buckets, weight(tx))))
end

function insert!(n::node, tx)
    b = n.buckets[bucket_range(n, tx)]
    insert!(b, tx)
end

function sizes(n::node)
    result = Dict{element_type(n), UInt16}()
    for i in n.buckets
        l = length(i.second.transactions)
        result[i.first] = l
    end
    result
end

function delete!(n::node, tx)
    delete!(n.buckets[bucket_range(n, tx)].transactions, tx)
end

# Is the transaction tx in any bucket.
function in(tx, n::node)
    any(b -> tx ∈ b.second.transactions, n.buckets)
end

# The union of transactions in every bucket
function transactions(n::node)
    result = Set{transaction{element_type(n)}}()
    for (_, b) = n.buckets
        result = result ∪ transactions(b)
    end
    result
end

function element_type(n::node{T}) where{T}
    return T
end

function working_set(node)
    result = ds.Set{transaction{element_type(node)}}()
    # Insert the highest priority transaction from each bucket
    for (k, v) = node.buckets
        if !isempty(v)
            push!(result, first(v))
        end
    end
    result
end

function load_factor(n::node)
    Statistics.mean((load_factor(b) for (_, b) in n.buckets))
end
