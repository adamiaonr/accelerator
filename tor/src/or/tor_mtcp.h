/* Copyright (c) 2015 clockWatchers	(https://bitbucket.org/clockWatchers) */

/**
 * \file tor_mtcp.h
 * \brief 'Stuff' related to mTCP.
 **/

#ifndef TOR_MTCP_H
#define TOR_MTCP_H

#include "../../../lib/mtcp/mtcp/src/include/mtcp_api.h"
#include "../../../lib/mtcp/mtcp/src/include/mtcp_epoll.h"

// XXX: mTCP changes: not sure about what these are for...
#define MAX_FLOW_NUM  (10000)
#define MAX_EVENTS (MAX_FLOW_NUM * 3)

struct thread_context
{
	mctx_t mctx;
	int ep;
};

#endif
