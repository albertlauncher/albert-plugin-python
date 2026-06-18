// Copyright (c) 2026 Manuel Schneider

#pragma once
#include <pybind11/pybind11.h>

template<typename T>
inline std::vector<T> vectorFromPyList(const pybind11::list& list)
{
    std::vector<T> vec;
    vec.reserve(list.size());
    for (auto item : list)
        vec.push_back(item.cast<T>());
    return vec;
}

template<typename T>
inline std::vector<T> vectorFromPyObject(const pybind11::object& obj)
{
    using pybind11::isinstance, pybind11::list, std::vector;

    vector<T> vec;
    if (isinstance<list>(obj))
        vec = vectorFromPyList<T>(obj.cast<list>());
    else if (isinstance<vector<T>>(obj))  // opaque
        vec = std::move(obj.cast<vector<T>&>());
    else
        throw pybind11::type_error("Expected Python list or opaque list type");
    return vec;
}
