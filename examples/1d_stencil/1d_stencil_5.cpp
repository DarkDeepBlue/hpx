//  Copyright (c) 2014 Hartmut Kaiser
//  Copyright (c) 2014 Bryce Adelstein-Lelbach
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/hpx.hpp>

#include <boost/shared_array.hpp>

///////////////////////////////////////////////////////////////////////////////
struct cell_data
{
    cell_data()
      : size_(0)
    {}

    cell_data(std::size_t size)
      : data_(new double [size]), size_(size)
    {}

    cell_data(std::size_t size, double initial_value)
      : data_(new double [size]), size_(size)
    {
        double base_value = double(initial_value * size);
        for (std::size_t i = 0; i != size; ++i)
            data_[i] = base_value + double(i);
    }

    double& operator[](std::size_t idx) { return data_[idx]; }
    double operator[](std::size_t idx) const { return data_[idx]; }

    std::size_t size() const { return size_; }

private:
    // serialization support
    friend boost::serialization::access;

    template <typename Archive>
    void save(Archive& ar, const unsigned int version) const
    {
    }

    template <typename Archive>
    void load(Archive& ar, const unsigned int version)
    {
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

private:
    boost::shared_array<double> data_;
    std::size_t size_;
};

std::ostream& operator<<(std::ostream& os, cell_data const& c)
{
    os << "{";
    for (std::size_t i = 0; i != c.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        os << c[i];
    }
    os << "}";
    return os;
}

///////////////////////////////////////////////////////////////////////////////
struct cell_server : hpx::components::simple_component_base<cell_server>
{
    // construct new instances
    cell_server() {}

    cell_server(cell_data const& data)
      : data_(data)
    {}

    cell_server(std::size_t size, double initial_value)
      : data_(size, initial_value)
    {}

    // access data
    cell_data get_data() const
    {
        return data_;
    }

    HPX_DEFINE_COMPONENT_CONST_ACTION(cell_server, get_data, get_data_action);

private:
    cell_data data_;
};

// This is a client side helper class allowing to hide some of the tedious
// boilerplate.
struct cell : hpx::components::client_base<cell, cell_server>
{
    typedef hpx::components::client_base<cell, cell_server> base_type;

    cell() {}

    // create new component on locality 'where' and initialize the held data
    cell(hpx::id_type where, std::size_t size, double initial_value)
      : base_type(hpx::new_<cell_server>(where, size, initial_value))
    {}

    cell(hpx::id_type where, cell_data && data)
      : base_type(hpx::new_colocated<cell_server>(where, std::move(data)))
    {}

    // attach future representing (possibly remote) cell
    cell(hpx::future<hpx::id_type> && id)
      : base_type(std::move(id))
    {}

    cell(hpx::future<cell> && c)
      : base_type(std::move(c))
    {}

    // Invoke the (remote) member function which gives us access to the data
    hpx::future<cell_data> get_data() const
    {
        cell_server::get_data_action act;
        return hpx::async(act, get_gid());
    }
};

typedef hpx::components::simple_component<cell_server> server_type;
HPX_REGISTER_MINIMAL_COMPONENT_FACTORY(server_type, cell_server);

typedef cell_server::get_data_action get_data_action;
HPX_REGISTER_ACTION_DECLARATION(get_data_action);
HPX_REGISTER_ACTION(get_data_action);

///////////////////////////////////////////////////////////////////////////////
typedef std::vector<cell> space;            // data for one time step
typedef std::vector<space> spacetime;       // all of stored time steps

///////////////////////////////////////////////////////////////////////////////
inline std::size_t idx(std::size_t i, std::size_t size)
{
    return (boost::int64_t(i) < 0) ? (i + size) % size : i % size;
}

///////////////////////////////////////////////////////////////////////////////
// Our operator:
//   f(t+1, i) = (f(t, i-1) + f(t, i) + f(t, i+1)) / 3
inline double heat(double a, double b, double c)
{
    return (a + b + c) / 3.;
}

// The partitioned operator, it invokes the heat operator above on all elements
// of a partition.
cell_data heat_part_data(
    cell_data const& left, cell_data const& middle, cell_data const& right)
{
    std::size_t size = middle.size();
    cell_data next(size);

    next[0] = heat(left[size-1], middle[0], middle[1]);

    for (std::size_t i = 1; i != size-1; ++i)
        next[i] = heat(middle[i-1], middle[i], middle[i+1]);

    next[size-1] = heat(middle[size-2], middle[size-1], right[0]);

    return next;
}

///////////////////////////////////////////////////////////////////////////////
cell heat_part(cell const& left, cell const& middle, cell const& right)
{
    using hpx::lcos::local::dataflow;
    using hpx::util::unwrapped;

    return dataflow(unwrapped(
        [left, middle, right](cell_data const& l, cell_data const& m, cell_data const& r)
        {
            // the new cell_data will be allocated on the same locality as 'middle'
            return cell(middle.get_gid(), heat_part_data(l, m, r));
        }),
        left.get_data(), middle.get_data(), right.get_data());
}

HPX_PLAIN_ACTION(heat_part, heat_part_action);

///////////////////////////////////////////////////////////////////////////////
int hpx_main(boost::program_options::variables_map& vm)
{
    using hpx::lcos::local::dataflow;
    using hpx::util::unwrapped;

    boost::uint64_t nt = vm["nt"].as<boost::uint64_t>();   // Number of steps.
    boost::uint64_t nx = vm["nx"].as<boost::uint64_t>();   // Number of grid points.
    boost::uint64_t np = vm["np"].as<boost::uint64_t>();   // Number of partitions.

    // U[t][i] is the state of position i at time t.
    spacetime U(2);
    for (space& s: U)
        s.resize(np);

    // Initial conditions:
    //   f(0, i) = i
    for (std::size_t i = 0; i != np; ++i)
        U[0][i] = cell(hpx::find_here(), nx, double(i));

    using hpx::util::placeholders::_1;
    using hpx::util::placeholders::_2;
    using hpx::util::placeholders::_3;
    auto Op = hpx::util::bind(heat_part_action(), hpx::find_here(), _1, _2, _3);

    for (std::size_t t = 0; t != nt; ++t)
    {
        space& current = U[t % 2];
        space& next = U[(t + 1) % 2];

        for (std::size_t i = 0; i != np; ++i)
            next[i] = dataflow(Op, current[idx(i-1, np)], current[i], current[idx(i+1, np)]);
    }

    // Print the solution at time-step 'nt'.
    space const& solution = U[nt % 2];
    for (std::size_t i = 0; i != np; ++i)
        std::cout << "U[" << i << "] = " << solution[i].get_data().get() << std::endl;

    return hpx::finalize();
}

int main(int argc, char* argv[])
{
    using namespace boost::program_options;

    options_description desc_commandline;
    desc_commandline.add_options()
        ("nx", value<boost::uint64_t>()->default_value(10),
         "Local x dimension (of each partition)")
        ("nt", value<boost::uint64_t>()->default_value(45),
         "Number of time steps")
        ("np", value<boost::uint64_t>()->default_value(10),
         "Number of partitions")
    ;

    // Initialize and run HPX
    return hpx::init(desc_commandline, argc, argv);
}
