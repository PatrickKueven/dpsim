/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <dpsim/MPILevelScheduler.h>
#include <mpi.h>

#include <iostream>
#include <fstream>

using namespace CPS;
using namespace DPsim;

MPILevelScheduler::MPILevelScheduler(Int threads, String outMeasurementFile) : mOutMeasurementFile(outMeasurementFile) {
	int initialized = 0;
	MPI_Initialized(&initialized);
	if (!initialized) {
	   MPI_Init(NULL, NULL);
	   initialized = 1;
	}

	if (threads >= 0)
		mNumRanks = threads;
	else
		MPI_Comm_size(MPI_COMM_WORLD, &mNumRanks);
	        //mNumRanks = 1;

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
	long i = 0, level = 0;
	
	if (mRank == 0)
		//MPI_Isend(...)
		std::cout << timeStepCount << ": Rank 0: Send\n";
	else
		//MPI_Recv(...)
		std::cout << timeStepCount << ": Rank " << mRank << ": Recv\n";

	if (!mOutMeasurementFile.empty()) {
		for (level = 0; level < static_cast<long>(mLevels[mRank].size()); level++)
				for (i = 0; i < static_cast<long>(mLevels[mRank][level].size()); i++) {
					auto start = std::chrono::steady_clock::now();
					mLevels[mRank][level][i]->execute(time, timeStepCount);
					auto end = std::chrono::steady_clock::now();
					updateMeasurement(mLevels[mRank][level][i].get(), end-start);
				}
	} else
		for (level = 0; level < static_cast<long>(mLevels[mRank].size()); level++) {
			for (i = 0; i < static_cast<long>(mLevels[mRank][level].size()); i++)
				mLevels[mRank][level][i]->execute(time, timeStepCount);
			//Hier vermutlich an der falschen Stelle. Nicht alle Prozesse durchlaufen alle Level.
			//Ansonsten werden nach dieser Barriere u.U. die Daten getauscht
			//MPI_Barrier(MPI_COMM_WORLD);
			std::cout << timeStepCount << ": Rank " << mRank << ": Barrier\n";
		}
}

void MPILevelScheduler::stop() {
	int finalized = 0;

	if (!mOutMeasurementFile.empty())
		writeMeasurements(mOutMeasurementFile);

	//MPI_Finalized(&finalized);
	if (!finalized)
		//MPI_Finalize();
		finalized = 1;
}
