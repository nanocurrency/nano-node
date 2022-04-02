function stress(node_count, bucket_count, bucket_max; type = transaction_type_default)
    series = []
    n = network(node_count = node_count, bucket_count = bucket_count, bucket_max = bucket_max, type = type)
    for i = iterations
        mutate(n)
        push!(series, n.stats.deleted)
    end
    (n, series)
end

function plot_type()
    types = [UInt8, UInt16, UInt32, UInt64]
    ys = []
    x = 1:10_000
    labels = []
    for type = types
        n = network(type = type)
        series = []
        for _ in x
            mutate(n)
            push!(series, n.stats.deleted)
        end
        push!(ys, series)
        push!(labels, string(type))
    end
    labels = permutedims(labels)
    Plots.plot(x, ys, label = labels, xlabel = "Operations", ylabel = "Confirmed")
    # Asymptote should drive a type for transaction_type_default.
end

function plot_node_count_iterations()
    y = []
    #x = collect(2^val for val = 2:5)
    #x = collect(2^val for val = 2:8)
    x = 4:64
    for i = x
        n = network(node_count = i)
        count = 0
        #print(i, ' ')
        while n.stats.deleted == 0
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmation by node count", xlabel = "Nodes", ylabel = "Operations")
end

function plot_bucket_max()
    y = []
    #x = map((val) -> 2^val, 2:6)
    x = 1:16
    iteration_count = 1_000
    for bucket_max = x
        #print(bucket_max, ' ')
        n = network(bucket_max = bucket_max)
        count = 0
        while n.stats.deleted < iteration_count
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmations(" * string(iteration_count) * ") by bucket max", xlabel = "Bucket max", ylabel = "Operations")
    # Asymptote should drive a value for bucket_max_default. Smaller gives better simulation throughput.
end

function plot_bucket_count()
    y = []
    #x = map((val) -> 2^val, 2:8)
    x = 1:64
    iteration_count = 5_000
    for bucket_count = x
        #print(bucket_count, ' ')
        n = network(bucket_count = bucket_count)
        count = 0
        while n.stats.deleted < iteration_count
            mutate(n)
            count += 1
        end
        push!(y, count)
    end
    Plots.plot(x, y, title = "Operations per confirmations(" * string(iteration_count) * ") by bucket count", xlabel = "Bucket max", ylabel = "Operations")
    # Asymptote should drive a value for bucket_count_default. Smaller gives better simulation throughput.
end

function plot_saturation()
    y = []
    x = 8:18
    #x = collect(1:100_000)
    n = network()
    for i = x
        count = 2^i
        #print(count, ' ')
        for j = 1:count
            mutate(n)
        end
        push!(y, log(2, n.stats.deleted))
    end
    Plots.plot(x, y, title = "Confirmations after operations", xlabel = "log2(Operations)", ylabel = "log2(Confirmations)")
 end

 function generate(op)
    print("Generating: " * string(op) * "...")
    display(op())
    print(" Done\n")
 end

function plots()
    test()
 
    generate(plot_type)
    #generate(plot_node_count_iterations)
    #generate(plot_bucket_max)
    #generate(plot_bucket_count)
    #generate(plot_saturation)
end