
# Flow control state transitions

# Add a transaction to the network's world set
function push!(n::network, transaction)
    push!(n.world, transaction)
    n.stats.inserted += 1
end

# Copy a transaction from the world set to node
function copy_global!(n::network, node)
    if !isempty(n.world)
        insert!(node, rand(n.world))
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
    @assert transaction ∈ n.world
    delete!(n.world, transaction)
    for node in n.nodes
        push!(n.confirmed, transaction)
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

const mutate_ops = [push_rand!, copy_global_rand!, copy_peer_rand!, delete_confirmed!]
const mutate_weights = [ 10, 10, 10, 10 ]
const mutate_weights_insert_light = [ 8, 10, 10, 10 ]
const mutate_weights_insert_10x = [ 10_000, 1000, 1000, 1000 ]
const mutate_weights_insert_100x = [ 100_000, 1000, 1000, 1000 ]
const mutate_weights_no_insert = [ 0, 10, 10, 10 ]

function mutate(n::network; weights = mutate_weights)
    StatsBase.sample(mutate_ops, StatsBase.Weights(weights))(n)
    n.stats.mutations += 1
end

# Runs no_insert_ops until the network is empty of transactions
function drain(n::network)
    count = 0
    # Run all ops except generating new transactions and the network should empty eventually
    while !isempty(n.world)
        StatsBase.sample(mutate_ops, StatsBase.Weights(mutate_weights_no_insert))(n)
        count += 1
    end
    count
end
