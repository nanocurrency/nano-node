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
include("operations.jl")
include("tests.jl")

function normalize_for_weight(val)
    balance = val ≠ 0 ? rand(0:(val - 1)) : 0
    (balance, val - balance)
end

include("plots.jl")

end #module

#flow_control.test()
flow_control.plots()

