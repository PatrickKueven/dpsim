/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#pragma once

#include <cps/SimPowerComp.h>
#include <cps/Solver/MNAInterface.h>

namespace CPS {
	namespace SP {
		namespace Ph3 {
			class ControlledVoltageSource :
				public MNAInterface,
				public SimPowerComp<Complex>,
				public SharedFactory<ControlledVoltageSource> {
			protected:
				void updateVoltage(Real time);

			public:
				/// Defines UID, name and logging level
				ControlledVoltageSource(String uid, String name, Logger::Level logLevel = Logger::Level::off);
				///
				ControlledVoltageSource(String name, Logger::Level logLevel = Logger::Level::off)
					: ControlledVoltageSource(name, name, logLevel) { }

				void setParameters(MatrixComp voltageRefABC);

				SimPowerComp<Complex>::Ptr clone(String name);
				// #### General ####
				/// Initializes component from power flow data
				void initializeFromNodesAndTerminals(Real frequency) { }

				// #### MNA section ####
				/// Initializes internal variables of the component
				void mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector);
				/// Stamps system matrix
				void mnaApplySystemMatrixStamp(Matrix& systemMatrix);
				/// Stamps right side (source) vector
				void mnaApplyRightSideVectorStamp(Matrix& rightVector);
				/// Returns current through the component
				void mnaUpdateCurrent(const Matrix& leftVector);

				class MnaPreStep : public CPS::Task {
				public:
					MnaPreStep(ControlledVoltageSource& ControlledVoltageSource) :
						Task(ControlledVoltageSource.mName + ".MnaPreStep", ControlledVoltageSource.mSubsystem), mControlledVoltageSource(ControlledVoltageSource) {
						mAttributeDependencies.push_back(ControlledVoltageSource.attribute("v_intf"));
						mModifiedAttributes.push_back(mControlledVoltageSource.attribute("right_vector"));
					}

					void execute(Real time, Int timeStepCount);

				private:
					ControlledVoltageSource& mControlledVoltageSource;
				};

				class MnaPostStep : public CPS::Task {
				public:
					MnaPostStep(ControlledVoltageSource& ControlledVoltageSource, Attribute<Matrix>::Ptr leftVector) :
						Task(ControlledVoltageSource.mName + ".MnaPostStep", ControlledVoltageSource.mSubsystem), mControlledVoltageSource(ControlledVoltageSource), mLeftVector(leftVector)
					{
						mAttributeDependencies.push_back(mLeftVector);
						mModifiedAttributes.push_back(mControlledVoltageSource.attribute("i_intf"));
					}

					void execute(Real time, Int timeStepCount);

				private:
					ControlledVoltageSource& mControlledVoltageSource;
					Attribute<Matrix>::Ptr mLeftVector;
				};
			};
		}
	}
}

