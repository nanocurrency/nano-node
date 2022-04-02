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
    result = Dict{element_type(n), UInt16}()
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