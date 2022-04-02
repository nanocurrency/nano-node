

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

function weight(t::transaction)
    max(t.amount, t.balance)
end