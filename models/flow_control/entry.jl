module flow_control
import Pkg; Pkg.add("DataStructures"); Pkg.add("Dates"); Pkg.add("StatsBase")
import DataStructures as ds
import Dates
import Base.copy, Base.delete!, Base.first, Base.getindex, Base.in, Base.isempty, Base.isless, Base.length, Base.lt, Base.insert!, Base.print, Base.push!, Base.Threads
import Test
import Plots, Plots.plot, Plots.plot!
using Plots.PlotMeasures
import StatsBase
import Statistics

const transaction_type_default = UInt128 # High precision minimizes value-collisions
#const transaction_type_default = UInt8 # Low precision maximizes value-collisions
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

#Threads.nthreads()
#print(Dates.now(), '\n')

@time flow_control.test()
#@time flow_control.plots()

