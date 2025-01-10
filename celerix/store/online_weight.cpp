#include <celerix/store/online_weight.hpp>
#include <celerix/store/reverse_iterator_templ.hpp>
#include <celerix/store/typed_iterator_templ.hpp>

template class celerix::store::typed_iterator<uint64_t, celerix::amount>;
template class celerix::store::reverse_iterator<celerix::store::typed_iterator<uint64_t, celerix::amount>>;

auto celerix::store::online_weight::rbegin (store::transaction const & tx) const -> reverse_iterator
{
	auto iter = end (tx);
	--iter;
	return reverse_iterator{ std::move (iter) };
}

auto celerix::store::online_weight::rend (transaction const & tx) const -> reverse_iterator
{
	return reverse_iterator{ end (tx) };
}
