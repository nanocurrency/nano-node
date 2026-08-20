add_library(
  secure
  account_info.hpp
  account_info.cpp
  account_iterator.cpp
  account_iterator.hpp
  account_iterator_impl.hpp
  common.hpp
  common.cpp
  election_ballot.hpp
  election_ballot.cpp
  endpoint_key.hpp
  endpoint_key.cpp
  fwd.hpp
  network_params.hpp
  network_params.cpp
  generate_cache_flags.hpp
  ledger.hpp
  ledger.cpp
  extended/account_block_by_height_key.hpp
  extended/account_block_by_height_key.cpp
  extended/account_delegator_by_weight_key.hpp
  extended/account_delegator_by_weight_key.cpp
  extended/account_receivable_by_amount_info.hpp
  extended/account_receivable_by_amount_info.cpp
  extended/account_receivable_by_amount_key.hpp
  extended/account_receivable_by_amount_key.cpp
  ledger_extended_index.cpp
  ledger_topo_index.cpp
  ledger_processor.hpp
  ledger_processor.cpp
  ledger_rollback.hpp
  ledger_rollback.cpp
  ledger_set_any.hpp
  ledger_set_any.cpp
  ledger_set_cemented.hpp
  ledger_set_cemented.cpp
  pending_info.hpp
  pending_info.cpp
  receivable_iterator.cpp
  receivable_iterator.hpp
  receivable_iterator_impl.hpp
  rep_weights.hpp
  rep_weights.cpp
  transaction.hpp
  voting_policy.hpp
  voting_policy.cpp)

target_link_libraries(secure nano_lib ed25519 crypto_lib Boost::system)

target_compile_definitions(secure PUBLIC -DQT_NO_KEYWORDS
                                         -DBOOST_ASIO_HAS_STD_ARRAY=1)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
