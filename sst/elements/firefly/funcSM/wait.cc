// Copyright 2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include "sst/core/serialization.h"

#include "funcSM/wait.h"
#include "dataMovement.h"

using namespace SST::Firefly;

WaitFuncSM::WaitFuncSM( int verboseLevel, Output::output_location_t loc,
                 Info* info, ProtocolAPI* dm ) :
    FunctionSMInterface(verboseLevel,loc,info),
    m_dm( static_cast<DataMovement*>( dm ) )
{
    m_dbg.setPrefix("@t:WaitFuncSM::@p():@l ");
}

void WaitFuncSM::handleStartEvent( SST::Event *e, Retval& retval ) 
{
    if ( m_setPrefix ) {
        char buffer[100];
        snprintf(buffer,100,"@t:%d:%d:WaitFuncSM::@p():@l ",
                    m_info->nodeId(), m_info->worldRank());
        m_dbg.setPrefix(buffer);

        m_setPrefix = false;
    }

    m_dbg.verbose(CALL_INFO,1,0,"\n");

    m_event = static_cast< WaitStartEvent* >(e);

    if ( m_event->req->src != Hermes::AnySrc ) {
        retval.setExit(0);
        delete m_event;
        m_event = NULL;
        return;
    }

    m_dm->enter();
}

void WaitFuncSM::handleEnterEvent( SST::Event *e, Retval& retval )
{
    m_dbg.verbose(CALL_INFO,1,0,"\n");
    if ( m_event->req->src != Hermes::AnySrc ) {
        m_dbg.verbose(CALL_INFO,1,0,"src=%d tag=%#x\n",
                    m_event->req->src, m_event->req->tag);
        retval.setExit(0);
        // remove entry from m_dm 
        delete m_event;
        m_event = NULL;
        return;
    } else {
        m_dm->sleep();
    }
}
