struct bucket{T}
    transactions::ds.SortedSet{transaction{T}}
    max
end

function first(b::bucket)::transaction
    ds.first(b.transactions)
end

function element_type(b::bucket{T}) where{T}
    T
end

function isempty(b::bucket)
    isempty(b.transactions)
end

function length(b::bucket)
    length(b.transactions)
end

function bucket(; type = transaction_type_default, bucket_max = bucket_max_default)
    bucket(ds.SortedSet{transaction{type}}(), bucket_max)
end

function in(transaction, b::bucket)
    transaction ∈ b.transactions
end

function transactions(b::bucket)
    result = Set{transaction{element_type(b)}}(b.transactions)
end

function insert!(b::bucket, transaction)
    insert!(b.transactions, transaction)
    if length(b.transactions) > b.max
        delete!(b.transactions, last(b.transactions))
    end
end