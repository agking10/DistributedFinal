#pragma once

#include <string>
#include <iostream>
#include "messages.h"
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

ptree generate_ptree(const MessageIdentifier& id)
{
    ptree output;
    output.put("origin", id.origin);
    output.put("index", id.index);
    return output;
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

