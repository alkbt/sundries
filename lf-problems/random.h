#pragma once

#include <random>

inline int get_random(int from, int to)
{
    static std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> uni(from, to);

    return uni(rng);
}