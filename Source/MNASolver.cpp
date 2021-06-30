/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/


#include <dpsim/MNASolver.h>
#include <dpsim/SequentialScheduler.h>
#include <memory>

using namespace DPsim;
using namespace CPS;

namespace DPsim {


template <typename VarType>
MnaSolver<VarType>::MnaSolver(String name, CPS::Domain domain, CPS::Logger::Level logLevel) :
	Solver(name, logLevel), mDomain(domain) {

	// Raw source and solution vector logging
	mLeftVectorLog = std::make_shared<DataLogger>(name + "_LeftVector", logLevel != CPS::Logger::Level::off);
	mRightVectorLog = std::make_shared<DataLogger>(name + "_RightVector", logLevel != CPS::Logger::Level::off);
}

template <typename VarType>
void MnaSolver<VarType>::setSystem(const CPS::SystemTopology &system) {
	mSystem = system;
}

template <typename VarType>
void MnaSolver<VarType>::initialize() {
	// TODO: check that every system matrix has the same dimensions
	mSLog->info("---- Start initialization ----");

	mSLog->info("-- Process topology");
	for (auto comp : mSystem.mComponents)
		mSLog->info("Added {:s} '{:s}' to simulation.", comp->type(), comp->name());

	// Otherwise LU decomposition will fail
	if (mSystem.mComponents.size() == 0)
		throw SolverException();

	// We need to differentiate between power and signal components and
	// ground nodes should be ignored.
	identifyTopologyObjects();
	// These steps complete the network information.
	collectVirtualNodes();
	assignMatrixNodeIndices();

	mSLog->info("-- Create empty MNA system matrices and vectors");
	createEmptyVectors();
	createEmptySystemMatrix();

	// Register attribute for solution vector
	if (mFrequencyParallel) {
		mSLog->info("Computing network harmonics in parallel.");
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); ++freq) {
			addAttribute<Matrix>("left_vector_"+std::to_string(freq), mLeftSideVectorHarm.data()+freq, Flags::read);
			mLeftVectorHarmAttributes.push_back(attribute<Matrix>("left_vector_"+std::to_string(freq)));
		}
	}
	else {
		addAttribute<Matrix>("left_vector", &mLeftSideVector, Flags::read);
	}

	// Initialize components from powerflow solution and
	// calculate MNA specific initialization values.
	initializeComponents();

	if (mSteadyStateInit) {
		mIsInInitialization = true;
		steadyStateInitialization();
	}
	mIsInInitialization = false;

	// Some components feature a different behaviour for simulation and initialization
	for (auto comp : mSystem.mComponents) {
		auto powerComp = std::dynamic_pointer_cast<CPS::TopologicalPowerComp>(comp);
		if (powerComp) powerComp->setBehaviour(TopologicalPowerComp::Behaviour::Simulation);

		auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
		if (sigComp) sigComp->setBehaviour(SimSignalComp::Behaviour::Simulation);
	}

	// Initialize system matrices and source vector.
	initializeSystem();

	mSLog->info("--- Initialization finished ---");
	mSLog->info("--- Initial system matrices and vectors ---");
	logSystemMatrices();

	mSLog->flush();
}

template <>
void MnaSolver<Real>::initializeComponents() {
	mSLog->info("-- Initialize components from power flow");
	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Real>>(comp);
		if (!pComp)	continue;
		pComp->checkForUnconnectedTerminals();
		pComp->initializeFromNodesAndTerminals(mSystem.mSystemFrequency);
	}

	// Initialize signal components.
	for (auto comp : mSimSignalComps)
		comp->initialize(mSystem.mSystemOmega, mTimeStep);

	// Initialize MNA specific parts of components.
	for (auto comp : mMNAComponents) {
		comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
		const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
		if (stamp.size() != 0) {
			mRightVectorStamps.push_back(&stamp);
		}
	}
	for (auto comp : mSwitches)
		comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
}

template <>
void MnaSolver<Complex>::initializeComponents() {
	mSLog->info("-- Initialize components from power flow");

	// Initialize power components with frequencies and from powerflow results
	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Complex>>(comp);
		if (!pComp)	continue;
		pComp->checkForUnconnectedTerminals();
		pComp->initializeFromNodesAndTerminals(mSystem.mSystemFrequency);
	}

	// Initialize signal components.
	for (auto comp : mSimSignalComps)
		comp->initialize(mSystem.mSystemOmega, mTimeStep);

	mSLog->info("-- Initialize MNA properties of components");
	if (mFrequencyParallel) {
		// Initialize MNA specific parts of components.
		for (auto comp : mMNAComponents) {
			// Initialize MNA specific parts of components.
			comp->mnaInitializeHarm(mSystem.mSystemOmega, mTimeStep, mLeftVectorHarmAttributes);
			const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
			if (stamp.size() != 0) mRightVectorStamps.push_back(&stamp);
		}
		// Initialize nodes
		for (UInt nodeIdx = 0; nodeIdx < mNodes.size(); ++nodeIdx) {
			mNodes[nodeIdx]->mnaInitializeHarm(mLeftVectorHarmAttributes);
		}
	}
	else {
		// Initialize MNA specific parts of components.
		for (auto comp : mMNAComponents) {
			comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
			const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
			if (stamp.size() != 0) {
				mRightVectorStamps.push_back(&stamp);
			}
		}
		for (auto comp : mSwitches)
			comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
	}
}

template <typename VarType>
void MnaSolver<VarType>::initializeSystem() {
	mSLog->info("-- Initialize MNA system matrices and source vector");
	mRightSideVector.setZero();

	// just a sanity check in case we change the static
	// initialization of the switch number in the future
	if (mSwitches.size() > sizeof(std::size_t)*8) {
		throw SystemError("Too many Switches.");
	}

	if (mFrequencyParallel)
		initializeSystemWithParallelFrequencies();
	else
		initializeSystemWithPrecomputedMatrices();
}

template <typename VarType>
void MnaSolver<VarType>::initializeSystemWithParallelFrequencies() {
	// iterate over all possible switch state combinations
	for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); ++freq)
			mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(i)][freq].setZero();
	}

	for(Int freq = 0; freq < mSystem.mFrequencies.size(); ++freq) {
		// Create system matrix if no switches were added
		// TODO add case for switches and possibly merge with no harmonics
		for (auto comp : mMNAComponents)
			comp->mnaApplySystemMatrixStampHarm(mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)][freq], freq);

		mLuFactorizationsHarm[std::bitset<SWITCH_NUM>(0)].push_back(
			Eigen::PartialPivLU<Matrix>(mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)][freq]));

		// Initialize source vector
		for (auto comp : mMNAComponents)
			comp->mnaApplyRightSideVectorStampHarm(mRightSideVectorHarm[freq], freq);
	}
}

template <typename VarType>
void MnaSolver<VarType>::initializeSystemWithPrecomputedMatrices() {
	// iterate over all possible switch state combinations
	for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
		switchedMatrixEmpty(i);
	}

	if (mSwitches.size() < 1) {
		switchedMatrixStamp(0, mMNAComponents);
	}
	else {
		// Generate switching state dependent system matrices
		for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
			switchedMatrixStamp(i, mMNAComponents);
		}
		updateSwitchStatus();
	}

	// Initialize source vector for debugging
	// CAUTION: this does not always deliver proper source vector initialization
	// as not full pre-step is executed (not involving necessary electrical or signal
	// subcomp updates before right vector calculation)
	for (auto comp : mMNAComponents) {
		comp->mnaApplyRightSideVectorStamp(mRightSideVector);
		auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
		mSLog->debug("Stamping {:s} {:s} into source vector",
			idObj->type(), idObj->name());
		if (mSLog->should_log(spdlog::level::trace))
			mSLog->trace("\n{:s}", Logger::matrixToString(mRightSideVector));
	}
}

template <typename VarType>
void MnaSolver<VarType>::updateSwitchStatus() {
	for (UInt i = 0; i < mSwitches.size(); ++i) {
		mCurrentSwitchStatus.set(i, mSwitches[i]->mnaIsClosed());
	}
}

template <typename VarType>
void MnaSolver<VarType>::identifyTopologyObjects() {
	for (auto baseNode : mSystem.mNodes) {
		// Add nodes to the list and ignore ground nodes.
		if (!baseNode->isGround()) {
			auto node = std::dynamic_pointer_cast< CPS::SimNode<VarType> >(baseNode);
			mNodes.push_back(node);
			mSLog->info("Added node {:s}", node->name());
		}
	}

	for (auto comp : mSystem.mComponents) {

		auto swComp = std::dynamic_pointer_cast<CPS::MNASwitchInterface>(comp);
		if (swComp) {
			mSwitches.push_back(swComp);
			auto mnaComp = std::dynamic_pointer_cast<CPS::MNAInterface>(swComp);
			if (mnaComp) mMNAIntfSwitches.push_back(mnaComp);
		}

		auto varComp = std::dynamic_pointer_cast<CPS::MNAVariableCompInterface>(comp);
		if (varComp) {
			mVariableComps.push_back(varComp);
			auto mnaComp = std::dynamic_pointer_cast<CPS::MNAInterface>(varComp);
			if (mnaComp) mMNAIntfVariableComps.push_back(mnaComp);
		}

		if (!(swComp || varComp)) {
			auto mnaComp = std::dynamic_pointer_cast<CPS::MNAInterface>(comp);
			if (mnaComp) mMNAComponents.push_back(mnaComp);

			auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
			if (sigComp) mSimSignalComps.push_back(sigComp);
		}
	}
}

template <typename VarType>
void MnaSolver<VarType>::assignMatrixNodeIndices() {
	UInt matrixNodeIndexIdx = 0;
	for (UInt idx = 0; idx < mNodes.size(); ++idx) {
		mNodes[idx]->setMatrixNodeIndex(0, matrixNodeIndexIdx);
		mSLog->info("Assigned index {} to phase A of node {}", matrixNodeIndexIdx, idx);
		++matrixNodeIndexIdx;
		if (mNodes[idx]->phaseType() == CPS::PhaseType::ABC) {
			mNodes[idx]->setMatrixNodeIndex(1, matrixNodeIndexIdx);
			mSLog->info("Assigned index {} to phase B of node {}", matrixNodeIndexIdx, idx);
			++matrixNodeIndexIdx;
			mNodes[idx]->setMatrixNodeIndex(2, matrixNodeIndexIdx);
			mSLog->info("Assigned index {} to phase B of node {}", matrixNodeIndexIdx, idx);
			++matrixNodeIndexIdx;
		}
		if (idx == mNumNetNodes-1) mNumNetMatrixNodeIndices = matrixNodeIndexIdx;
	}
	// Total number of network nodes is matrixNodeIndexIdx + 1
	mNumMatrixNodeIndices = matrixNodeIndexIdx;
	mNumVirtualMatrixNodeIndices = mNumMatrixNodeIndices - mNumNetMatrixNodeIndices;
	mNumHarmMatrixNodeIndices = static_cast<UInt>(mSystem.mFrequencies.size()-1) * mNumMatrixNodeIndices;

	mSLog->info("Assigned simulation nodes to topology nodes:");
	mSLog->info("Number of network simulation nodes: {:d}", mNumNetMatrixNodeIndices);
	mSLog->info("Number of simulation nodes: {:d}", mNumMatrixNodeIndices);
	mSLog->info("Number of harmonic simulation nodes: {:d}", mNumHarmMatrixNodeIndices);
}

template<>
void MnaSolver<Real>::createEmptyVectors() {
	mRightSideVector = Matrix::Zero(mNumMatrixNodeIndices, 1);
	mLeftSideVector = Matrix::Zero(mNumMatrixNodeIndices, 1);
}

template<>
void MnaSolver<Complex>::createEmptyVectors() {
	if (mFrequencyParallel) {
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); ++freq) {
			mRightSideVectorHarm.push_back(Matrix::Zero(2*(mNumMatrixNodeIndices), 1));
			mLeftSideVectorHarm.push_back(Matrix::Zero(2*(mNumMatrixNodeIndices), 1));
		}
	}
	else {
		mRightSideVector = Matrix::Zero(2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 1);
		mLeftSideVector = Matrix::Zero(2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 1);
	}
}

template <typename VarType>
void MnaSolver<VarType>::collectVirtualNodes() {
	// We have not added virtual nodes yet so the list has only network nodes
	mNumNetNodes = (UInt) mNodes.size();
	// virtual nodes are placed after network nodes
	UInt virtualNode = mNumNetNodes - 1;

	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<VarType>>(comp);
		if (!pComp)	continue;

		// Check if component requires virtual node and if so get a reference
		if (pComp->hasVirtualNodes()) {
			for (UInt node = 0; node < pComp->virtualNodesNumber(); ++node) {
				mNodes.push_back(pComp->virtualNode(node));
				mSLog->info("Collected virtual node {} of {}", virtualNode, node, pComp->name());
			}
		}

		// Repeat the same steps for virtual nodes of sub components
		// TODO: recursive behavior
		if (pComp->hasSubComponents()) {
			for (auto pSubComp : pComp->subComponents()) {
				for (UInt node = 0; node < pSubComp->virtualNodesNumber(); ++node) {
					mNodes.push_back(pSubComp->virtualNode(node));
					mSLog->info("Collected virtual node {} of {}", virtualNode, node, pComp->name());
				}
			}
		}
	}

	// Update node number to create matrices and vectors
	mNumNodes = (UInt) mNodes.size();
	mNumVirtualNodes = mNumNodes - mNumNetNodes;
	mSLog->info("Created virtual nodes:");
	mSLog->info("Number of network nodes: {:d}", mNumNetNodes);
	mSLog->info("Number of network and virtual nodes: {:d}", mNumNodes);
}

template <typename VarType>
void MnaSolver<VarType>::steadyStateInitialization() {
	mSLog->info("--- Run steady-state initialization ---");

	DataLogger initLeftVectorLog(mName + "_InitLeftVector", mLogLevel != CPS::Logger::Level::off);
	DataLogger initRightVectorLog(mName + "_InitRightVector", mLogLevel != CPS::Logger::Level::off);

	TopologicalPowerComp::Behaviour initBehaviourPowerComps = TopologicalPowerComp::Behaviour::Initialization;
	SimSignalComp::Behaviour initBehaviourSignalComps = SimSignalComp::Behaviour::Initialization;

	// TODO: enable use of timestep distinct from simulation timestep
	Real initTimeStep = mTimeStep;

	Int timeStepCount = 0;
	Real time = 0;
	Real maxDiff = 1.0;
	Real max = 1.0;
	Matrix diff = Matrix::Zero(2 * mNumNodes, 1);
	Matrix prevLeftSideVector = Matrix::Zero(2 * mNumNodes, 1);

	mSLog->info("Time step is {:f}s for steady-state initialization", initTimeStep);

	for (auto comp : mSystem.mComponents) {
		auto powerComp = std::dynamic_pointer_cast<CPS::TopologicalPowerComp>(comp);
		if (powerComp) powerComp->setBehaviour(initBehaviourPowerComps);

		auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
		if (sigComp) sigComp->setBehaviour(initBehaviourSignalComps);
	}

	initializeSystem();
	logSystemMatrices();

	// Use sequential scheduler
	SequentialScheduler sched;
	CPS::Task::List tasks;
	Scheduler::Edges inEdges, outEdges;

	for (auto node : mNodes) {
		for (auto task : node->mnaTasks())
			tasks.push_back(task);
	}
	for (auto comp : mMNAComponents) {
		for (auto task : comp->mnaTasks()) {
			tasks.push_back(task);
		}
	}
	// TODO signal components should be moved out of MNA solver
	for (auto comp : mSimSignalComps) {
		for (auto task : comp->getTasks()) {
			tasks.push_back(task);
		}
	}
	auto solveTask = createSolveTask();
	solveTask->setSubsystem(mSubsystem);
	tasks.push_back(solveTask);

	sched.resolveDeps(tasks, inEdges, outEdges);
	sched.createSchedule(tasks, inEdges, outEdges);

	while (time < mSteadStIniTimeLimit) {
		// Reset source vector
		mRightSideVector.setZero();

		sched.step(time, timeStepCount);

		if (mDomain == CPS::Domain::EMT) {
			initLeftVectorLog.logEMTNodeValues(time, leftSideVector());
			initRightVectorLog.logEMTNodeValues(time, rightSideVector());
		}
		else {
			initLeftVectorLog.logPhasorNodeValues(time, leftSideVector());
			initRightVectorLog.logPhasorNodeValues(time, rightSideVector());
		}

		// Calculate new simulation time
		time = time + initTimeStep;
		++timeStepCount;

		// Calculate difference
		diff = prevLeftSideVector - mLeftSideVector;
		prevLeftSideVector = mLeftSideVector;
		maxDiff = diff.lpNorm<Eigen::Infinity>();
		max = mLeftSideVector.lpNorm<Eigen::Infinity>();
		// If difference is smaller than some epsilon, break
		if ((maxDiff / max) < mSteadStIniAccLimit)
			break;
	}

	mSLog->info("Max difference: {:f} or {:f}% at time {:f}", maxDiff, maxDiff / max, time);

	// Reset system for actual simulation
	mRightSideVector.setZero();

	mSLog->info("--- Finished steady-state initialization ---");
}

template <typename VarType>
Task::List MnaSolver<VarType>::getTasks() {
	Task::List l;

	for (auto comp : mMNAComponents) {
		for (auto task : comp->mnaTasks()) {
			l.push_back(task);
		}
	}
	for (auto comp : mSwitches) {
		for (auto task : comp->mnaTasks()) {
			l.push_back(task);
		}
	}
	for (auto node : mNodes) {
		for (auto task : node->mnaTasks())
			l.push_back(task);
	}
	// TODO signal components should be moved out of MNA solver
	for (auto comp : mSimSignalComps) {
		for (auto task : comp->getTasks()) {
			l.push_back(task);
		}
	}
	if (mFrequencyParallel) {
		for (UInt i = 0; i < mSystem.mFrequencies.size(); ++i) {
			auto taskHarm = createSolveTaskHarm(i);
			taskHarm->setSubsystem(mSubsystem);
			l.push_back(taskHarm);
		}
	} else {
		auto solveTask = createSolveTask();
		solveTask->setSubsystem(mSubsystem);
		l.push_back(solveTask);
		auto logTask = createLogTask();
		logTask->setSubsystem(mSubsystem);
		l.push_back(logTask);
	}
	return l;
}


template <typename VarType>
void MnaSolver<VarType>::log(Real time, Int timeStepCount) {
	if (mLogLevel == Logger::Level::off)
		return;

	if (mDomain == CPS::Domain::EMT) {
		mLeftVectorLog->logEMTNodeValues(time, leftSideVector());
		mRightVectorLog->logEMTNodeValues(time, rightSideVector());
	}
	else {
		mLeftVectorLog->logPhasorNodeValues(time, leftSideVector());
		mRightVectorLog->logPhasorNodeValues(time, rightSideVector());
	}
}

}

template class DPsim::MnaSolver<Real>;
template class DPsim::MnaSolver<Complex>;
