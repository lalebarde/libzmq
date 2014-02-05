/*
    Copyright (c) 2007-2014 Contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __ZMQ_PROXY_HPP_INCLUDED__
#define __ZMQ_PROXY_HPP_INCLUDED__

#define ZMQ_PROXY_CHAIN_MAX_LENGTH 10
#include "../include/zmq.h" // TODO: to be suppressed if possible

namespace zmq
{
//    class socket_base_t;
    class msg_t;
//    class zmq_pollitem_t;

    typedef int (*hook_f)(void *frontend, void *backend, void *capture, void* msg_, size_t n_, void *data_);

    struct proxy_hook_t
    {
        void *data;
        hook_f front2back_hook;
        hook_f back2front_hook;
    };

    typedef enum { //  Proxy can be in these three states
        active,
        paused,
        terminated
    } proxy_state_t;

    class proxy_t {
    public:
        proxy_t (
                class socket_base_t **open_endpoint_,
                class socket_base_t **frontend_,
                class socket_base_t **backend_,
                class socket_base_t *capture_ = NULL,
                class socket_base_t *control_ = NULL,
                proxy_hook_t **hook_ = NULL,
                long time_out_ = -1
            );
        ~proxy_t();
        int poll();
        int set_socket_events_mask (size_t socket_index, int state);
    private:
        int capture_msg(zmq::msg_t& msg_, int more_);
        int forward(
                class zmq::socket_base_t *from_,
                class zmq::socket_base_t *to_,
                zmq::hook_f do_hook_,
                void *data_);

    private:
        class socket_base_t **open_endpoint;
        class socket_base_t **frontend; // not used ?
        class socket_base_t **backend; // not used ?
        class socket_base_t *capture;
        class socket_base_t *control;
        proxy_hook_t **hook;
        long time_out;
    private:
        size_t moresz;
        size_t qt_oep; // number of open_endpoint_ sockets - ends with NULL
        size_t qt_pairs_fb; // number of pair of sockets: both arrays frontend_ & backend_ ends with NULL
        size_t qt_sockets; // total number of sockets inside open_endpoint_, frontend_, and backend_
//        zmq_pollitem_t* items;
//        size_t* linked_to;
//        zmq::hook_f* hook_func;
//        void** hook_data;
        zmq_pollitem_t items[ZMQ_PROXY_CHAIN_MAX_LENGTH];
        size_t linked_to[ZMQ_PROXY_CHAIN_MAX_LENGTH];
        zmq::hook_f hook_func[ZMQ_PROXY_CHAIN_MAX_LENGTH];
        void* hook_data[ZMQ_PROXY_CHAIN_MAX_LENGTH];
        int qt_poll_items; // total number of sockets which shall be polled, including control_ if not NULL
        proxy_state_t state;
    };
}

#endif
