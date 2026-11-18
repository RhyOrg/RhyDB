#pragma once

// From https://en.cppreference.com/w/cpp/utility/variant/visit
// helper type for the visitor #4
template <class... Ts>
// NOLINTNEXTLINE(misc-multiple-inheritance) inheriting from the lambdas is the point of the idiom
struct Overloaded : Ts... {
   using Ts::operator()...;
};

// explicit deduction guide (not needed as of C++20)
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
