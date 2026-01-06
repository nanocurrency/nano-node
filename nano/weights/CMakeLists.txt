add_library(weights bootstrap_weights.hpp bootstrap_weights.cpp
                    bootstrap_weights_beta.hpp bootstrap_weights_live.hpp)

target_link_libraries(weights nano_lib)
