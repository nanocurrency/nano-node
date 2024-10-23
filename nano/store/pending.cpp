#include <nano/secure/pending_info.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/typed_iterator_templ.hpp>

template class nano::store::typed_iterator<nano::pending_key, nano::pending_info>;
