struct node{T}
    buckets::ds.SortedDict{T, bucket}
end

function node_buckets(type, count)
    ds.SortedSet{type}((i * (typemax(type) ÷ count) for i in 0:count - 1))
end

# Divide the keyspace of transaction_type in to count buckets
function node(; type = transaction_type_default, bucket_count = bucket_count_default, bucket_max = bucket_max_default)
    node(ds.SortedDict{type, bucket}((range_start => bucket(type = type, bucket_max = bucket_max) for range_start in node_buckets(type, bucket_count))))
end

function insert!(n::node, tx)
    insert!(n[tx], tx)
end

function delete!(n::node, tx)
    index = ds.searchsortedlast(n.buckets, weight(tx))
    delete!(n.buckets[index], tx)
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
    for (_, v) = node.buckets
        if !isempty(v)
            push!(result, first(v))
        end
    end
    result
end

# An occupancy histogram of the buckets in this node
# Index is the number of items in a bucket and the value is the number of buckets with that occupancy
function histogram(n::node, max)
    result = zeros(max + 1)
    for (_, b) = n.buckets
        result[length(b) + 1] += 1
    end
    result
end

function overflows(n::node)
    sum((b.stats.overflows) for (_, b) in n.buckets)
end

# The bucket the given transaction belongs to
function getindex(n::node, tx)
    n.buckets[ds.searchsortedlast(n.buckets, weight(tx))]
end
