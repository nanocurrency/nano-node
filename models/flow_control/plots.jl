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
    types = [UInt8, UInt16, UInt32, UInt64, UInt128]
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
    x = []
    y = []
    # 6348.392737 seconds (40.97 G allocations: 2.350 TiB, 6.67% gc time, 0.10% compilation time)
    large_set = 2:12
    # 6.604670 seconds (12.63 M allocations: 654.941 MiB, 2.22% gc time, 94.88% compilation time)
    small_set = 2:6

    set = small_set
    for i = set
        n = network(node_count = 2^i, type = UInt128)
        count = 0
        while n.stats.deleted == 0
            mutate(n)
            count += 1
        end
        print(length(n.nodes), ' ', count, '\n')
        push!(x, length(n.nodes))
        # Count operations are performed across all nodes in the network
        # Divide count by number of nodes so they look similar no matter the sequence fed in
        push!(y, count ÷ length(n.nodes))
    end
    Plots.plot(x, y, title = "Operations per confirmation by node count", xlabel = "Nodes", ylabel = "Operations/node")
end

function plot_node_count_iterations_3d()
    # 6348.392737 seconds (40.97 G allocations: 2.350 TiB, 6.67% gc time, 0.10% compilation time)
    large_set = 2:12
    # 6.604670 seconds (12.63 M allocations: 654.941 MiB, 2.22% gc time, 94.88% compilation time)
    small_set = (2^i for i in 2:6)
    all_vals = 2:64

    set = collect(small_set)
    x = []
    y = []
    z = []
    for multiplier = 10:10:40
        weights = [multiplier * 100, 100, 100, 100]
        print(weights, '\n')
        for i = set
            n = network(node_count = i, type = UInt128)
            count = 0
            while n.stats.deleted == 0
                mutate(n, weights = weights)
                count += 1
            end
            print(length(n.nodes), ' ', count, '\n')
            # Count operations are performed across all nodes in the network
            # Divide count by number of nodes so they look similar no matter the sequence fed in
            push!(x, i)
            push!(y, count ÷ length(n.nodes))
            push!(z, multiplier)
        end
        #push!(labels, "x" * string(multiplier))
    end
    Plots.surface(x, y, z, camera=(30,60), seriescolor = :broc, title = "Operations per confirmation by node count", xlabel = "Nodes", ylabel = "Operations/node", zlabel= "Multiplier")
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
            mutate(n, weights = mutate_weights)
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

function log_or_one(val)
    val == 0 ? 1 : log(2, val)
end

function plot_confirmed_abandoned_load()
    bucket_max = 2
    confirmed = []
    abandoned = []
    o = []
    ys = [confirmed, abandoned, o]
    exponential_sampling = (2^x for x in 16:20)
    count = 1_000
    full_range = 1:count

    x = collect(full_range)
    n = network(bucket_max = bucket_max)
    for i = x
        mutate(n, weights = mutate_weights_insert_100x)
        push!(confirmed, log_or_one(n.stats.deleted))
        push!(abandoned, log_or_one(length(abandoned_set(n))))
        push!(o, log_or_one(overflows(n)))
    end
    print(histogram(n, bucket_max), '\n')
    while !isempty(n.world)
        mutate(n, weights = mutate_weights_no_insert)
        push!(confirmed, log_or_one(n.stats.deleted))
        push!(abandoned, log_or_one(length(abandoned_set(n))))
        push!(o, log_or_one(overflows(n)))
        count += 1
        push!(x, count)
    end
    plt = Plots.plot(x, ys, label = ["Confirmed" "Abandoned" "Overflows"], title = "Confirmations after operations", xlabel = "Operations", ylabel = "Transaction count(log2)", right_margin=15mm)
    #Plots.plot(Plots.twinx(plt), collect(x), load, lims = [0.0, 1,0], legend = false, ylabel = "Load Factor", color="grey")
end

function plot_histogram_iteration_series()
    bucket_max = bucket_max_default
    set = 1:1_000
    x = []
    y = []
    z = []
    n = network(bucket_max = bucket_max)
    for i = set
        for j = 1:10
            mutate(n, weights = mutate_weights_no_confirm)
        end
        h = histogram(n, bucket_max)
        for s = 1:length(h)
            # Number of items
            push!(x, i)
            # Frequency of that count
            push!(y, s)
            push!(z, log_or_one(h[s]))
        end
    end
    print("Plotting...\n")
    Plots.surface(x, y, z, seriescolor = :broc, camera=(30,60), title = "Bucket status after iterations", xlabel = "Iteration", ylabel = "Fill amount", zlabel="Frequency")
end

function plot_histogram_series()
    bucket_max = bucket_max_default
    set = 1:1_000
    x = []
    y = []
    n = network(bucket_max = bucket_max)
    for i = set
        mutate(n, weights = mutate_weights_no_confirm)
    end
    h = histogram(n, bucket_max)
    for s = 1:length(h)
        # Number of items
        push!(x, s)
        # Frequency of that count
        push!(y, h[s])
    end
    Plots.bar(x, y, title = "Bucket status after iterations", xlabel = "Bucket fill count", ylabel = "Frequency")
end

 function generate(op)
    print("Generating: " * string(op) * "...\n")
    display(op())
    print(" Done\n")
 end

function plots()
    #generate(plot_type)
    #generate(plot_node_count_iterations)
    #generate(plot_node_count_iterations_3d)
    #generate(plot_bucket_max)
    #generate(plot_bucket_count)
    #generate(plot_saturation)
    #generate(plot_confirmed_abandoned_load)
    generate(plot_histogram_iteration_series)
    #generate(plot_histogram_series)
end
