module flow_control
import Pkg; Pkg.add("DataStructures");
import DataStructures as ds
import Base.first, Base.delete!, Base.in, Base.isempty, Base.isless, Base.length, Base.lt, Base.insert!, Base.print, Base.push!
import Test
import Plots

const transaction_type_default = UInt64
const bucket_max_default = 16
const bucket_count_default = 32
const node_count_default = 4

include("transaction.jl")
include("bucket.jl")
include("node.jl")
include("network.jl")
include("transitions.jl")
include("tests.jl")

function normalize_for_weight(val)
    balance = val ≠ 0 ? rand(0:(val - 1)) : 0
    (balance, val - balance)
end

function op_push!(n::network)
    t = transaction_type(n)
    randval = () -> rand(typemin(t):typemax(t))
    (balance, amount) = normalize_for_weight(randval())
    tx = transaction{t}(randval(), balance, amount, randval(), randval())
    push!(n, tx)
end

function op_copy_global!(n::network)
    if !isempty(n.nodes)
        copy_global!(n, rand(n.nodes))
    end
end

function op_copy_peer!(n::network)
    if !isempty(n.nodes)
        copy_peer!(n, rand(n.nodes))
    end
end

all_ops = ['i' => op_push!, 'g' => op_copy_global!, 'p' => op_copy_peer!, 'd' => delete_confirmed!]
no_insert_ops = ['g' => op_copy_global!, 'p' => op_copy_peer!, 'd' => delete_confirmed!]

function mutate(n::network)
    rand(all_ops).second(n)
end

function drain(n::network)
    # Run all ops except generating new transactions and the network should empty eventually
    while !isempty(n.transactions)
        rand(no_insert_ops).second(n)
    end
end

include("plots.jl")

end #module

#flow_control.test()

flow_control.plots()

