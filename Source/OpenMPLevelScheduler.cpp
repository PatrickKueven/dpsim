/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <dpsim/OpenMPLevelScheduler.h>
#include <omp.h>

#include <iostream>
#include <fstream>

using namespace CPS;
using namespace DPsim;

OpenMPLevelScheduler::OpenMPLevelScheduler(Int threads, String outMeasurementFile) : mOutMeasurementFile(outMeasurementFile) {
	size_t i = 0, j = 0, k = 0;
	if (threads >= 0)
		mNumThreads = threads;
	else
		mNumThreads = omp_get_num_threads();
	for (i = 0; i < 5001; i++) {
		std::vector<std::vector<std::chrono::nanoseconds>> vec;
		for (j = 0; j < (size_t)mNumThreads; j++) {
			std::vector<std::chrono::nanoseconds> vec1;
			for (k = 0; k < 3; k++) {
				vec1.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::nanoseconds(0)));
			}
			vec.push_back(vec1);
		}
		std::vector<std::chrono::nanoseconds> vecAll;
		vecAll.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::nanoseconds(0)));
		vec.push_back(vecAll);
		mTimes.push_back(vec);
	}
	mCount = 0;
	mPreStep = "PreStep";
	mSolve = "Solve";
	mPostStep = "PostStep";
}

void OpenMPLevelScheduler::createSchedule(const Task::List& tasks, const Edges& inEdges, const Edges& outEdges) {
	Task::List ordered;

	Scheduler::topologicalSort(tasks, inEdges, outEdges, ordered);
	Scheduler::levelSchedule(ordered, inEdges, outEdges, mLevels);

	if (!mOutMeasurementFile.empty())
		Scheduler::initMeasurements(tasks);
}

void OpenMPLevelScheduler::step(Real time, Int timeStepCount) {
	long i = 0, level = 0;
	mCount++;
	std::chrono::steady_clock::time_point start, end;
//	std::ofstream outFile1, outFile2, outFile3, outFile4, outFile5, outFile6, outFile7, outFile8, outFile9, outFile10, outFile11, outFile12, outFile13, outFile14, outFile15, outFile16, outFile17, outFile18, outFile19, outFile20;
//	outFile1.open("test1");
//	outFile2.open("test2");
//	outFile3.open("test3");
//	outFile4.open("test4");
//	outFile5.open("test5");
//	outFile6.open("test6");
//	outFile7.open("test7");
//	outFile8.open("test8");
//	outFile9.open("test9");
//	outFile10.open("test10");
//	outFile11.open("test11");
//	outFile12.open("test12");
//	outFile13.open("test13");
//	outFile14.open("test14");
//	outFile15.open("test15");
//	outFile16.open("test16");
//	outFile17.open("test17");
//	outFile18.open("test18");
//	outFile19.open("test19");
//	outFile20.open("test20");
	auto startAll = std::chrono::system_clock::now();

	if (!mOutMeasurementFile.empty()) {
		#pragma omp parallel shared(time,timeStepCount) private(level, i, start, end) num_threads(mNumThreads)
		for (level = 0; level < static_cast<long>(mLevels.size()); level++) {
			{
				#pragma omp for schedule(static)
				for (i = 0; i < static_cast<long>(mLevels[level].size()); i++) {
					start = std::chrono::steady_clock::now();
					mLevels[level][i]->execute(time, timeStepCount);
					end = std::chrono::steady_clock::now();
					updateMeasurement(mLevels[level][i].get(), end-start);
				}
			}
		}
	} else {
		#pragma omp parallel shared(time,timeStepCount) private(level, i) num_threads(mNumThreads)
		for (level = 0; level < static_cast<long>(mLevels.size()); level++) {
			{
				#pragma omp for schedule(static)
				for (i = 0; i < static_cast<long>(mLevels[level].size()); i++) {
					auto start1 = std::chrono::system_clock::now();
					mLevels[level][i]->execute(time, timeStepCount);
					auto end1 = std::chrono::system_clock::now();
					auto elapsed_nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
					int thread = omp_get_thread_num();
					if (mLevels[level][i]->toString().find(mPreStep) != std::string::npos) {
						mTimes[mCount-1][thread][0] += elapsed_nanoseconds;
					}
					else if (mLevels[level][i]->toString().find(mSolve) != std::string::npos) {
						mTimes[mCount-1][thread][1] += elapsed_nanoseconds;
					}
					else if (mLevels[level][i]->toString().find(mPostStep) != std::string::npos) {
						mTimes[mCount-1][thread][2] += elapsed_nanoseconds;
					}
//					std::ostringstream ss;
//					ss.precision(15);
//					ss << std::fixed << (elapsed_nanoseconds.count() / 1000000000.0);
//					switch (omp_get_thread_num()) {
//						case 0:
//							outFile1 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//							break;
//						case 1:
//								outFile2 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 2:
//								outFile3 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 3:
//								outFile4 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 4:
//								outFile5 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 5:
//								outFile6 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 6:
//								outFile7 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 7:
//								outFile8 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 8:
//								outFile9 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 9:
//								outFile10 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 10:
//								outFile11 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 11:
//								outFile12 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 12:
//								outFile13 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 13:
//								outFile14 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 14:
//								outFile15 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 15:
//								outFile16 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 16:
//								outFile17 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 17:
//								outFile18 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 18:
//								outFile19 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//						case 19:
//								outFile20 << mLevels[level][i]->toString().c_str() << ";" << ss.str() << "\n";
//								break;
//					}
				}
			}
		}
	}
//	outFile1.close();
//	outFile2.close();
//	outFile3.close();
//	outFile4.close();
//	outFile5.close();
//	outFile6.close();
//	outFile7.close();
//	outFile8.close();
//	outFile9.close();
//	outFile10.close();
//	outFile11.close();
//	outFile12.close();
//	outFile13.close();
//	outFile14.close();
//	outFile15.close();
//	outFile16.close();
//	outFile17.close();
//	outFile18.close();
//	outFile19.close();
//	outFile20.close();
	auto endAll = std::chrono::system_clock::now();
	auto elapsed_nanoseconds_all = std::chrono::duration_cast<std::chrono::nanoseconds>(endAll - startAll);
	mTimes[mCount-1][mNumThreads][0] += elapsed_nanoseconds_all;
//	std::ostringstream ss;
//	ss.precision(15);
//	ss << std::fixed << (elapsed_nanoseconds.count() / 1000000000.0);
//	mOutFile << mCount << ";" << ss.str() << "\n";
}

void OpenMPLevelScheduler::stop() {
	if (!mOutMeasurementFile.empty()) {
		writeMeasurements(mOutMeasurementFile);
	}
	OpenMPLevelScheduler::printFileOfTimes();
}

void OpenMPLevelScheduler::printFileOfTimes() {
	size_t i, j, k;
	std::ofstream outFile;
	outFile.open("measurement.csv");

	outFile << "#;";
	for (i = 0; i < (size_t)mNumThreads; i++) {
		outFile << "t" << i << "_PreStep;t" << i << "_Solve;t" << i << "_PostStep;";
	}
	outFile << "Overall\n";

	for (i = 0; i < 5001; i++) {
		std::ostringstream ss;
		ss << std::fixed;
		outFile << i + 1 << ";";
		for (j = 0; j < (size_t)mNumThreads; j++) {
			for (k = 0; k < 3; k++) {
				ss << (mTimes[i][j][k].count() / 1000000000.0) << ";";
			}
		}
		ss << (mTimes[i][mNumThreads][0].count()  / 1000000000.0);
		outFile << ss.str() << "\n";
	}

	for (i = 0; i < 5001; i++) {
		for (j = 0; j < (size_t)mNumThreads; j++) {
			mTimes[i][j].clear();
		}
		mTimes[i].clear();
	}
	mTimes.clear();

	outFile.close();
}
