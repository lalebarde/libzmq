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

#include <stddef.h>
#include "platform.hpp"
#include "proxy.hpp"
#include "likely.hpp"
#include "msg.hpp"

#if defined ZMQ_FORCE_SELECT
#define ZMQ_POLL_BASED_ON_SELECT
#elif defined ZMQ_FORCE_POLL
#define ZMQ_POLL_BASED_ON_POLL
#elif defined ZMQ_HAVE_LINUX || defined ZMQ_HAVE_FREEBSD ||\
    defined ZMQ_HAVE_OPENBSD || defined ZMQ_HAVE_SOLARIS ||\
    defined ZMQ_HAVE_OSX || defined ZMQ_HAVE_QNXNTO ||\
    defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_AIX ||\
    defined ZMQ_HAVE_NETBSD
#define ZMQ_POLL_BASED_ON_POLL
#elif defined ZMQ_HAVE_WINDOWS || defined ZMQ_HAVE_OPENVMS ||\
     defined ZMQ_HAVE_CYGWIN
#define ZMQ_POLL_BASED_ON_SELECT
#endif

//  On AIX platform, poll.h has to be included first to get consistent
//  definition of pollfd structure (AIX uses 'reqevents' and 'retnevents'
//  instead of 'events' and 'revents' and defines macros to map from POSIX-y
//  names to AIX-specific names).
#if defined ZMQ_POLL_BASED_ON_POLL
#include <poll.h>
#endif

// These headers end up pulling in zmq.h somewhere in their include
// dependency chain
#include "socket_base.hpp"
#include "err.hpp"

// zmq.h must be included *after* poll.h for AIX to build properly
#include "../include/zmq.h"

int
zmq::proxy_t::capture_msg(zmq::msg_t& msg_, int more_)
{
    //  Copy message to capture socket if any
    if (capture) {
        zmq::msg_t ctrl;
        int rc = ctrl.init ();
        if (unlikely (rc < 0))
            return -1;
        rc = ctrl.copy (msg_);
        if (unlikely (rc < 0))
            return -1;
        rc = capture->send (&ctrl, more_? ZMQ_SNDMORE: 0);
        if (unlikely (rc < 0))
            return -1;
    }
    return 0;
}

int
zmq::proxy_t::forward(
        class zmq::socket_base_t *from_,
        class zmq::socket_base_t *to_,
        zmq::hook_f do_hook_,
        void *data_)
{
    zmq::msg_t msg;
    int rc = msg.init ();
    if (unlikely (rc < 0))
        return -1;
    int more;
    for (size_t n = 1;; n++) {
        int rc = from_->recv (&msg, 0);
        if (unlikely (rc < 0))
            return -1;

        rc = from_->getsockopt (ZMQ_RCVMORE, &more, &moresz);
        if (unlikely (rc < 0))
            return -1;

        //  Copy message to capture socket if any
        rc = capture_msg(msg, more);
        if (unlikely (rc < 0))
            return -1;

        // Hook
        if (do_hook_) {
            rc = (*do_hook_)(this, from_, to_, capture, &msg, more ? n : 0, data_); // first message: n == 1, mth message: n == m, last message: n == 0
            if (unlikely (rc < 0))
                return -1;
        }

        rc = to_->send (&msg, more? ZMQ_SNDMORE: 0);
        if (unlikely (rc < 0))
            return -1;
        if (more == 0)
            break;
    }
    return 0;
}

zmq::proxy_t::proxy_t (
        class socket_base_t **open_endpoint_,
        class socket_base_t **frontend_,
        class socket_base_t **backend_,
        class socket_base_t *capture_,
        class socket_base_t *control_,
        zmq::proxy_hook_t **hook_,
        long time_out_) :
            open_endpoint(open_endpoint_), frontend(frontend_), backend(backend_),
            capture(capture_), control(control_), hook(hook_), time_out(time_out_)
{
    moresz = sizeof (int);

    // some cases which are mis-uses and we don't want to deal with them
    if ((frontend_ && !backend_) || (!frontend_ && backend_)) {
        errno = EFAULT;
        return;
    }

    // counts the number of sockets in open_endpoint_
    qt_oep = 0;
    if (open_endpoint_)
        for (;; qt_oep++)
            if (!open_endpoint_[qt_oep])
                break;

    // counts the number of pair of sockets in frontend/backend, and the total number of sockets
    qt_sockets = qt_oep;
    qt_pairs_fb = 0;
    if (frontend_ && backend_)
        for (;; qt_pairs_fb++) {
            if (!frontend_[qt_pairs_fb] && !backend_[qt_pairs_fb])
                break;
            if (frontend_[qt_pairs_fb])
                qt_sockets++;
            if (backend_[qt_pairs_fb])
                qt_sockets++;
        }
    qt_poll_items = (control_ ? qt_sockets + 1 : qt_sockets);

    // strick criteria for zmq_proxy, zmq_proxy_steerable, zmq_proxy_hook: one single proxy with frontend and backend defined
    if (time_out == -1)
        if (!frontend_ || !backend_ || !frontend_[0] || !backend_[0]) {
            errno = EFAULT;
            return;
        }

    // allocate memory for arrays
//    items = new zmq_pollitem_t [qt_poll_items]; // (zmq_pollitem_t*) malloc(qt_poll_items * sizeof(zmq_pollitem_t));
//    linked_to = new size_t [qt_poll_items]; // (size_t*) malloc(qt_poll_items * sizeof(size_t));
//    hook_func = new hook_f [qt_poll_items]; // (hook_f*) malloc(qt_poll_items * sizeof(hook_f));
//    hook_data = new void* [qt_poll_items]; // (void**) malloc(qt_poll_items * sizeof(void*));

    // fill the zmq_pollitem_t array, identifying with linked_to if a socket is alone (open) or to which one it is proxied
    zmq_pollitem_t null_item = { NULL, 0, ZMQ_POLLIN, 0 };
    size_t k = 0;
    if (open_endpoint_)
        while (open_endpoint_[k]) {
            memcpy(&items[k], &null_item, sizeof(null_item));
            items[k].socket = open_endpoint_[k];
            linked_to[k] = k; // this socket is alone (open)
            hook_data[k] = NULL; // No hook will be executed on an end-point socket since we don't recv the messages
            hook_func[k] = NULL; // No hook will be executed on an end-point socket since we don't recv the messages
            k++;
        }
    if (frontend && backend)
        for (size_t i = 0; i < qt_pairs_fb; i++, k++) {
            memcpy(&items[k], &null_item, sizeof(null_item));
            hook_data[k] = hook && hook[i] ? hook[i]->data : NULL;
            if (!frontend[i]) {
                items[k].socket = backend[i];
                linked_to[k] = k; // this socket is alone (open)
                hook_func[k] = NULL; // No hook will be executed on an "open" socket since we don't recv the messages
            }
            else if (!backend[i]) {
                items[k].socket = frontend[i];
                linked_to[k] = k; // this socket is alone (open)
                hook_func[k] = NULL; // No hook will be executed on an "open" socket since we don't recv the messages
            }
            else {
                items[k].socket = frontend[i];
                linked_to[k] = k+1; // this socket is proxied to the next one
                hook_func[k] = hook && hook[i] ? hook[i]->front2back_hook : NULL;
                k++;
                hook_data[k] = hook && hook[i] ? hook[i]->data : NULL;
                memcpy(&items[k], &null_item, sizeof(null_item));
                items[k].socket = backend[i];
                linked_to[k] = k-1; // this socket is proxied to the previous one
                hook_func[k] = hook && hook[i] ? hook[i]->back2front_hook : NULL;
            }
    }
    if (!k) { // we require at least one socket
        errno = EFAULT;
        return;
    }
    assert (k == qt_sockets);
    memcpy(&items[k], &null_item, sizeof(null_item));
    items[k].socket =     control_;

    state = active;
}

zmq::proxy_t::~proxy_t ()
{
    // deallocate memory of arrays
//    delete[] hook_data; // free (hook_data);
//    delete[] hook_func; // free (hook_func);
//    delete[] linked_to; // free (linked_to);
//    delete[] items; // free (items);
}

int
zmq::proxy_t::poll()
{
    //  The algorithm below assumes ratio of requests and replies processed
    //  under full load to be 1:1.

    zmq::msg_t msg;
    int rc = msg.init ();
    if (unlikely (rc < 0))
        return -1;
    int more;

    while (state != terminated) {
        //  Wait while there are either requests or replies to process.
        rc = zmq_poll (&items [0], qt_poll_items, time_out);
        if (unlikely (rc < 0))
            return -1;
        if (rc == 0) // no message. Obviously, we are in the case where: time_out_ != -1
            return 0;

        //  Process a control command if any
        if (control && items [qt_poll_items - 1].revents & ZMQ_POLLIN) {
            rc = control->recv (&msg, 0);
            if (unlikely (rc < 0))
                return -1;

            rc = control->getsockopt (ZMQ_RCVMORE, &more, &moresz);
            if (unlikely (rc < 0) || more)
                return -1;

            //  Copy message to capture socket if any
            rc = capture_msg(msg, more);
            if (unlikely (rc < 0))
                return -1;

            if (msg.size () == 5 && memcmp (msg.data (), "PAUSE", 5) == 0)
                state = paused;
            else
            if (msg.size () == 6 && memcmp (msg.data (), "RESUME", 6) == 0)
                state = active;
            else
            if (msg.size () == 9 && memcmp (msg.data (), "TERMINATE", 9) == 0)
                state = terminated;
            else {
                //  This is an API error, we should assert
                puts ("E: invalid command sent to proxy");
                zmq_assert (false);
            }
        }

        // process each pair of sockets
        for (size_t i = 0; i < qt_sockets; i++) {
            if (state == active
            &&  items [i].revents & ZMQ_POLLIN) {
                if (i != linked_to[i]) { // this socket is proxied to the linked_to[i] one
                    rc = forward((zmq::socket_base_t *) items[i].socket,
                                 (zmq::socket_base_t *) items[linked_to[i]].socket,
                                 hook_func[i],
                                 hook_data[i]);
                    if (unlikely (rc < 0))
                        return -1;
                }
                else // this socket is alone (open)
                    return time_out == -1 ? 1 : i + 1; // 1 is for backward compatibility, sockets are counted starting at 1
            }
        }

        // proxy opening
        if (time_out != -1)
            break;
    }
    return 0;
}

int
zmq::proxy_t::set_socket_events_mask (size_t socket_index, int state)
{
    if (items && socket_index <= qt_sockets && socket_index > 0) { // socket_index starts at 1
        items[--socket_index].events = state; // trim to 0 before apply
        return 0;
    }
    else
        return -1;
}

