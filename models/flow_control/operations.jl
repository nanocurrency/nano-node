
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

function normalize_for_weight(val)
    balance = val ≠ 0 ? rand(0:val) : 0
    (balance, val - balance)
end

function delete!(n::network, transaction)
    @assert transaction ∈ n.transactions
    delete!(n.transactions, transaction)
    for node in n.nodes
        delete!(node, transaction)
    end
end



function push_rand!(n::network)
    t = element_type(n)
    randval = () -> rand(typemin(t):typemax(t))
    (balance, amount) = normalize_for_weight(randval())
    tx = transaction(randval(), balance, amount, randval(), randval(), type = t)
    push!(n, tx)
end

function copy_global_rand!(n::network)
    if !isempty(n.nodes)
        copy_global!(n, rand(n.nodes))
    end
end

function copy_peer_rand!(n::network)
    if !isempty(n.nodes)
        copy_peer!(n, rand(n.nodes))
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

const mutate_ops = ['i' => push_rand!, 'g' => copy_global_rand!, 'p' => copy_peer_rand!, 'd' => delete_confirmed!]

function mutate(n::network)
    rand(mutate_ops).second(n)
    n.stats.mutations += 1
end

# Doesn't contain any operations that add new data
const no_insert_ops = [(all_ops[x] for x = ['g', 'p', 'd'])]
# Runs no_insert_ops until the network is empty of transactions
function drain(n::network)
    count = 0
    # Run all ops except generating new transactions and the network should empty eventually
    while !isempty(n.transactions)
        rand(no_insert_ops).second(n)
        count += 1
    end
    count
end