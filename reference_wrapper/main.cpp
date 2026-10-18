// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <functional>
#include <iostream>
#include <list>
#include <numeric>
#include <random>
#include <vector>

static void println(auto const rem, std::ranges::range auto const& v)
{
    for (std::cout << rem; auto const& e : v)
        std::cout << e << ' ';
    std::cout << '\n';
}

int main()
{
    std::list<int> l(10);
    std::iota(l.begin(), l.end(), -4);

    // can't use shuffle on a list (requires random access), but can use it on a vector
    std::vector<std::reference_wrapper<int>> v(l.begin(), l.end());

    std::ranges::shuffle(v, std::mt19937{std::random_device{}()});

    println("Contents of the list: ", l);
    println("Contents of the list, as seen through a shuffled vector: ", v);

    std::cout << "Doubling the values in the initial list...\n";
    std::ranges::for_each(l, [](int& i) { i *= 2; });

    println("Contents of the list, as seen through a shuffled vector: ", v);
}
