/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <dpsim/MPILevelScheduler.h>
#include <dpsim/MNASolver.h>
#include <dpsim/MNASolverSysRecomp.h>
#include <mpi.h>
#include <cps/Signal/DecouplingLine.h>

#include <iostream>
#include <fstream>
#include <iomanip>

using namespace CPS;
using namespace DPsim;

MPILevelScheduler::MPILevelScheduler(CPS::SystemTopology& sys, Int threads, String outMeasurementFile) : mSys(sys), mOutMeasurementFile(outMeasurementFile) {
	int initialized = 0;
	MPI_Initialized(&initialized);
	if (!initialized)
	   MPI_Init(NULL, NULL);

	if (threads >= 0)
		mNumRanks = threads;
	else
		MPI_Comm_size(MPI_COMM_WORLD, &mNumRanks);

	MPI_Comm_rank(MPI_COMM_WORLD, &mRank);
}

void MPILevelScheduler::createSchedule(const Task::List& tasks, const Edges& inEdges, const Edges& outEdges) {
	Task::List ordered;
	long i = 0,  level = 0, maxSubsystem = 0;//, subsystem = 0;

	std::vector<CPS::Task::List> levels;

	Scheduler::topologicalSort(tasks, inEdges, outEdges, ordered);
	Scheduler::levelSchedule(ordered, inEdges, outEdges, levels);

	if (!mOutMeasurementFile.empty())
		Scheduler::initMeasurements(tasks);

	for (level = 0; level < static_cast<long>(levels.size()); level++)
                for (i = 0; i < static_cast<long>(levels[level].size()); i++)
                        if (levels[level][i]->getSubsystem() > maxSubsystem)
                                maxSubsystem = levels[level][i]->getSubsystem();

        for (i = 0; i <= maxSubsystem; i++) {
		std::vector<CPS::Task::List> tmp;
                mLevels.push_back(tmp);
	}

        for (level = 0; level < static_cast<long>(levels.size()); level++)
                for (i = 0; i <= maxSubsystem; i++) {
			CPS::Task::List tmp;
                        mLevels[i].push_back(tmp);
		}

        for (level = 0; level < static_cast<long>(levels.size()); level++)
                for (i = 0; i < static_cast<long>(levels[level].size()); i++)
                        mLevels[levels[level][i]->getSubsystem()][level].push_back(levels[level][i]);

	for (i = 0; i <= maxSubsystem; i++) {
		if (i % mNumRanks == mRank) {
			mSubsystems.push_back(i);
		}
	}

	//std::ofstream outFile;
	//outFile.open("Tasks.csv");

	//outFile << "Name;Subsystem\n";
	//for (level = 0; level < static_cast<long>(levels.size()); level++)
	//	for (i = 0; i < static_cast<long>(levels[level].size()); i++)
	//		outFile << levels[level][i]->toString() << ";" << levels[level][i]->getSubsystem() << "\n";

	//outFile.close();

	//std::ofstream outFile2;
        //outFile2.open("Tasks2.csv");

        //outFile2 << "Subsystem;Level;Name\n";
        //for (subsystem = 0; subsystem < static_cast<long>(mLevels.size()); subsystem++)
        //        for (level = 0; level < static_cast<long>(mLevels[subsystem].size()); level++)
        //                for (i = 0; i < static_cast<long>(mLevels[subsystem][level].size()); i++)
        //                        outFile2 << subsystem << ";" << level << ";" << mLevels[subsystem][level][i]->toString() << "\n";

        //outFile2.close();
}

void MPILevelScheduler::step(Real time, Int timeStepCount) {
	int i = 0;
        long level = 0, subsystem = 0;
	
	if (!mOutMeasurementFile.empty()) {
		for (level = 0; level < static_cast<long>(mLevels[mRank].size()); level++)
				for (i = 0; i < static_cast<long>(mLevels[mRank][level].size()); i++) {
					auto start = std::chrono::steady_clock::now();
					mLevels[mRank][level][i]->execute(time, timeStepCount);
					auto end = std::chrono::steady_clock::now();
					updateMeasurement(mLevels[mRank][level][i].get(), end-start);
				}
	} else {
		for (subsystem = 0; subsystem < static_cast<long>(mSubsystems.size()); subsystem++) {
			for (level = 0; level < static_cast<long>(mLevels[mSubsystems[subsystem]].size()); level++) {
				for (i = 0; i < static_cast<long>(mLevels[mSubsystems[subsystem]][level].size()); i++)
					mLevels[mSubsystems[subsystem]][level][i]->execute(time, timeStepCount);
			}
		}

		std::cout << mRank << "start" << mNumRanks << "\n";

		for (i = 0; i < mNumRanks; i++) {
			std::cout << "Test\n";
			std::cout << mRank << "Loopstart" << i << "\n";
			std::vector<decouplingLineValues_t> values;
			long size = 0;
			if (i == mRank) {
				values = getDecouplingLineValues();
				std::cout << "1.: " << values[0].values[0].idx << "\n";
				size = getSizeOfDecouplingLineValues(values);
			}
			MPI_Barrier(MPI_COMM_WORLD);
			MPI_Bcast(&size, sizeof(long), MPI_LONG, i, MPI_COMM_WORLD);
			char* data = (char*)malloc(size);
			if (i == mRank) {
				std::cout << "2.: " << values[0].values[0].idx << "\n";
				getData(data, values, size);
				std::cout << "3.: " << values[0].values[0].idx << "\n";
			}
			MPI_Barrier(MPI_COMM_WORLD);
			MPI_Bcast(data, size, MPI_BYTE, i, MPI_COMM_WORLD);
			std::cout << mRank << "Data transferred\n";
			MPI_Barrier(MPI_COMM_WORLD);
			if (i != mRank)
				setDecouplingLineValues(data);
			free(data);
			std::cout << mRank << "Data released\n";
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "Test2\n";
		}

		std::cout << mRank << "end\n";
	}
}

long MPILevelScheduler::getSizeOfDecouplingLineValues(std::vector<decouplingLineValues_t> values) {
	long size = sizeof(int);
	int sizeRingbufferValues = sizeof(UInt) + 4 * sizeof(Complex);
	for (auto val : values)
		size += 2 * sizeof(int) + val.lenValues * sizeRingbufferValues;

	return size;
}

void MPILevelScheduler::getData(char* data, std::vector<decouplingLineValues_t> values, long size) {
        int lenValues = values.size();
	std::memcpy(data, &lenValues, sizeof(lenValues));
	data += sizeof(lenValues);
	
	for (auto val : values) {
		std::memcpy(data, &val.subsystem, sizeof(val.subsystem));
		data += sizeof(val.subsystem);
		std::memcpy(data, &val.lenValues, sizeof(val.lenValues));
		data += sizeof(val.lenValues);
		for (int k = 0; k < val.lenValues; k++) {
			std::cout << "getData idx: " << val.values[k].idx << "\n";
			std::memcpy(data, &val.values[k].idx, sizeof(val.values[k].idx));
			data += sizeof(val.values[k].idx);
			for(int i = 0; i < 4; i++) {
				std::memcpy(data, &val.values[k].values[i], sizeof(val.values[k].values[i]));
				data += sizeof(val.values[k].values[i]);
			}
		}
	}
}

std::vector<decouplingLineValues_t> MPILevelScheduler::getDecouplingLineValues() {
	std::vector<decouplingLineValues_t> values;
	long subsystem = 0;

	for (subsystem = 0; subsystem < static_cast<long>(mSubsystems.size()); subsystem++) {
		int cnt = 0;
		std::vector<CPS::Signal::ringbufferValues_t> subsystemValues;
		for (auto comp : mSys.mComponents) {
			auto pcomp = std::dynamic_pointer_cast<CPS::Signal::DecouplingLine>(comp);
			if (pcomp && pcomp->mSubsystem == mSubsystems[subsystem]) {
				subsystemValues.push_back(pcomp->getLastRingbufferValues());
				cnt++;
			}
		}
		CPS::Signal::ringbufferValues_t vals[subsystemValues.size()];
		for (long unsigned int i = 0; i < subsystemValues.size(); i++)
			vals[i] = subsystemValues[i];
		decouplingLineValues_t value;
		value.subsystem = mSubsystems[subsystem];
		value.lenValues = cnt;
		if (cnt > 0) {
			value.values = &vals[0];
			std::cout << "getValues idx: " << vals[0].idx << "\n";
			std::cout << "getValues2 idx: " << value.values[0].idx << "\n";
		}
		else {
			CPS::Signal::ringbufferValues_t val;
			val.idx = 0;
			val.values[0] = 0;
			val.values[1] = 0;
			val.values[2] = 0;
			val.values[3] = 0;
			value.values = &val;
		}
		values.push_back(value);
	}

	return values;
}

void MPILevelScheduler::setDecouplingLineValues(char* data) {
	//TODO: set information
	int i = 0, k = 0, l = 0;
	
	std::vector<decouplingLineValues_t> values;

	int lenValues;
	std::memcpy(&lenValues, data, sizeof(int));
	data += sizeof(int);

	for (i = 0; i < lenValues; i++) {
		decouplingLineValues_t value;

		int subsystem;
		std::memcpy(&subsystem, data, sizeof(int));
		value.subsystem = subsystem;
		data += sizeof(int);

		int lenVals;
		std::memcpy(&lenVals, data, sizeof(int));
		value.lenValues = lenVals;
		data += sizeof(int);

		for (k = 0; k < lenVals; k++) {
			CPS::Signal::ringbufferValues_t vals;

			UInt idx;
			std::memcpy(&idx, data, sizeof(UInt));
			vals.idx = idx;
			data += sizeof(UInt);

			for (l = 0; l < 4; l++) {
				Complex c;
				std::memcpy(&c, data, sizeof(Complex));
				vals.values[l] = c;
				data += sizeof(Complex);
			}

			value.values = &vals;
		}

		values.push_back(value);
	}

	for (auto val : values) {
		for (auto comp : mSys.mComponents) {
                        auto pcomp = std::dynamic_pointer_cast<CPS::Signal::DecouplingLine>(comp);
                        if (pcomp)
                                if (val.subsystem == pcomp->mSubsystem)
					pcomp->setLastRingbufferValues(*val.values++);
                }
	}
}

void MPILevelScheduler::stop() {
	int finalized = 0;

	if (!mOutMeasurementFile.empty())
		writeMeasurements(mOutMeasurementFile);

	MPI_Finalized(&finalized);
	if (!finalized)
		MPI_Finalize();
}
