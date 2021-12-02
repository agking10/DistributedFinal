#pragma once

#include <string>
#include <iostream>
#include "messages.h"
#include <set>
#include <boost/property_tree/ptree.hpp>

using boost::property_tree::ptree;

/*
    Decorator to require that a condition is met for a function to
    be called. 
    arg1: boolean condition
    arg2: error message in case cond == false
    arg3: pointer to function
    arg4+: function arguments
*/
template <typename Func, typename ...Args>
void require(bool cond, const std::string & msg, Func f, Args&&... args)
{
    if (!cond)
        std::cerr << msg << std::endl; 
    else
        f(std::forward<Args>(args)...);
}

void strip_newline(char * buf)
{
    if (buf[strlen(buf) - 1] == '\n')
        buf[strlen(buf) - 1] = '\0';
}

template <typename T>
T identity(T x)
{
    return x;
}

template <typename T>
ptree generate_1d_ptree(T array[], int n)
{
    ptree output;
    for (int i = 0; i < n; i++)
    {
        ptree entry;
        entry.put("", array[i]);
        output.push_back(std::make_pair("", entry));
    }
    return output;
}

template <typename T>
ptree generate_2d_ptree(T * array, int rows, int cols)
{
    ptree output;
    for (int i = 0; i < rows; i++)
    {
        output.push_back(std::make_pair("", generate_1d_ptree(array[i], cols)));
    }
    return output;
}

template <typename T>
void read_1d_ptree_array(T * array, int n, const ptree& pt)
{
    int i = 0;
    for (const auto& child : pt)
    {
        array[i] = child.second.get_value<T>();
        ++i;
    }
}

template <typename T>
void read_2d_ptree_array(T * array, int rows, int cols, const ptree& pt)
{
    int i = 0;
    for (const auto& child : pt)
    {
        read_1d_ptree_array(array[i], cols, child.second);
        ++i;
    }
}

template <typename iterable, typename func>
ptree generate_iterable_ptree(iterable data, func f = identity)
{
    ptree output;
    for (const auto& i : data)
    {
        ptree elem;
        elem.push_back(std::make_pair("", f(i)));
        output.push_back(std::make_pair("", elem));
    }
    return output;
}

template <typename T, typename func>
void rehydrate_set_from_ptree(std::set<T>& set, func f, const ptree& pt)
{
    for (const auto& child : pt)
    {
        set.insert(f(child.second));
    }
}