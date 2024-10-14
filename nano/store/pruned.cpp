#include <nano/store/pruned.hpp>
#include <nano/store/typed_iterator_templ.hpp>

template class nano::store::typed_iterator<nano::block_hash, std::nullptr_t>;
