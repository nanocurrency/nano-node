#include <nano/secure/ledger_view_confirmed.hpp>
#include <nano/secure/ledger_view_unconfirmed.hpp>
#include <nano/secure/receivable_iterator_impl.hpp>

namespace nano
{
class ledger_view_confirmed;
class ledger_view_unconfirmed;
}

template class nano::receivable_iterator<nano::ledger_view_confirmed>;
template class nano::receivable_iterator<nano::ledger_view_unconfirmed>;
