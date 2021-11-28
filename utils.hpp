#pragma once

#include <string>
#include <iostream>

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
