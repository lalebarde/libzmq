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

#include "testutil.hpp"
#include "../include/zmq_utils.h"

// Asynchronous client-to-server (DEALER to ROUTER) - pure libzmq
//
// While this example runs in a single process, that is to make
// it easier to start and stop the example. Each task may have its own
// context and conceptually acts as a separate process. To have this
// behaviour, it is necessary to replace the inproc transport of the
// control socket by a tcp transport.

// This is our client task
// It connects to the server, and then sends a request once per second
// It collects responses as they arrive, and it prints them out. We will
// run several client tasks in parallel, each with a different random ID.

#define CONTENT_SIZE 13
#define CONTENT_SIZE_MAX 32
#define ID_SIZE 10
#define ID_SIZE_MAX 32
#define is_verbose 1

int
main (void)
{
    setup_test_environment ();

    void *ctx = zmq_ctx_new ();
    assert (ctx);

    void *client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    char content [CONTENT_SIZE_MAX];
    // Set random identity to make tracing easier
    char identity [ID_SIZE];
    sprintf (identity, "%04X-%04X", rand() % 0xFFFF, rand() % 0xFFFF);
    int rc = zmq_setsockopt (client, ZMQ_IDENTITY, identity, ID_SIZE); // includes '\0' as an helper for printf
    assert (rc == 0);
    rc = zmq_connect (client, "tcp://127.0.0.1:9999");
    assert (rc == 0);

    // Frontend socket talks to clients over TCP
    void *frontend = zmq_socket (ctx, ZMQ_ROUTER);
    assert (frontend);
    rc = zmq_bind (frontend, "tcp://127.0.0.1:9999");
    assert (rc == 0);

    // Intermediate 1
    void *intermediate1 = zmq_socket (ctx, ZMQ_DEALER);
    assert (intermediate1);
    rc = zmq_connect (intermediate1, "inproc://intermediate");
    assert (rc == 0);

    // Intermediate 2
    void *intermediate2 = zmq_socket (ctx, ZMQ_DEALER);
    assert (intermediate2);
    rc = zmq_bind (intermediate2, "inproc://intermediate");
    assert (rc == 0);

    // Backend socket talks to workers over inproc
    void *backend = zmq_socket (ctx, ZMQ_DEALER);
    assert (backend);
    rc = zmq_bind (backend, "inproc://backend");
    assert (rc == 0);

    void *worker = zmq_socket (ctx, ZMQ_DEALER);
    assert (worker);
    rc = zmq_connect (worker, "inproc://backend");
    assert (rc == 0);

    void* frontends[] = {client, frontend,      intermediate2, NULL,   NULL}; // client is n° 1
    void* backends[] =  {NULL,   intermediate1, backend,       worker, NULL}; // worker is n° 8

    for (int request_nbr = 0; request_nbr < 3; request_nbr++) {
        // Tick once per 200 ms, pulling in arriving messages
        int centitick;
        for (centitick = 0; centitick < 20; centitick++) {
            // Connect backend to frontend via a proxies
            int trigged_socket = zmq_proxy_open (frontends, backends, NULL, NULL, NULL, 10);
            if (trigged_socket == 1) {
                int rcvmore;
                size_t sz = sizeof (rcvmore);
                rc = zmq_recv (client, content, CONTENT_SIZE_MAX, 0);
                assert (rc == CONTENT_SIZE);
                if (is_verbose) printf("client receive - identity = %s    content = %s\n", identity, content);
                //  Check that message is still the same
                assert (memcmp (content, "request #", 9) == 0);
                rc = zmq_getsockopt (client, ZMQ_RCVMORE, &rcvmore, &sz);
                assert (rc == 0);
                assert (!rcvmore);
            }
            if (trigged_socket == 8) {
                // The DEALER socket gives us the reply envelope and message
                rc = zmq_recv (worker, identity, ID_SIZE_MAX, 0); // ZMQ_DONTWAIT
                if (rc == ID_SIZE) {
                    rc = zmq_recv (worker, content, CONTENT_SIZE_MAX, 0);
                    assert (rc == CONTENT_SIZE);
                    if (is_verbose)
                        printf ("server receive - identity = %s    content = %s\n", identity, content);

                    // Send 0..4 replies back
                    int reply, replies = 1; //rand() % 5;
                    for (reply = 0; reply < replies; reply++) {
                        // Sleep for some fraction of a second
                        msleep (rand () % 10 + 1);
                        //  Send message from server to client
                        rc = zmq_send (worker, identity, ID_SIZE, ZMQ_SNDMORE);
                        assert (rc == ID_SIZE);
                        rc = zmq_send (worker, content, CONTENT_SIZE, 0);
                        assert (rc == CONTENT_SIZE);
                    }
                }
            }
        }
        sprintf(content, "request #%03d", ++request_nbr); // CONTENT_SIZE
        rc = zmq_send (client, content, CONTENT_SIZE, 0);
        assert (rc == CONTENT_SIZE);
    }


    rc = zmq_close (client);
    assert (rc == 0);
    rc = zmq_close (frontend);
    assert (rc == 0);
    rc = zmq_close (intermediate1);
    assert (rc == 0);
    rc = zmq_close (intermediate2);
    assert (rc == 0);
    rc = zmq_close (backend);
    assert (rc == 0);
    rc = zmq_close (worker);
    assert (rc == 0);
    rc = zmq_ctx_term (ctx);
    assert (rc == 0);
    return 0;
}
