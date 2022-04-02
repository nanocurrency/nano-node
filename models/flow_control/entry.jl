module flow_control
import Pkg;
import DataStructures as ds
import Dates
import Base.@time, Base.delete!, Base.first, Base.in, Base.isempty, Base.isless, Base.length, Base.lt, Base.insert!, Base.print, Base.push!
import Test
import Plots, Plots.plot

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
include("plots.jl")

end #module

function run()
    flow_control.test()
    #flow_control.plots()
end

print(Dates.now(), '\n')
@time run()

