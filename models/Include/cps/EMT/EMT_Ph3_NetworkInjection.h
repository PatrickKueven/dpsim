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
#include <cps/EMT/EMT_Ph3_VoltageSource.h>

namespace CPS {
	namespace EMT {
		namespace Ph3 {
			/// \brief Network injection model
			///
			/// This model represents network injections by an ideal voltage source.
			class NetworkInjection :
				public SimPowerComp<Real>,
				public MNAInterface,
				public SharedFactory<NetworkInjection> {
			private:
				// ### Electrical Subcomponents ###
				/// Voltage source
				std::shared_ptr<EMT::Ph3::VoltageSource> mSubVoltageSource;

				// #### solver ####
				/// Vector to collect subcomponent right vector stamps
				std::vector<const Matrix*> mRightVectorStamps;
			public:
				/// Defines UID, name, component parameters and logging level
				NetworkInjection(String uid, String name, Logger::Level loglevel = Logger::Level::off);
				/// Defines UID, name, component parameters and logging level
				NetworkInjection(String name, Logger::Level logLevel = Logger::Level::off)
					: NetworkInjection(name, name, logLevel) { }
				/// Defines name, component parameters and logging level
				NetworkInjection(String name,
					Complex voltage, Logger::Level logLevel = Logger::Level::off);
				///
				SimPowerComp<Real>::Ptr clone(String name);

				// #### General ####
				/// Initializes component from power flow data
				void initializeFromNodesAndTerminals(Real frequency);
				/// Setter for reference voltage parameters
				void setParameters(MatrixComp voltageRef, Real srcFreq = -1);

				// #### MNA Section ####
				/// Initializes internal variables of the component
				void mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector);
				/// Stamps system matrix
				void mnaApplySystemMatrixStamp(Matrix& systemMatrix);
				/// Stamps right side (source) vector
				void mnaApplyRightSideVectorStamp(Matrix& rightVector);
				/// Returns current through the component
				void mnaUpdateCurrent(const Matrix& leftVector);
				/// Updates voltage across component
				void mnaUpdateVoltage(const Matrix& leftVector);
				/// MNA pre step operations
				void mnaPreStep(Real time, Int timeStepCount);
				/// MNA post step operations
				void mnaPostStep(Real time, Int timeStepCount, Attribute<Matrix>::Ptr &leftVector);
				/// Add MNA pre step dependencies
				void mnaAddPreStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes);
				/// Add MNA post step dependencies
				void mnaAddPostStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes, Attribute<Matrix>::Ptr &leftVector);

				class MnaPreStep : public CPS::Task {
				public:
					MnaPreStep(NetworkInjection& networkInjection) :
						Task(networkInjection.mName + ".MnaPreStep", networkInjection.mSubsystem), mNetworkInjection(networkInjection) {
							mNetworkInjection.mnaAddPreStepDependencies(mPrevStepDependencies, mAttributeDependencies, mModifiedAttributes);
					}
					void execute(Real time, Int timeStepCount) { mNetworkInjection.mnaPreStep(time, timeStepCount); };

				private:
					NetworkInjection& mNetworkInjection;
				};

				class MnaPostStep : public CPS::Task {
				public:
					MnaPostStep(NetworkInjection& networkInjection, Attribute<Matrix>::Ptr leftVector) :
						Task(networkInjection.mName + ".MnaPostStep", networkInjection.mSubsystem), mNetworkInjection(networkInjection), mLeftVector(leftVector) {
						mNetworkInjection.mnaAddPostStepDependencies(mPrevStepDependencies, mAttributeDependencies, mModifiedAttributes, mLeftVector);
					}
					void execute(Real time, Int timeStepCount) { mNetworkInjection.mnaPostStep(time, timeStepCount, mLeftVector); };

				private:
					NetworkInjection& mNetworkInjection;
					Attribute<Matrix>::Ptr mLeftVector;
				};
			};
		}
	}
}
