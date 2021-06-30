/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#pragma once

#include <dpsim/Scheduler.h>
#include <cps/Signal/DecouplingLine.h>
#include <cps/SystemTopology.h>

#include <vector>
#include <iostream>
#include <fstream>

namespace DPsim {
	struct decouplingLineValues_t {
		int subsystem;
		int lenValues;
		CPS::Signal::ringbufferValues_t* values;
	};

	class MPILevelScheduler : public Scheduler {
	public:
		MPILevelScheduler(CPS::SystemTopology& sys, Int threads = -1, String outMeasurementFile = String());
		void createSchedule(const CPS::Task::List& tasks, const Edges& inEdges, const Edges& outEdges);
		void step(Real time, Int timeStepCount);
		void stop();

	private:
		CPS::SystemTopology& mSys;
		int mRank;
		int mNumRanks;
		std::vector<int> mSubsystems;
		String mOutMeasurementFile;
		std::vector<std::vector<CPS::Task::List>> mLevels;
		long getSizeOfDecouplingLineValues(std::vector<decouplingLineValues_t> values);
		std::vector<decouplingLineValues_t> getDecouplingLineValues();
		void getData(char* data, std::vector<decouplingLineValues_t> values, long size);
		void setDecouplingLineValues(char* data);
	};
};
