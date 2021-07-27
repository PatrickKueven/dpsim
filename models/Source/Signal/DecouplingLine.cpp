/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include "cps/Definitions.h"
#include <cps/Signal/DecouplingLine.h>

using namespace CPS;
using namespace CPS::DP::Ph1;
using namespace CPS::Signal;

DecouplingLine::DecouplingLine(String name, SimNode<Complex>::Ptr node1, SimNode<Complex>::Ptr node2,
	Real resistance, Real inductance, Real capacitance, Logger::Level logLevel) :
	SimSignalComp(name, name, logLevel),
	mResistance(resistance), mInductance(inductance), mCapacitance(capacitance),
	mNode1(node1), mNode2(node2) {

	mSplit = false;

	addAttribute<Matrix>("states", &mStates);
	addAttribute<Complex>("i_src1", &mSrcCur1Ref, Flags::read);
	addAttribute<Complex>("i_src2", &mSrcCur2Ref, Flags::read);

	mSurgeImpedance = sqrt(inductance / capacitance);
	mDelay = sqrt(inductance * capacitance);
	mSLog->info("surge impedance: {}", mSurgeImpedance);
	mSLog->info("delay: {}", mDelay);

	mRes1 = Resistor::make(name + "_r1", logLevel);
	mRes1->setParameters(mSurgeImpedance + resistance / 4);
	mRes1->connect({node1, SimNode<Complex>::GND});
	mRes2 = Resistor::make(name + "_r2", logLevel);
	mRes2->setParameters(mSurgeImpedance + resistance / 4);
	mRes2->connect({node2, SimNode<Complex>::GND});

	mSrc1 = CurrentSource::make(name + "_i1", logLevel);
	mSrc1->setParameters(0);
	mSrc1->connect({node1, SimNode<Complex>::GND});
	mSrcCur1 = mSrc1->attributeComplex("I_ref");
	mSrc2 = CurrentSource::make(name + "_i2", logLevel);
	mSrc2->setParameters(0);
	mSrc2->connect({node2, SimNode<Complex>::GND});
	mSrcCur2 = mSrc2->attributeComplex("I_ref");
}

DecouplingLine::DecouplingLine(String name, SimNode<Complex>::Ptr node, Real resistance,
	Real inductance, Real capacitance, Logger::Level logLevel) :
	SimSignalComp(name, name, logLevel),
	mResistance(resistance), mInductance(inductance), mCapacitance(capacitance),
	mNode1(node) {

	mSplit = true;

	addAttribute<Matrix>("states", &mStates);
        addAttribute<Complex>("i_src", &mSrcCur1Ref, Flags::read);

	mSurgeImpedance = sqrt(inductance / capacitance);
        mDelay = sqrt(inductance * capacitance);
        mSLog->info("surge impedance: {}", mSurgeImpedance);
        mSLog->info("delay: {}", mDelay);

	mRes1 = Resistor::make(name + "_r", logLevel);
        mRes1->setParameters(mSurgeImpedance + resistance / 4);
        mRes1->connect({node, SimNode<Complex>::GND});

	mSrc1 = CurrentSource::make(name + "_i", logLevel);
        mSrc1->setParameters(0);
        mSrc1->connect({node, SimNode<Complex>::GND});
        mSrcCur1 = mSrc1->attributeComplex("I_ref");
}

DecouplingLine::DecouplingLine(String name, Logger::Level logLevel) :
	SimSignalComp(name, name, logLevel) {

	mSplit = false;

	addAttribute<Matrix>("states", &mStates);
	addAttribute<Complex>("i_src1", &mSrcCur1Ref, Flags::read);
	addAttribute<Complex>("i_src2", &mSrcCur2Ref, Flags::read);

	mRes1 = Resistor::make(name + "_r1", logLevel);
        mRes2 = Resistor::make(name + "_r2", logLevel);
       	mSrc1 = CurrentSource::make(name + "_i1", logLevel);
        mSrc2 = CurrentSource::make(name + "_i2", logLevel);

       	mSrcCur1 = mSrc1->attributeComplex("I_ref");
        mSrcCur2 = mSrc2->attributeComplex("I_ref");
}

void DecouplingLine::setOtherEndOfDecouplingLine(Ptr otherEndOfDecouplingLine) {
	mOtherEndOfDecouplingLine = otherEndOfDecouplingLine;
}

void DecouplingLine::setParameters(SimNode<Complex>::Ptr node1, SimNode<Complex>::Ptr node2,
	Real resistance, Real inductance, Real capacitance) {

	mSplit = false;

	mResistance = resistance;
	mInductance = inductance;
	mCapacitance = capacitance;
	mNode1 = node1;
	mNode2 = node2;

	mSurgeImpedance = sqrt(inductance / capacitance);
	mDelay = sqrt(inductance * capacitance);
	mSLog->info("surge impedance: {}", mSurgeImpedance);
	mSLog->info("delay: {}", mDelay);

	mRes1->setParameters(mSurgeImpedance + resistance / 4);
	mRes1->connect({node1, SimNode<Complex>::GND});
	mRes2->setParameters(mSurgeImpedance + resistance / 4);
	mRes2->connect({node2, SimNode<Complex>::GND});
	mSrc1->setParameters(0);
	mSrc1->connect({node1, SimNode<Complex>::GND});
	mSrc2->setParameters(0);
	mSrc2->connect({node2, SimNode<Complex>::GND});
}

void DecouplingLine::initialize(Real omega, Real timeStep) {
        if (mDelay < timeStep)
                throw SystemError("Timestep too large for decoupling");

        if (mNode1 == nullptr || (!mSplit && mNode2 == nullptr))
                throw SystemError("nodes not initialized!");

        mBufSize = static_cast<UInt>(ceil(mDelay / timeStep));
        mAlpha = 1 - (mBufSize - mDelay / timeStep);
        mSLog->info("bufsize {} alpha {}", mBufSize, mAlpha);

        Complex volt1 = mNode1->initialSingleVoltage();
        Complex volt2 = mSplit ? mNode1->initialSingleVoltage() : mNode2->initialSingleVoltage();
        // TODO different initialization for lumped resistance?
        Complex initAdmittance = 1. / Complex(mResistance, omega * mInductance) + Complex(0, omega * mCapacitance / 2);
        Complex cur1 = volt1 * initAdmittance - volt2 / Complex(mResistance, omega * mInductance);
        Complex cur2 = volt2 * initAdmittance - volt1 / Complex(mResistance, omega * mInductance);
        mSLog->info("initial voltages: v_k {} v_m {}", volt1, volt2);
        mSLog->info("initial currents: i_km {} i_mk {}", cur1, cur2);

        // Resize ring buffers and initialize
        mVolt1.resize(mBufSize, volt1);
        mVolt2.resize(mBufSize, volt2);
        mCur1.resize(mBufSize, cur1);
        mCur2.resize(mBufSize, cur2);
}

Complex DecouplingLine::interpolate(std::vector<Complex>& data) {
	// linear interpolation of the nearest values
	Complex c1 = data[mBufIdx];
	Complex c2 = mBufIdx == mBufSize-1 ? data[0] : data[mBufIdx+1];
	return mAlpha * c1 + (1-mAlpha) * c2;
}

void DecouplingLine::step(Real time, Int timeStepCount) {
	Complex volt1 = interpolate(mVolt1);
	Complex volt2 = interpolate(mVolt2);
	Complex cur1 = interpolate(mCur1);
	Complex cur2 = interpolate(mCur2);

	if (timeStepCount == 0) {
		// bit of a hack for proper initialization
		mSrcCur1Ref = cur1 - volt1 / (mSurgeImpedance + mResistance / 4);
		if (!mSplit)
			mSrcCur2Ref = cur2 - volt2 / (mSurgeImpedance + mResistance / 4);
	} else {
		// Update currents
		Real denom = (mSurgeImpedance + mResistance/4) * (mSurgeImpedance + mResistance/4);
		mSrcCur1Ref = -mSurgeImpedance / denom * (volt2 + (mSurgeImpedance - mResistance/4) * cur2)
			- mResistance/4 / denom * (volt1 + (mSurgeImpedance - mResistance/4) * cur1);
                mSrcCur1Ref = mSrcCur1Ref * Complex(cos(-2.*PI*50*mDelay),sin(-2.*PI*50*mDelay));
		if (!mSplit) {
			mSrcCur2Ref = -mSurgeImpedance / denom * (volt1 + (mSurgeImpedance - mResistance/4) * cur1)
				- mResistance/4 / denom * (volt2 + (mSurgeImpedance - mResistance/4) * cur2);
			mSrcCur2Ref = mSrcCur2Ref * Complex(cos(-2.*PI*50*mDelay),sin(-2.*PI*50*mDelay));
		}
	}
	mSrcCur1->set(mSrcCur1Ref);
	if (!mSplit)
		mSrcCur2->set(mSrcCur2Ref);
}

void DecouplingLine::PreStep::execute(Real time, Int timeStepCount) {
	mLine.step(time, timeStepCount);
}

void DecouplingLine::postStep() {
	// Update ringbuffers with new values
	mVolt1[mBufIdx] = -mRes1->intfVoltage()(0, 0);
	mCur1[mBufIdx] = -mRes1->intfCurrent()(0, 0) + mSrcCur1->get();
	if (!mSplit) {
		mVolt2[mBufIdx] = -mRes2->intfVoltage()(0, 0);
		mCur2[mBufIdx] = -mRes2->intfCurrent()(0, 0) + mSrcCur2->get();
	}

	mBufIdx++;
	if (mBufIdx == mBufSize)
		mBufIdx = 0;
}

void DecouplingLine::PostStep::execute(Real time, Int timeStepCount) {
	mLine.postStep();
}

Task::List DecouplingLine::getTasks() {
	return Task::List({std::make_shared<PreStep>(*this), std::make_shared<PostStep>(*this)});
}

IdentifiedObject::List DecouplingLine::getLineComponents() {
	if (!mSplit)
		return IdentifiedObject::List({mRes1, mRes2, mSrc1, mSrc2});
	else
		return IdentifiedObject::List({mRes1, mSrc1});
}

void DecouplingLine::getLastRingbufferValues(char* data) {
	UInt lastBufIdx = 0;
        if (mBufIdx == 0)
                lastBufIdx = mBufSize - 1;
        else
                lastBufIdx = mBufIdx - 1;

	std::memcpy(data, &lastBufIdx, sizeof(UInt));
	data += sizeof(UInt);
	std::memcpy(data, &(mVolt1[lastBufIdx]), sizeof(Complex));
        data += sizeof(Complex);
	std::memcpy(data, &(mCur1[lastBufIdx]), sizeof(Complex));
        data += sizeof(Complex);
	if (!mSplit) {
		std::memcpy(data, &(mVolt2[lastBufIdx]), sizeof(Complex));
	        data += sizeof(Complex);
		std::memcpy(data, &(mCur2[lastBufIdx]), sizeof(Complex));
	        data += sizeof(Complex);
	}
}

void DecouplingLine::setLastRingbufferValues(char* data) {
	UInt lastBufIdx = 0;
	std::memcpy(&lastBufIdx, data, sizeof(UInt));
	data += sizeof(UInt);
	if (!mSplit) {
		std::memcpy(&(mVolt1[lastBufIdx]), data, sizeof(Complex));
        	data += sizeof(Complex);
		std::memcpy(&(mCur1[lastBufIdx]), data, sizeof(Complex));
        	data += sizeof(Complex);
	}
	std::memcpy(&(mVolt2[lastBufIdx]), data, sizeof(Complex));
       	data += sizeof(Complex);
	std::memcpy(&(mCur2[lastBufIdx]), data, sizeof(Complex));
       	data += sizeof(Complex);

	mBufIdx = lastBufIdx + 1;
        if (mBufIdx == mBufSize)
                mBufIdx = 0;
}

std::shared_ptr<DP::SimNode> DecouplingLine::getFirstNode() {
	return mNode1;
}

IdentifiedObject::List DecouplingLine::splitLine() {
	DecouplingLine::Ptr line1 = DecouplingLine::make(mName, mNode1, mResistance, mInductance, mCapacitance, mLogLevel);
	DecouplingLine::Ptr line2 = DecouplingLine::make(mName, mNode2, mResistance, mInductance, mCapacitance, mLogLevel);
	line1->setOtherEndOfDecouplingLine(line2);
	line2->setOtherEndOfDecouplingLine(line1);
	return IdentifiedObject::List({line1, line2});
}
