////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2012 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#if !defined(HPX_09DFB7AE_8265_4667_AA02_65BF8C0B1DFD)
#define HPX_09DFB7AE_8265_4667_AA02_65BF8C0B1DFD

#include <string>

#include <hpx/config.hpp>

namespace hpx { namespace util
{
    HPX_EXPORT const char* hpx_prefix(const char * prefix = 0);

    // return the installation path of the specified module
    HPX_EXPORT std::string find_prefix(std::string library = "hpx");

    // return a list of paths delimited by HPX_INI_PATH_DELIMITER
    HPX_EXPORT std::string find_prefixes(std::string const& suffix,
        std::string const& library = "hpx");

    // return the full path of the current executable
    HPX_EXPORT std::string get_executable_filename(char const* argv0 = 0);
    HPX_EXPORT std::string get_executable_prefix(char const* argv0 = 0);
}}

#endif // HPX_09DFB7AE_8265_4667_AA02_65BF8C0B1DFD

