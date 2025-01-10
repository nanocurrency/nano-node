#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/secure/receivable_iterator_impl.hpp>

template class celerix::receivable_iterator<celerix::ledger_set_any>;
template class celerix::receivable_iterator<celerix::ledger_set_confirmed>;
