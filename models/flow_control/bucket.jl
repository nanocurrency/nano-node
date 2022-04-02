

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