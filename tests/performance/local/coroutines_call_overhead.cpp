//  Copyright (c) 2006, Giovanni P. Deretta
//  Copyright (c) 2012 Hartmut Kaiser
//  Copyright (c) 2011-2014 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/include/util.hpp>
#include <hpx/util/coroutine/coroutine.hpp>
#include <hpx/util/activate_counters.hpp>

#include <boost/format.hpp>
#include <boost/random.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "worker_timed.hpp"

char const* benchmark_name = "Context Switching Overhead - HPX";

using namespace boost::program_options;
using namespace hpx::threads;

using hpx::util::coroutines::coroutine;
using std::cout;

///////////////////////////////////////////////////////////////////////////////
boost::uint64_t payload    = 0;
boost::uint64_t contexts   = 1000;
boost::uint64_t iterations = 100000;
boost::uint64_t seed       = 0; 
bool header = true;

///////////////////////////////////////////////////////////////////////////////
std::string format_build_date(std::string timestamp)
{
    boost::gregorian::date d = boost::gregorian::from_us_string(timestamp);

    char const* fmt = "%02i-%02i-%04i";

    return boost::str(boost::format(fmt)
                     % d.month().as_number() % d.day() % d.year());
}

///////////////////////////////////////////////////////////////////////////////
void print_results(
    double walltime
  , std::vector<std::string> const& counter_shortnames
  , boost::shared_ptr<hpx::util::activate_counters> ac 
    )
{
    std::vector<hpx::performance_counters::counter_value> counter_values;

    if (ac)
        counter_values = ac->evaluate_counters_sync();

    if (header)
    {
        cout << "# BENCHMARK: " << benchmark_name << "\n";

        cout << "# VERSION: " << HPX_GIT_COMMIT << " "
                 << format_build_date(__DATE__) << "\n"
             << "#\n";

        // Note that if we change the number of fields above, we have to
        // change the constant that we add when printing out the field # for
        // performance counters below (e.g. the last_index part).
        cout <<
                "## 0:PLOAD:Payload [micro-seconds] - Independent Variable\n"
                "## 1:CTXS:# of Contexts - Independent Variable\n"
                "## 2:ITER:# of Iterations - Independent Variable\n"
                "## 3:SEED:PRNG seed - Independent Variable\n"
                "## 4:WTIME_CS:Walltime/Context Switch [nano-seconds]\n"
                ;

        boost::uint64_t const last_index = 4;

        for (boost::uint64_t i = 0; i < counter_shortnames.size(); ++i)
        {
            cout << "## "
                 << (i + 1 + last_index) << ":"
                 << counter_shortnames[i] << ":"
                 << ac->name(i);

            if (!ac->unit_of_measure(i).empty())
                cout << " [" << ac->unit_of_measure(i) << "]";

            cout << "\n";            
        }
    }

    cout << ( boost::format("%lu %lu %lu %lu %.14g")
            % payload 
            % contexts 
            % iterations
            % seed
            % ((walltime/(2*iterations))*1e9)
            );

    if (ac)
    {   
        for (boost::uint64_t i = 0; i < counter_shortnames.size(); ++i)
            cout << ( boost::format(" %.14g")
                    % counter_values[i].get_value<double>());
    }

    cout << "\n";
}

///////////////////////////////////////////////////////////////////////////////
struct kernel
{
    thread_state_enum operator()(thread_state_ex_enum)
    {
        worker_timed(payload);

        return terminated;
    }

    bool operator!() const { return true; }
    void clear() {}
};

int hpx_main(
    variables_map& vm
    )
{
    {
        if (vm.count("no-header"))
            header = false;

        if (!seed)
            seed = boost::uint64_t(std::time(0));
   
        std::vector<coroutine_type*> coroutines;
        std::vector<boost::uint64_t> indices;

        coroutines.reserve(contexts);
        indices.reserve(iterations);

        boost::random::mt19937_64 prng(seed);
        boost::random::uniform_int_distribution<boost::uint64_t>
            dist(0, contexts - 1);

        kernel k;

        for (boost::uint64_t i = 0; i < contexts; ++i)
        {
            coroutine_type* c = new coroutine_type(k, hpx::find_here());
            coroutines.push_back(c);
        }

        for (boost::uint64_t i = 0; i < iterations; ++i)
            indices.push_back(dist(prng));

        ///////////////////////////////////////////////////////////////////////
        std::vector<std::string> counter_shortnames;
        std::vector<std::string> counters;
        if (vm.count("counter"))
        {
            std::vector<std::string> raw_counters =
                vm["counter"].as<std::vector<std::string> >();

            for (boost::uint64_t i = 0; i < raw_counters.size(); ++i)
            {
                std::vector<std::string> entry;
                boost::algorithm::split(entry, raw_counters[i],
                    boost::algorithm::is_any_of(","),
                    boost::algorithm::token_compress_on);

                HPX_ASSERT(entry.size() == 2);

                counter_shortnames.push_back(entry[0]);
                counters.push_back(entry[1]);
            }
        }

        boost::shared_ptr<hpx::util::activate_counters> ac;
        if (!counters.empty())
            ac.reset(new hpx::util::activate_counters(counters));

        ///////////////////////////////////////////////////////////////////////
        hpx::util::high_resolution_timer t;

        for (boost::uint64_t i = 0; i < iterations; ++i)
        {
            (*coroutines[indices[i]])(wait_signaled);
        }

        double elapsed = t.elapsed();

        print_results(elapsed, counter_shortnames, ac);

        for (boost::uint64_t i = 0; i < contexts; ++i)
        {
            delete coroutines[i];
        }

        coroutines.clear();
    }

    return hpx::finalize();
}

///////////////////////////////////////////////////////////////////////////////
int main(
    int argc
  , char* argv[]
    )
{
    // Configure application-specific options.
    options_description cmdline("usage: " HPX_APPLICATION_STRING " [options]");

    cmdline.add_options()
        ( "payload"
        , value<boost::uint64_t>(&payload)->default_value(5)
        , "artificial delay of each coroutine")

        ( "contexts"
        , value<boost::uint64_t>(&contexts)->default_value(100000)
        , "number of contexts use") 

        ( "iterations"
        , value<boost::uint64_t>(&iterations)->default_value(100000)
        , "number of iterations to invoke (2 * iterations context switches "
          "will occur)")

        ( "seed"
        , value<boost::uint64_t>(&seed)->default_value(0)
        , "seed for the pseudo random number generator (if 0, a seed is "
          "choosen based on the current system time)")

        ( "counter"
        , value<std::vector<std::string> >()->composing()
        , "activate and report the specified performance counter")

        ( "no-header"
        , "do not print out the header")
        ;

    // Initialize and run HPX.
    return hpx::init(cmdline, argc, argv);
}

