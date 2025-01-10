#include <celerix/secure/pending_info.hpp>
#include <celerix/store/pending.hpp>
#include <celerix/store/typed_iterator_templ.hpp>

template class celerix::store::typed_iterator<celerix::pending_key, celerix::pending_info>;
