// Copyright Louis Dionne 2017
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef ANY_ITERATOR_HPP
#define ANY_ITERATOR_HPP

#include <te.hpp>

#include <cassert>
#include <iterator>
#include <type_traits>
#include <utility>
using namespace te::literals;


// This is the definition of an Iterator concept using a "generic" language.
// Instead of defining specific methods that must be defined, it defines its
// interface in terms of compile-time strings, assuming these may be fulfilled
// in possibly many different ways.
template <typename Reference>
struct Iterator : decltype(te::requires(
  te::CopyConstructible{},
  te::CopyAssignable{},
  te::Destructible{},
  te::Swappable{},
  "increment"_s = te::function<void (te::T&)>,
  "dereference"_s = te::function<Reference (te::T&)>
)) { };

template <typename Reference>
struct InputIterator : decltype(te::requires(
  Iterator<Reference>{},
  te::EqualityComparable{}
)) { };

template <typename Reference>
struct ForwardIterator : decltype(te::requires(
  InputIterator<Reference>{},
  te::DefaultConstructible{}
)) { };

template <typename Reference>
struct BidirectionalIterator : decltype(te::requires(
  ForwardIterator<Reference>{},
  "decrement"_s = te::function<void (te::T&)>
)) { };

template <typename Reference, typename Difference>
struct RandomAccessIterator : decltype(te::requires(
  BidirectionalIterator<Reference>{},
  "advance"_s = te::function<void (te::T&, Difference)>,
  "distance"_s = te::function<Difference (te::T const&, te::T const&)>
)) { };


template <typename Category, typename T>
using when = std::enable_if_t<
  std::is_base_of<Category, typename std::iterator_traits<T>::iterator_category>{}
>;

template <typename Iterator>
using reference_t = typename std::iterator_traits<Iterator>::reference;

template <typename Iterator>
using difference_t = typename std::iterator_traits<Iterator>::difference_type;

// This is some kind of concept map; it maps the "generic" iterator interface
// (method names as compile-time strings) to actual implementations for a
// specific iterator type.
template <typename T>
auto const te::default_concept_map<Iterator<reference_t<T>>, T>
  = te::make_default_concept_map<Iterator<reference_t<T>>, T>(
  "increment"_s = [](T& self) { ++self; },
  "dereference"_s = [](T& self) -> reference_t<T> { return *self; }
);

template <typename T>
auto const te::default_concept_map<BidirectionalIterator<reference_t<T>>, T,
  when<std::bidirectional_iterator_tag, T>
> = te::make_default_concept_map<BidirectionalIterator<reference_t<T>>, T>(
  "decrement"_s = [](T& self) -> void { --self; }
);

template <typename T>
auto const te::default_concept_map<RandomAccessIterator<reference_t<T>, difference_t<T>>, T,
  when<std::random_access_iterator_tag, T>
> = te::make_default_concept_map<RandomAccessIterator<reference_t<T>, difference_t<T>>, T>(
  "advance"_s = [](T& self, difference_t<T> diff) -> void {
    std::advance(self, diff);
  },

  "distance"_s = [](T const& first, T const& last) -> difference_t<T> {
    return std::distance(first, last);
  }
);

namespace detail {
  template <typename Category, typename Reference, typename Difference>
  struct iterator_category_to_concept;

  template <typename Reference, typename Difference>
  struct iterator_category_to_concept<std::input_iterator_tag, Reference, Difference> {
    using type = InputIterator<Reference>;
  };

  template <typename Reference, typename Difference>
  struct iterator_category_to_concept<std::forward_iterator_tag, Reference, Difference> {
    using type = ForwardIterator<Reference>;
  };

  template <typename Reference, typename Difference>
  struct iterator_category_to_concept<std::bidirectional_iterator_tag, Reference, Difference> {
    using type = BidirectionalIterator<Reference>;
  };

  template <typename Reference, typename Difference>
  struct iterator_category_to_concept<std::random_access_iterator_tag, Reference, Difference> {
    using type = RandomAccessIterator<Reference, Difference>;
  };
} // end namespace detail

// This defines a type-erased wrapper satisfying a specific concept (Iterator)
// and providing the given interface (methods, etc..). The choice of how to
// store the type-erased object and how to implement the VTable should be done
// here, and should be disjoint from the actual concept definition and concept
// map above.
template <
  typename Value,
  typename Category,
  typename Reference = Value&,
  typename Difference = std::ptrdiff_t
>
struct any_iterator {
  using iterator_category = Category;
  using value_type = Value;
  using reference = Reference;
  using pointer = value_type*;
  using difference_type = Difference;

private:
  using Concept = typename detail::iterator_category_to_concept<
    iterator_category, reference, difference_type
  >::type;

  using Storage = te::local_storage<8>;
  using VTable = te::remote_vtable<te::local_vtable<Concept>>;
  te::poly<Concept, Storage, VTable> poly_;

public:
  template <typename It>
  explicit any_iterator(It it)
    : poly_{std::move(it)}
  {
    using IteratorTraits = std::iterator_traits<It>;
    using Source_value_type = typename IteratorTraits::value_type;
    using Source_reference = typename IteratorTraits::reference;
    using Source_category = typename IteratorTraits::iterator_category;
    using Source_difference_type = typename IteratorTraits::difference_type;

    static_assert(std::is_convertible<Source_value_type, value_type>{},
      "The 'value_type' of the erased iterator must be convertible to that of "
      "the 'any_iterator'");

    static_assert(std::is_convertible<Source_reference, reference>{},
      "The 'reference' of the erased iterator must be convertible to that of "
      "the 'any_iterator'");

    static_assert(std::is_base_of<iterator_category, Source_category>{},
      "The 'iterator_category' of the erased iterator must be at least as "
      "powerful as that of the 'any_iterator'");

    static_assert(std::is_same<difference_type, Source_difference_type>{},
      "The 'difference_type' of the erased iterator must match that of the "
      "'any_iterator'");
  }

  any_iterator(any_iterator const& other) = default;
  any_iterator(any_iterator&& other) = default;
  any_iterator& operator=(any_iterator const& other) = default;
  any_iterator& operator=(any_iterator&& other) = default;
  ~any_iterator() = default;

  void swap(any_iterator& other) {
    using std::swap;
    swap(this->poly_, other.poly_);
  }

  friend void swap(any_iterator& a, any_iterator& b) { a.swap(b); }

  any_iterator& operator++() {
    poly_.virtual_("increment"_s)(poly_.get());
    return *this;
  }

  template <bool True = true, typename = std::enable_if_t<True &&
    std::is_base_of<std::bidirectional_iterator_tag, iterator_category>{}
  >> any_iterator& operator--() {
    poly_.virtual_("decrement"_s)(poly_.get());
    return *this;
  }

  reference operator*() {
    return poly_.virtual_("dereference"_s)(poly_.get());
  }

  friend bool operator==(any_iterator const& a, any_iterator const& b) {
    assert(a.poly_.virtual_("equal"_s) == b.poly_.virtual_("equal"_s));
    return a.poly_.virtual_("equal"_s)(a.poly_.get(), b.poly_.get());
  }

  friend bool operator!=(any_iterator const& a, any_iterator const& b) {
    return !(a == b);
  }
};

#endif // ANY_ITERATOR_HPP
