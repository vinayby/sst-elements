// Copyright 2009-2013 Sandia Corporation. Under the terms // of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

/*
 * Classes representing information about an allocation for Mesh
 * Machines
 */

#include "AllocInfo.h"
#include "Job.h"
#include "MeshMachine.h"
#include "MeshAllocInfo.h"

using namespace SST::Scheduler;

MeshAllocInfo::MeshAllocInfo(Job* j) : AllocInfo(j) 
{
    processors = new std::vector<MeshLocation*>();
    for (int x = 0; x < j -> getProcsNeeded(); x++) {
        nodeIndices[x] = -1;
    }
    for (int x = 0; x < j -> getProcsNeeded(); x++) {
        processors -> push_back(NULL);
    }
}

MeshAllocInfo::~MeshAllocInfo()
{
    /*
     *note that the MeshLocations in MeshAllocInfo are assumed to be unique!
     *in other words, they were created solely for the purpose of storing a
     *location of a processor in this MAI.  All current allocators except MBS
     *use machine->getprocessors() to get these processors; this function 
     *  creates new MeshLocations so it works
     */
    for (int x = 0; x < (int)processors -> size(); x++) {
        delete processors -> at (x);
    }
    processors -> clear();
}

std::string MeshAllocInfo::getProcList(Machine* m)
{
    std::string ret="";
    MeshMachine* mesh = (MeshMachine*) m;
    for (std::vector<MeshLocation*>::iterator ml = processors -> begin(); ml != processors->end(); ++ml) {
        ret += (*ml) -> x + mesh -> getXDim() * (*ml) -> y + mesh -> getXDim() * mesh -> getYDim()*(*ml) -> z + ",";
    }
    return ret;	
}


