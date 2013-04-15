// Copyright 2009-2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sst_config.h>
#include <sst/core/serialization/element.h>
#include <sst/core/simulation.h>
#include <sst/core/element.h>
#include <sst/core/interfaces/memEvent.h>
#include <sst/core/interfaces/stringEvent.h>

#include "memController.h"

#if defined(HAVE_LIBDRAMSIM)
// Our local copy of 'JEDEC_DATA_BUS_BITS', which has the comment // In bytes
static unsigned JEDEC_DATA_BUS_BITS_local= 64;
#endif

#define DPRINTF( fmt, args...) __DBG( DBG_MEMORY, Memory, "%s: " fmt, getName().c_str(), ## args )

#define NO_STRING_DEFINED "N/A"

using namespace SST;
using namespace SST::MemHierarchy;
using namespace SST::Interfaces;

MemController::MemController(ComponentId_t id, Params_t &params) : Component(id)
{
	unsigned int ramSize = (unsigned int)params.find_integer("mem_size", 0);
	if ( ramSize == 0 )
		_abort(MemController, "Must specify RAM size (mem_size) in MB\n");
	memSize = ramSize * (1024*1024);
	rangeStart = (Addr)params.find_integer("rangeStart", 0);
	rangeEnd = rangeStart + memSize;

	std::string memoryFile = params.find_string("memory_file", NO_STRING_DEFINED);

	std::string clock_freq = params.find_string("clock", "");

	registerClock(clock_freq, new Clock::Handler<MemController>(this,
				&MemController::clock));
	registerTimeBase("1 ns", true);

	use_dramsim = (bool)params.find_integer("use_dramsim", 0);
	if ( use_dramsim ) {
#if !defined(HAVE_LIBDRAMSIM)
		_abort(MemController, "This version of SST not compiled with DRAMSim.\n");
#else

		std::string deviceIniFilename = params.find_string("device_ini",
				NO_STRING_DEFINED);
		if ( deviceIniFilename == NO_STRING_DEFINED )
			_abort(MemController, "XML must define a 'device_ini' file parameter\n");
		std::string systemIniFilename = params.find_string("system_ini",
				NO_STRING_DEFINED);
		if ( systemIniFilename == NO_STRING_DEFINED )
			_abort(MemController, "XML must define a 'system_ini' file parameter\n");


		memSystem = DRAMSim::getMemorySystemInstance(
				deviceIniFilename, systemIniFilename, "", "", ramSize);

		DRAMSim::Callback<MemController, void, unsigned int, uint64_t, uint64_t>
			*readDataCB, *writeDataCB;

		readDataCB = new DRAMSim::Callback<MemController, void, unsigned int, uint64_t, uint64_t>(
				this, &MemController::dramSimReadDone);
		writeDataCB = new DRAMSim::Callback<MemController, void, unsigned int, uint64_t, uint64_t>(
				this, &MemController::dramSimWriteDone);

		memSystem->RegisterCallbacks(readDataCB, writeDataCB, NULL);
#endif
	} else {
		std::string access_time = params.find_string("access_time", "1000 ns");
		self_link = configureSelfLink("Self", access_time,
				new Event::Handler<MemController>(this, &MemController::handleSelfEvent));
	}


	bus_requested = false;


	int mmap_flags = MAP_PRIVATE;
	if ( memoryFile != NO_STRING_DEFINED ) {
		backing_fd = open(memoryFile.c_str(), O_RDWR);
		if ( backing_fd < 0 ) {
			_abort(MemController, "Unable to open backing file!\n");
		}
	} else {
		backing_fd = -1;
		mmap_flags |= MAP_ANON;
	}

	memBuffer = (uint8_t*)mmap(NULL, memSize, PROT_READ|PROT_WRITE, mmap_flags, backing_fd, 0);
	if ( !memBuffer ) {
		_abort(MemController, "Unable to MMAP backing store for Memory\n");
	}

	snoop_link = configureLink( "snoop_link", "50 ps",
			new Event::Handler<MemController>(this, &MemController::handleEvent));
	assert(snoop_link);

}


void MemController::init(unsigned int phase)
{
	if ( !phase ) {
		snoop_link->sendInitData(new StringEvent("SST::Interfaces::MemEvent"));
	}

	SST::Event *ev = NULL;
	while ( (ev = snoop_link->recvInitData()) != NULL ) {
		MemEvent *me = dynamic_cast<MemEvent*>(ev);
		if ( me ) {
			/* Push data to memory */
			if ( me->getCmd() == WriteReq ) {
				//printf("Memory received Init Command: of size 0x%x at addr 0x%lx\n", me->getSize(), me->getAddr() );
				for ( size_t i = 0 ; i < me->getSize() ; i++ ) {
					memBuffer[me->getAddr() + i - rangeStart] = me->getPayload()[i];
				}
			} else {
				printf("Memory received unexpected Init Command: %d\n", me->getCmd() );
			}
		}
		delete ev;
	}

}

int MemController::Setup(void)
{
	return 0;
}


int MemController::Finish(void)
{
	munmap(memBuffer, memSize);
	if ( backing_fd != -1 ) {
		close(backing_fd);
	}
#if defined(HAVE_LIBDRAMSIM)
	if ( use_dramsim )
		memSystem->printStats(true);
#endif


#if 0
    /* TODO:  Toggle this based off of a parameter */
    printf("--------------------------------------------------------\n");
    printf("MemController: %s\n", getName().c_str());
    printf("Outstanding Requests:  %zu\n", outstandingReadReqs.size());
    for ( std::map<Addr, DRAMReq*>::iterator i = outstandingReadReqs.begin() ; i != outstandingReadReqs.end() ; ++i ) {
        DRAMReq *req = i->second;
        printf("\t0x%08lx\t%s (%lu, %lu)\t%zu bytes:  %zu/%zu\n",
                i->first, CommandString[req->reqEvent->getCmd()],
                req->reqEvent->getID().first, req->reqEvent->getID().second,
                req->size, req->amt_in_process, req->amt_processed);
    }
    printf("Requests Queue:  %zu\n", requestQueue.size());
    printf("--------------------------------------------------------\n");
#endif

	return 0;
}


void MemController::handleEvent(SST::Event *event)
{
	MemEvent *ev = static_cast<MemEvent*>(event);
    bool to_me = ( ev->getDst() == getName() || ev->getDst() == BROADCAST_TARGET );
    switch ( ev->getCmd() ) {
    case RequestData:
    case ReadReq:
        if ( to_me ) addRequest(ev);
        break;
    case ReadResp:
        if ( ev->getSrc() != getName() ) // don't cancel from what we sent.
            cancelEvent(ev);
        break;
    case WriteReq:
    case SupplyData:
        if ( ev->queryFlag(MemEvent::F_WRITEBACK) )
            addRequest(ev);
        else
            if ( ev->getSrc() != getName() ) // don't cancel from what we sent.
                cancelEvent(ev);
        break;
    case BusClearToSend:
        if ( to_me ) sendBusPacket();
        break;
    default:
        /* Ignore */
        break;
    }
    delete event;
}


void MemController::handleSelfEvent(SST::Event *event)
{
	MemEvent *ev = static_cast<MemEvent*>(event);
	assert(ev);
	if ( !isCanceled(ev) )
		sendResponse(ev);
}


void MemController::addRequest(MemEvent *ev)
{
	DRAMReq *req = new DRAMReq(ev);
	DPRINTF("New Memory Request for 0x%lx (%s)\n", req->addr, req->isWrite ? "WRITE" : "READ");

	requestQueue.push_back(req);
    if ( req->isWrite ) {
    } else {
        std::map<Addr, DRAMReq*>::iterator i = outstandingReadReqs.find(ev->getAddr());
        if ( i == outstandingReadReqs.end() ) {
            outstandingReadReqs.insert(std::make_pair(ev->getAddr(), req));
        } else {
            i->second->req_count++;
            /* If this has been "over-canceled", correct to allow this one to go on */
            if ( i->second->req_count <= 0 ) i->second->req_count = 1;
        }
    }
}



bool MemController::clock(Cycle_t cycle)
{
#if defined(HAVE_LIBDRAMSIM)
	if ( use_dramsim )
		memSystem->update();
#endif

	if ( use_dramsim ) {
#if defined(HAVE_LIBDRAMSIM)
		// Send any new requests
		while ( ! requestQueue.empty() ) {
			DRAMReq *req = requestQueue.front();
			if ( req->canceled ) {
				requestQueue.pop_front();
				if ( req->amt_in_process == 0 ) {
					// Haven't started.  get rid of it completely
					outstandingReadReqs.erase(req->addr);
					delete req;
				}
			} else {
				uint64_t addr = req->addr + req->amt_in_process;

				addr &= ~(JEDEC_DATA_BUS_BITS_local -1); // Round down to bus boundary

                /* Don't bother if this block is already in progress. */
                if ( dramReadReqs.find(addr) == dramReadReqs.end() ) {

                    bool ok = memSystem->willAcceptTransaction(addr);
                    if ( !ok ) break;
                    ok = memSystem->addTransaction(req->isWrite, addr);
                    if ( !ok ) break;  // This *SHOULD* always be ok
                    DPRINTF("Issued transaction for address 0x%lx\n", addr);
                } else {
                    DPRINTF("Added to existing transaction for address 0x%lx\n", addr);
                }

                req->amt_in_process += JEDEC_DATA_BUS_BITS_local;

                dramReadReqs[addr].push_back(req);

                if ( req->amt_in_process >= req->size ) {
                    // Sent all requests off
                    DPRINTF("Completed issue of request\n");
                    requestQueue.pop_front();
                }
			}
		}
#endif
	} else {
		while ( ! requestQueue.empty() ) {
			DRAMReq *req = requestQueue.front();
			/* Simple timing */
			if ( req->req_count > 0 ) {
				MemEvent *resp = performRequest(req);
				if ( resp->getCmd() != NULLCMD ) {
					self_link->Send(resp);
				} else {
					delete resp;
				}
			}
			requestQueue.pop_front();
		}
	}

	return false;
}



MemEvent* MemController::performRequest(DRAMReq *req)
{
	MemEvent *resp = req->reqEvent->makeResponse(this);
	if ( ((req->addr - rangeStart) + req->size) > memSize ) {
		_abort(MemController, "Request for address 0x%lx, and size 0x%lx is larger than the physical memory of size 0x%lx bytes\n",
				req->addr, req->size, memSize);
	}
	if ( req->isWrite ) {
		for ( size_t i = 0 ; i < req->size ; i++ ) {
			memBuffer[req->addr + i - rangeStart] = req->reqEvent->getPayload()[i];
		}
        DPRINTF("Writing Memory: %zu bytes beginning at 0x%lx [0x%02x%02x%02x%02x%02x%02x%02x%02x...\n",
                req->size, req->addr,
                memBuffer[req->addr - rangeStart + 0], memBuffer[req->addr - rangeStart + 1],
                memBuffer[req->addr - rangeStart + 2], memBuffer[req->addr - rangeStart + 3],
                memBuffer[req->addr - rangeStart + 4], memBuffer[req->addr - rangeStart + 5],
                memBuffer[req->addr - rangeStart + 6], memBuffer[req->addr - rangeStart + 7]);
	} else {
		for ( size_t i = 0 ; i < req->size ; i++ ) {
			resp->getPayload()[i] = memBuffer[req->addr + i - rangeStart];
		}
        DPRINTF("Reading Memory: %zu bytes beginning at 0x%lx [0x%02x%02x%02x%02x%02x%02x%02x%02x...\n",
                req->size, req->addr,
                memBuffer[req->addr - rangeStart + 0], memBuffer[req->addr - rangeStart + 1],
                memBuffer[req->addr - rangeStart + 2], memBuffer[req->addr - rangeStart + 3],
                memBuffer[req->addr - rangeStart + 4], memBuffer[req->addr - rangeStart + 5],
                memBuffer[req->addr - rangeStart + 6], memBuffer[req->addr - rangeStart + 7]);
	}
	return resp;
}


void MemController::sendBusPacket(void)
{
	for (;;) {
		if ( busReqs.size() == 0 ) {
			snoop_link->Send(new MemEvent(this, NULL, CancelBusRequest));
			bus_requested = false;
			break;
		} else {
			MemEvent *ev = busReqs.front();
			busReqs.pop_front();
			if ( !isCanceled(ev) ) {
				DPRINTF("Sending (%lu, %d) in response to (%lu, %d) 0x%lx\n",
						ev->getID().first, ev->getID().second,
						ev->getResponseToID().first, ev->getResponseToID().second,
						ev->getAddr());
				snoop_link->Send(0, ev);
				bus_requested = false;
				if ( busReqs.size() > 0 ) {
					// Re-request bus
					sendResponse(NULL);
				}
                if ( ev->getCmd() == SupplyData ) {
                    delete outstandingReadReqs[ev->getAddr()];
                    outstandingReadReqs.erase(ev->getAddr());
                }
				break;
			}
            if ( ev->getCmd() == SupplyData ) {
                delete outstandingReadReqs[ev->getAddr()];
                outstandingReadReqs.erase(ev->getAddr());
            }
		}
	}
}


void MemController::sendResponse(MemEvent *ev)
{
	if ( ev != NULL ) {
		busReqs.push_back(ev);
	}
	if (!bus_requested) {
		snoop_link->Send(new MemEvent(this, NULL, RequestBus));
		bus_requested = true;
	}
}


bool MemController::isCanceled(Addr addr)
{
	std::map<Addr, DRAMReq*>::iterator i = outstandingReadReqs.find(addr);
	if ( i == outstandingReadReqs.end() ) return false;
	return (i->second->req_count <= 0);
}

bool MemController::isCanceled(MemEvent *ev)
{
	return isCanceled(ev->getAddr());
}


void MemController::cancelEvent(MemEvent* ev)
{
	std::map<Addr, DRAMReq*>::iterator i = outstandingReadReqs.find(ev->getAddr());
	DPRINTF("Looking to cancel for (0x%lx)\n", ev->getAddr());
	if ( i != outstandingReadReqs.end() ) {
        if ( i->second->size <= ev->getSize() ) {
            outstandingReadReqs[ev->getAddr()]->req_count--;
            DPRINTF("Canceling request.  %d remaining requests.\n", outstandingReadReqs[ev->getAddr()]);
        } else {
            DPRINTF("Not Canceling. Size mismatch.\n");
        }
	} else {
        DPRINTF("No matching read requests found.\n");
    }
}




#if defined(HAVE_LIBDRAMSIM)

void MemController::dramSimReadDone(unsigned int id, uint64_t addr, uint64_t clockcycle)
{
    std::vector<DRAMReq *> &reqs = dramReadReqs[addr];
    DPRINTF("Memory Request for 0x%lx Finished [%zu reqs]\n", addr, reqs.size());
    for ( std::vector<DRAMReq*>::iterator i = reqs.begin(); i != reqs.end(); ++i ) {
        DRAMReq *req = *i;
        req->amt_processed += JEDEC_DATA_BUS_BITS_local;
        if ( req->amt_processed >= req->size ) {
            // This req is done
            if ( !req->canceled ) {
                DPRINTF("Memory Request for 0x%lx (%s) Finished\n", req->addr, req->isWrite ? "WRITE" : "READ");
                MemEvent *resp = performRequest(req);
                sendResponse(resp);
            } else {
                outstandingReadReqs.erase(req->addr);
                delete req;
            }
        }
    }
	dramReadReqs.erase(addr);
}


void MemController::dramSimWriteDone(unsigned int id, uint64_t addr, uint64_t clockcycle)
{
    std::deque<DRAMReq *> &reqs = dramWriteReqs[addr];
    DPRINTF("Memory Request for 0x%lx Finished [%zu reqs]\n", addr, reqs.size());
    DRAMReq *req = reqs.front();
    reqs.pop_front();
    if ( reqs.size() == 0 )
        dramWriteReqs.erase(addr);

    req->amt_processed += JEDEC_DATA_BUS_BITS_local;
    if ( req->amt_processed >= req->size ) {
        // This req is done
        if ( !req->canceled ) {
            DPRINTF("Memory Request for 0x%lx (%s) Finished\n", req->addr, req->isWrite ? "WRITE" : "READ");
            MemEvent *resp = performRequest(req);
            sendResponse(resp);
        } else {
            delete req;
        }
    }
}



#endif

