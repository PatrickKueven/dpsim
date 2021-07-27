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

	Logger::setLogDir(Logger::logDir() + "_" + std::to_string(mRank));
}

void MPILevelScheduler::createSchedule(const Task::List& tasks, const Edges& inEdges, const Edges& outEdges) {
	Task::List ordered;
	long i = 0,  level = 0, maxSubsystem = 0;

	std::vector<CPS::Task::List> levels;

	Scheduler::topologicalSort(tasks, inEdges, outEdges, ordered);
	Scheduler::levelSchedule(ordered, inEdges, outEdges, levels);

	if (!mOutMeasurementFile.empty())
		Scheduler::initMeasurements(tasks);

	for (level = 0; level < static_cast<long>(levels.size()); level++)
                for (i = 0; i < static_cast<long>(levels[level].size()); i++) {
                        if (levels[level][i]->getSubsystem() > maxSubsystem)
                                maxSubsystem = levels[level][i]->getSubsystem();
		}

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
	
	for (i = 0; i < mNumRanks; i++) {
		std::vector<int> tmp;
		mSubsystems.push_back(tmp);
	}

	for (i = 0; i <= maxSubsystem; i++)
		mSubsystems[i % mNumRanks].push_back(i);

	defineSizesOfDecouplingLineValues();

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
		for (subsystem = 0; subsystem < static_cast<long>(mSubsystems[mRank].size()); subsystem++) {
			for (level = 0; level < static_cast<long>(mLevels[mSubsystems[mRank][subsystem]].size()); level++) {
				for (i = 0; i < static_cast<long>(mLevels[mSubsystems[mRank][subsystem]][level].size()); i++)
					mLevels[mSubsystems[mRank][subsystem]][level][i]->execute(time, timeStepCount);
			}
		}

		//std::cout << mRank << "start\n";

		for (i = 0; i < mNumRanks; i++) {
			if (mSizesOfDecouplingLineValuesPerRank[i] != 0) {
				char* data = (char*)malloc(mSizesOfDecouplingLineValuesPerRank[i]);
				if (i == mRank)
					getData(data);
				MPI_Barrier(MPI_COMM_WORLD);
				MPI_Bcast(data, mSizesOfDecouplingLineValuesPerRank[i], MPI_BYTE, i, MPI_COMM_WORLD);
				MPI_Barrier(MPI_COMM_WORLD);
				if (i != mRank)
					setData(data, i);
				free(data);
				MPI_Barrier(MPI_COMM_WORLD);
			}
		}

		//std::cout << mRank << "end\n";
	}
}

void MPILevelScheduler::defineSizesOfDecouplingLineValues() {
	mSizesOfDecouplingLineValuesPerRank = new long[mNumRanks];

	int sizeRingbufferValues = sizeof(UInt) + 2 * sizeof(Complex);

	for (int i = 0; i < mNumRanks; i++) {
		mSizesOfDecouplingLineValuesPerRank[i] = 0L;
		for (int subsystem : mSubsystems[i]) {
			long cnt = 0;
	                for (auto comp : mSys.mComponents) {
        	                auto pcomp = std::dynamic_pointer_cast<CPS::Signal::DecouplingLine>(comp);
                	        if (pcomp && pcomp->getSubsystem() == subsystem)
                        	        cnt++;
	                }
	        	mSizesOfDecouplingLineValuesPerRank[i] += cnt * sizeRingbufferValues;
		}
	}
}

void MPILevelScheduler::getData(char* data) {
	long subsystem = 0;

        for (subsystem = 0; subsystem < static_cast<long>(mSubsystems[mRank].size()); subsystem++) {
                for (auto comp : mSys.mComponents) {
                        auto pcomp = std::dynamic_pointer_cast<CPS::Signal::DecouplingLine>(comp);
                        if (pcomp && pcomp->mSubsystem == mSubsystems[mRank][subsystem])
                                pcomp->getLastRingbufferValues(data);
                }
	}
}

void MPILevelScheduler::setData(char* data, int rank) {
	long subsystem = 0;

        for (subsystem = 0; subsystem < static_cast<long>(mSubsystems[rank].size()); subsystem++) {
                for (auto comp : mSys.mComponents) {
                        auto pcomp = std::dynamic_pointer_cast<CPS::Signal::DecouplingLine>(comp);
                        if (pcomp && pcomp->mSubsystem == mSubsystems[rank][subsystem])
				pcomp->mOtherEndOfDecouplingLine->setLastRingbufferValues(data);
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
