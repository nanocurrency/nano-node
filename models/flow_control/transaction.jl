# Transaction properties used to bucket and sort transactions
struct transaction{T<:Integer}
    # Unique transaction id
    tx::UInt64
    tally::T
    balance::T
    amount::T
    # Timestamp for the last confirmed transaction for this transaction's account
    lru::T
    difficulty::T
end

# Returns the type T for a given transaction
function element_type(_::transaction{T}) where{T}
    T
end

# Constructs a transaction with a random tx id and default transaction type
function transaction(tally, balance, amount, lru, difficulty; tx = rand(UInt64), type = transaction_type_default)
    transaction{type}(tx, tally, balance, amount, lru, difficulty)
end

function isless(lhs::transaction, rhs::transaction)
    lhs_w = flow_control.weight(lhs)
    rhs_w = flow_control.weight(rhs)
    (lhs.tally != rhs.tally) ? 
    # First sort based on highest tally
    (lhs.tally > rhs.tally) :
        (lhs.lru != rhs.lru) ?
        # Then sort by LRU account
        (lhs.lru < rhs.lru) :
            (lhs.difficulty != rhs.difficulty) ?
            # Then sort by difficulty
            (lhs.difficulty > rhs.difficulty) :
                (lhs_w != rhs_w) ? 
                # Then by highest transaction weight
                (lhs_w > rhs_w) :
                false
end

# Simulates malleability of the transaction sort between different machines
# This behavior is emulated on values by randomizing a value that is copied between nodes
# Return a random value of the same type as val, but not equal to val
function rand_ne(val)
    result = val
    while true
        result = rand(typemin(typeof(val)):typemax(typeof(val)))
        # keep looping if the random value happened to be the same
        result == val || break
    end
    result
end

function copy_malleable(tx::transaction)
    type = element_type(tx)

     # Different nodes can have different local timestamps for last account confirmation.
     # Different nodes' clocks can never be perfectly synchronized
    lru = rand_ne(tx.lru)

    # Different nodes can compute different tallies when they observe different vote sets.
    tally = rand_ne(tx.tally)

    transaction(tally, tx.balance, tx.amount, lru, tx.difficulty, tx = tx.tx, type = type)
    # Merge other higher difficulty
end

function copy_exact(tx::transaction)
    transaction(tx.tally, tx.balance, tx.amount, tx.lru, tx.difficulty, tx = tx.tx, type = element_type(t))
end

function copy(tx::transaction)::transaction
    copy_malleable(tx)
    #copy_exact(tx)
end

function isequal_invariant(lhs, rhs)
    lhs.balance == rhs.balance && lhs.amount == rhs.amount && lhs.difficulty == rhs.difficulty
end

function isequal(lhs, rhs)
    result = lhs.tx == rhs.tx

    # Transactions with equal tx values should have equal non-malleable fields.
    @assert !result || isequal_invariant(lhs, rhs)
end

function weight(t::transaction)
    max(t.amount, t.balance)
end
