

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