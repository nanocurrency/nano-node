#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/receivable_iterator_impl.hpp>

template class nano::receivable_iterator<nano::ledger_set_any>;
template class nano::receivable_iterator<nano::ledger_set_confirmed>;
