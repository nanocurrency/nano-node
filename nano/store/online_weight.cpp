#include <nano/store/online_weight.hpp>
#include <nano/store/reverse_iterator_templ.hpp>
#include <nano/store/typed_iterator_templ.hpp>

template class nano::store::typed_iterator<uint64_t, nano::amount>;
template class nano::store::reverse_iterator<nano::store::typed_iterator<uint64_t, nano::amount>>;

auto nano::store::online_weight::rbegin (store::transaction const & tx) const -> reverse_iterator
{
	auto iter = end (tx);
	--iter;
	return reverse_iterator{ std::move (iter) };
}

auto nano::store::online_weight::rend (transaction const & tx) const -> reverse_iterator
{
	return reverse_iterator{ end (tx) };
}
