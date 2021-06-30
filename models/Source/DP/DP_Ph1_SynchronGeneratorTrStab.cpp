/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <cps/DP/DP_Ph1_SynchronGeneratorTrStab.h>
using namespace CPS;

DP::Ph1::SynchronGeneratorTrStab::SynchronGeneratorTrStab(String uid, String name, Logger::Level logLevel)
	: SimPowerComp<Complex>(uid, name, logLevel) {
	setVirtualNodeNumber(2);
	setTerminalNumber(1);
	mIntfVoltage = MatrixComp::Zero(1, 1);
	mIntfCurrent = MatrixComp::Zero(1, 1);

	// Register attributes
	addAttribute<Real>("Ep_mag", &mEp_abs, Flags::read);
	addAttribute<Real>("Ep_phase", &mDelta_p, Flags::read);
	addAttribute<Real>("P_elec", &mElecActivePower, Flags::read);
	addAttribute<Real>("P_mech", &mMechPower, Flags::read);
	addAttribute<Real>("w_r", &mOmMech, Flags::read);
	addAttribute<Real>("inertia", &mInertia, Flags::read | Flags::write);

	mStates = Matrix::Zero(10,1);
}

SimPowerComp<Complex>::Ptr DP::Ph1::SynchronGeneratorTrStab::clone(String name) {
	auto copy = SynchronGeneratorTrStab::make(name, mLogLevel);
	copy->setStandardParametersPU(mNomPower, mNomVolt, mNomFreq, mXpd / mBase_Z, mInertia);
	return copy;
}

void DP::Ph1::SynchronGeneratorTrStab::setFundamentalParametersPU(Real nomPower, Real nomVolt, Real nomFreq,
	Real Ll, Real Lmd, Real Llfd, Real inertia) {
	setBaseParameters(nomPower, nomVolt, nomFreq);

	// Input is in per unit but all values are converted to absolute values.
	mParameterType = ParameterType::statorReferred;
	mStateType = StateType::statorReferred;

	mLl = Ll;
	mLmd = Lmd;
	mLd = mLl + mLmd;
	mLlfd = Llfd;
	mLfd = mLlfd + mLmd;
	// M = 2*H where H = inertia
	mInertia = inertia;
	// X'd in absolute values
	mXpd = mNomOmega * (mLd - mLmd*mLmd / mLfd) * mBase_L;
	mLpd = (mLd - mLmd*mLmd / mLfd) * mBase_L;

	mSLog->info("\n--- Parameters ---"
				"\nimpedance: {:f}"
				"\ninductance: {:f}", mXpd, mLpd);
}

void DP::Ph1::SynchronGeneratorTrStab::setStandardParametersSI(Real nomPower, Real nomVolt, Real nomFreq, Int polePairNumber,
	Real Rs, Real Lpd, Real inertiaJ, Real Kd) {
	setBaseParameters(nomPower, nomVolt, nomFreq);

	mParameterType = ParameterType::statorReferred;
	mStateType = StateType::statorReferred;

	// M = 2*H where H = inertia
	// H = J * 0.5 * omegaNom^2 / polePairNumber
	mInertia = calcHfromJ(inertiaJ, 2*PI*nomFreq, polePairNumber);
	// X'd in absolute values
	mXpd = mNomOmega * Lpd;
	mLpd = Lpd;

	mSLog->info("\n--- Parameters ---"
				"\nimpedance: {:f}"
				"\ninductance: {:f}", mXpd, mLpd);
}

void DP::Ph1::SynchronGeneratorTrStab::setStandardParametersPU(Real nomPower, Real nomVolt, Real nomFreq,
	Real Xpd, Real inertia) {
	setBaseParameters(nomPower, nomVolt, nomFreq);

	// Input is in per unit but all values are converted to absolute values.
	mParameterType = ParameterType::statorReferred;
	mStateType = StateType::statorReferred;

	// M = 2*H where H = inertia
	mInertia = inertia;
	// X'd in absolute values
	mXpd = Xpd * mBase_Z;
	mLpd = Xpd * mBase_L;

	mSLog->info("\n--- Parameters ---"
				"\nimpedance: {:f}"
				"\ninductance: {:f}", mXpd, mLpd);
}

void DP::Ph1::SynchronGeneratorTrStab::setInitialValues(Complex elecPower, Real mechPower) {
	mInitElecPower = elecPower;
	mInitMechPower = mechPower;
}

void DP::Ph1::SynchronGeneratorTrStab::initializeFromNodesAndTerminals(Real frequency) {

	// Initialize omega mech with nominal system frequency
	mOmMech = mNomOmega;

	// Static calculation based on load flow
	mIntfVoltage(0,0) = initialSingleVoltage(0);
	mInitElecPower = (mInitElecPower == Complex(0,0))
		? -terminal(0)->singlePower()
		: mInitElecPower;
	mInitMechPower = (mInitElecPower == Complex(0,0))
		? mInitElecPower.real()
		: mInitMechPower;
	mIntfCurrent(0,0) = std::conj( mInitElecPower / mIntfVoltage(0,0) );
	mImpedance = Complex(0, mXpd);

	// Calculate emf behind reactance
	mEp = mIntfVoltage(0,0) + mImpedance * mIntfCurrent(0,0);
	// The absolute value of Ep is constant, only delta_p changes every step
	mEp_abs = Math::abs(mEp);
	mDelta_p = Math::phase(mEp);
	// Update active electrical power that is compared with the mechanical power
	mElecActivePower = ( (mEp - mIntfVoltage(0,0)) / mImpedance *  mIntfVoltage(0,0) ).real();
	// Start in steady state so that electrical and mech. power are the same
	mMechPower = mElecActivePower;

	// Initialize node between X'd and Ep
	mVirtualNodes[0]->setInitialVoltage(mEp);

	// Create sub voltage source for emf
	mSubVoltageSource = DP::Ph1::VoltageSource::make(mName + "_src", mLogLevel);
	mSubVoltageSource->setSubsystem(mSubsystem);
	mSubVoltageSource->setParameters(mEp);
	mSubVoltageSource->connect({SimNode::GND, mVirtualNodes[0]});
	mSubVoltageSource->setVirtualNodeAt(mVirtualNodes[1], 0);
	mSubVoltageSource->initialize(mFrequencies);
	mSubVoltageSource->initializeFromNodesAndTerminals(frequency);

	// Create sub inductor as Xpd
	mSubInductor = DP::Ph1::Inductor::make(mName + "_ind", mLogLevel);
	mSubInductor->setSubsystem(mSubsystem);
	mSubInductor->setParameters(mLpd);
	mSubInductor->connect({terminal(0)->node(), mVirtualNodes[0]});
	mSubInductor->initialize(mFrequencies);
	mSubInductor->initializeFromNodesAndTerminals(frequency);

	mSLog->info("\n--- Initialize according to powerflow ---"
				"\nTerminal 0 voltage: {:e}<{:e}"
				"\nVoltage behind reactance: {:e}<{:e}"
				"\ninitial electrical power: {:e}+j{:e}"
				"\nactive electrical power: {:e}"
				"\nmechanical power: {:e}"
				"\n--- End of powerflow initialization ---",
				Math::abs(mIntfVoltage(0,0)), Math::phaseDeg(mIntfVoltage(0,0)),
				Math::abs(mEp), Math::phaseDeg(mEp),
				mInitElecPower.real(), mInitElecPower.imag(),
				mElecActivePower, mMechPower);
}

void DP::Ph1::SynchronGeneratorTrStab::step(Real time) {
	// #### Calculations on input of time step k ####
	// Update electrical power
	mElecActivePower = ( (mEp - mIntfVoltage(0,0)) / mImpedance *  mIntfVoltage(0,0) ).real();
	// For infinite power bus
	// mElecActivePower = Math::abs(mEp) * Math::abs(mIntfVoltage(0,0)) / mXpd * sin(mDelta_p);

	// #### Calculate state for time step k+1 ####
	// semi-implicit Euler or symplectic Euler method for mechanical equations
	Real dOmMech = mNomOmega / (2.*mInertia * mNomPower) * (mMechPower - mElecActivePower);
	if (mBehaviour == Behaviour::Simulation)
		mOmMech = mOmMech + mTimeStep * dOmMech;
	Real dDelta_p = mOmMech - mNomOmega;
	if (mBehaviour == Behaviour::Simulation)
		mDelta_p = mDelta_p + mTimeStep * dDelta_p;
	// Update emf - only phase changes
	if (mBehaviour == Behaviour::Simulation)
		mEp = Complex(mEp_abs * cos(mDelta_p), mEp_abs * sin(mDelta_p));

	mStates << Math::abs(mEp), Math::phaseDeg(mEp), mElecActivePower, mMechPower,
		mDelta_p, mOmMech, dOmMech, dDelta_p, mIntfVoltage(0,0).real(), mIntfVoltage(0,0).imag();
	SPDLOG_LOGGER_DEBUG(mSLog, "\nStates, time {:f}: \n{:s}", time, Logger::matrixToString(mStates));
}

void DP::Ph1::SynchronGeneratorTrStab::mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector) {
	MNAInterface::mnaInitialize(omega, timeStep);
	updateMatrixNodeIndices();

	mSubVoltageSource->mnaInitialize(omega, timeStep, leftVector);
	mSubInductor->mnaInitialize(omega, timeStep, leftVector);
	mTimeStep = timeStep;
	mRightVector = Matrix::Zero(leftVector->get().rows(), 1);
	for (auto task : mSubVoltageSource->mnaTasks()) {
		mMnaTasks.push_back(task);
	}
	for (auto task : mSubInductor->mnaTasks()) {
		mMnaTasks.push_back(task);
	}
	mMnaTasks.push_back(std::make_shared<MnaPreStep>(*this));
	mMnaTasks.push_back(std::make_shared<AddBStep>(*this));
	mMnaTasks.push_back(std::make_shared<MnaPostStep>(*this, leftVector));
}

void DP::Ph1::SynchronGeneratorTrStab::mnaApplySystemMatrixStamp(Matrix& systemMatrix) {
	mSubVoltageSource->mnaApplySystemMatrixStamp(systemMatrix);
	mSubInductor->mnaApplySystemMatrixStamp(systemMatrix);
}

void DP::Ph1::SynchronGeneratorTrStab::mnaApplyRightSideVectorStamp(Matrix& rightVector) {
	mSubVoltageSource->mnaApplyRightSideVectorStamp(rightVector);
	mSubInductor->mnaApplyRightSideVectorStamp(rightVector);
}

void DP::Ph1::SynchronGeneratorTrStab::MnaPreStep::execute(Real time, Int timeStepCount) {
	mGenerator.step(time);
	mGenerator.mSubVoltageSource->attribute<Complex>("V_ref")->set(mGenerator.mEp);
}

void DP::Ph1::SynchronGeneratorTrStab::AddBStep::execute(Real time, Int timeStepCount) {
	mGenerator.mRightVector =
		mGenerator.mSubInductor->attribute<Matrix>("right_vector")->get()
		+ mGenerator.mSubVoltageSource->attribute<Matrix>("right_vector")->get();
}

void DP::Ph1::SynchronGeneratorTrStab::MnaPostStep::execute(Real time, Int timeStepCount) {
	// TODO current update?
	mGenerator.mnaUpdateVoltage(*mLeftVector);
}

void DP::Ph1::SynchronGeneratorTrStab::mnaUpdateVoltage(const Matrix& leftVector) {
	SPDLOG_LOGGER_DEBUG(mSLog, "Read voltage from {:d}", matrixNodeIndex(0));
	mIntfVoltage(0,0) = Math::complexFromVectorElement(leftVector, matrixNodeIndex(0));
}
