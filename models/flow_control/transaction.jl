

# Transaction properties used to bucket and sort transactions
struct transaction{T<:Integer}
    # Unique transaction id
    tx::UInt64
    tally::T
    balance::T
    amount::T
    lru::T
    difficulty::T
end

function element_type(t::transaction{T}) where{T}
    T
end

function transaction(tally, balance, amount, lru, difficulty; tx = rand(UInt64), type = transaction_type_default)
    transaction{type}(tx, tally, balance, amount, lru, difficulty)
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

# Return a random value of the same type as val, but not equal to val
function rand_ne(val)
    result = val
    while true 
        result = rand(typemin(typeof(val)):typemax(typeof(val)))
        result == val || break
    end
    result
end

# Simulates malleability of the transaction sort between different machines
# Example: different nodes can compute different tallies when they observe different vote sets.
# This behavior is emulated on values by randomizing the value that is copied.
function copy(t::transaction)
    type = element_type(t)
    lru = rand_ne(t.lru) # We may have a different local timestamp for last account confirmation.
    tally = rand_ne(t.tally) # We may not have the same vote sets
    transaction(tally, t.balance, t.amount, lru, t.difficulty, tx = t.tx, type = type)
    # Merge other higher difficulty
end

function isequal_invariant(lhs, rhs)
    lhs.balance == rhs.balance && lhs.amount == rhs.amount && lhs.difficulty == rhs.difficulty
end

function isequal(lhs, rhs)
    result = lhs.tx == rhs.tx
    @assert !result || isequal_invariant(lhs, rhs)
end

function weight(t::transaction)
    max(t.amount, t.balance)
end