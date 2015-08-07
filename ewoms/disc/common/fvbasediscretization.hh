// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright (C) 2008-2014 by Andreas Lauser
  Copyright (C) 2009-2011 by Bernd Flemisch

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 *
 * \copydoc Ewoms::FvBaseDiscretization
 */
#ifndef EWOMS_FV_BASE_DISCRETIZATION_HH
#define EWOMS_FV_BASE_DISCRETIZATION_HH

#include <opm/material/localad/Math.hpp>

#include "fvbaseproperties.hh"
#include "fvbaselinearizer.hh"
#include "fvbasefdlocallinearizer.hh"
#include "fvbaseadlocallinearizer.hh"
#include "fvbaselocalresidual.hh"
#include "fvbaseelementcontext.hh"
#include "fvbaseboundarycontext.hh"
#include "fvbaseconstraintscontext.hh"
#include "fvbaseconstraints.hh"
#include "fvbasediscretization.hh"
#include "fvbasegradientcalculator.hh"
#include "fvbasenewtonmethod.hh"
#include "fvbaseprimaryvariables.hh"
#include "fvbaseintensivequantities.hh"
#include "fvbaseextensivequantities.hh"

#include <ewoms/parallel/gridcommhandles.hh>
#include <ewoms/parallel/threadmanager.hh>
#include <ewoms/linear/nullborderlistmanager.hh>
#include <ewoms/common/simulator.hh>
#include <ewoms/aux/baseauxiliarymodule.hh>

#include <opm/material/common/MathToolbox.hpp>
#include <opm/material/common/Exceptions.hpp>

#include <dune/common/fvector.hh>
#include <dune/common/fmatrix.hh>
#include <dune/istl/bvector.hh>

#if HAVE_DUNE_FEM
#include <dune/fem/space/common/adaptmanager.hh>
#include <dune/fem/space/common/restrictprolongtuple.hh>
#include <dune/fem/function/blockvectorfunction.hh>
#include <dune/fem/misc/capabilities.hh>
#endif

#include <limits>
#include <list>
#include <sstream>
#include <string>
#include <vector>

namespace Ewoms {
template<class TypeTag>
class FvBaseDiscretization;

namespace Properties {
//! Set the default type for the time manager
SET_TYPE_PROP(FvBaseDiscretization, Simulator, Ewoms::Simulator<TypeTag>);

//! Mapper for the grid view's vertices.
SET_TYPE_PROP(FvBaseDiscretization, VertexMapper,
              Dune::MultipleCodimMultipleGeomTypeMapper<typename GET_PROP_TYPE(TypeTag, GridView),
                                                        Dune::MCMGVertexLayout>);

//! Mapper for the grid view's elements.
SET_TYPE_PROP(FvBaseDiscretization, ElementMapper,
              Dune::MultipleCodimMultipleGeomTypeMapper<typename GET_PROP_TYPE(TypeTag, GridView),
                                                        Dune::MCMGElementLayout>);

//! marks the border indices (required for the algebraic overlap stuff)
SET_PROP(FvBaseDiscretization, BorderListCreator)
{
    typedef typename GET_PROP_TYPE(TypeTag, DofMapper) DofMapper;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
public:
    typedef Ewoms::Linear::NullBorderListCreator<GridView, DofMapper> type;
};

SET_TYPE_PROP(FvBaseDiscretization, DiscLocalResidual, Ewoms::FvBaseLocalResidual<TypeTag>);

SET_TYPE_PROP(FvBaseDiscretization, DiscIntensiveQuantities, Ewoms::FvBaseIntensiveQuantities<TypeTag>);
SET_TYPE_PROP(FvBaseDiscretization, DiscExtensiveQuantities, Ewoms::FvBaseExtensiveQuantities<TypeTag>);

//! Calculates the gradient of any quantity given the index of a flux approximation point
SET_TYPE_PROP(FvBaseDiscretization, GradientCalculator, Ewoms::FvBaseGradientCalculator<TypeTag>);

//! Set the type of a global jacobian matrix from the solution types
SET_PROP(FvBaseDiscretization, JacobianMatrix)
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    enum { numEq = GET_PROP_VALUE(TypeTag, NumEq) };
    typedef typename Dune::FieldMatrix<Scalar, numEq, numEq> MatrixBlock;
public:
    typedef typename Dune::BCRSMatrix<MatrixBlock> type;
};

//! The maximum allowed number of timestep divisions for the
//! Newton solver
SET_INT_PROP(FvBaseDiscretization, MaxTimeStepDivisions, 10);

/*!
 * \brief A vector of quanties, each for one equation.
 */
SET_TYPE_PROP(FvBaseDiscretization, EqVector,
              Dune::FieldVector<typename GET_PROP_TYPE(TypeTag, Scalar),
                                GET_PROP_VALUE(TypeTag, NumEq)>);

/*!
 * \brief A vector for mass/energy rates.
 *
 * E.g. Neumann fluxes or source terms
 */
SET_TYPE_PROP(FvBaseDiscretization, RateVector,
              typename GET_PROP_TYPE(TypeTag, EqVector));

/*!
 * \brief Type of object for specifying boundary conditions.
 */
SET_TYPE_PROP(FvBaseDiscretization, BoundaryRateVector,
              typename GET_PROP_TYPE(TypeTag, RateVector));

/*!
 * \brief The class which represents constraints.
 */
SET_TYPE_PROP(FvBaseDiscretization, Constraints, Ewoms::FvBaseConstraints<TypeTag>);

/*!
 * \brief The type for storing a residual for an element.
 */
SET_TYPE_PROP(FvBaseDiscretization, ElementEqVector,
              Dune::BlockVector<typename GET_PROP_TYPE(TypeTag, EqVector)>);

/*!
 * \brief The type for storing a residual for the whole grid.
 */
SET_TYPE_PROP(FvBaseDiscretization, GlobalEqVector,
              Dune::BlockVector<typename GET_PROP_TYPE(TypeTag, EqVector)>);

/*!
 * \brief An object representing a local set of primary variables.
 */
SET_TYPE_PROP(FvBaseDiscretization, PrimaryVariables, Ewoms::FvBasePrimaryVariables<TypeTag>);

/*!
 * \brief The type of a solution for the whole grid at a fixed time.
 */
SET_TYPE_PROP(FvBaseDiscretization, SolutionVector,
              Dune::BlockVector<typename GET_PROP_TYPE(TypeTag, PrimaryVariables)>);

/*!
 * \brief The class representing intensive quantities.
 *
 * This should almost certainly be overloaded by the model...
 */
SET_TYPE_PROP(FvBaseDiscretization, IntensiveQuantities, Ewoms::FvBaseIntensiveQuantities<TypeTag>);

/*!
 * \brief The element context
 */
SET_TYPE_PROP(FvBaseDiscretization, ElementContext, Ewoms::FvBaseElementContext<TypeTag>);
SET_TYPE_PROP(FvBaseDiscretization, BoundaryContext, Ewoms::FvBaseBoundaryContext<TypeTag>);
SET_TYPE_PROP(FvBaseDiscretization, ConstraintsContext, Ewoms::FvBaseConstraintsContext<TypeTag>);

/*!
 * \brief The OpenMP threads manager
 */
SET_TYPE_PROP(FvBaseDiscretization, ThreadManager, Ewoms::ThreadManager<TypeTag>);
SET_INT_PROP(FvBaseDiscretization, ThreadsPerProcess, 1);

/*!
 * \brief Linearizer for the global system of equations.
 */
SET_TYPE_PROP(FvBaseDiscretization, Linearizer, Ewoms::FvBaseLinearizer<TypeTag>);

//! use an unlimited time step size by default
#if 0
// requires GCC 4.6 or later to be able call the constexpr function here
SET_SCALAR_PROP(FvBaseDiscretization, MaxTimeStepSize, std::numeric_limits<Scalar>::infinity());
#else
SET_SCALAR_PROP(FvBaseDiscretization, MaxTimeStepSize, 1e100);
#endif
//! By default, accept any time step larger than zero
SET_SCALAR_PROP(FvBaseDiscretization, MinTimeStepSize, 0.0);

//! Disable grid adaptation by default
SET_BOOL_PROP(FvBaseDiscretization, EnableGridAdaptation, false);

//! Enable the VTK output by default
SET_BOOL_PROP(FvBaseDiscretization, EnableVtkOutput, true);

//! Set the format of the VTK output to ASCII by default
SET_INT_PROP(FvBaseDiscretization, VtkOutputFormat, Dune::VTK::ascii);

// disable linearization recycling by default
SET_BOOL_PROP(FvBaseDiscretization, EnableLinearizationRecycling, false);

// disable partial relinearization by default
SET_BOOL_PROP(FvBaseDiscretization, EnablePartialRelinearization, false);

// disable constraints by default
SET_BOOL_PROP(FvBaseDiscretization, EnableConstraints, false);

// by default, disable the intensive quantity cache. If the intensive quantities are
// relatively cheap to calculate, the cache basically does not yield any performance
// impact because of the intensive quantity cache will cause additional pressure on the
// CPU caches...
SET_BOOL_PROP(FvBaseDiscretization, EnableIntensiveQuantityCache, false);

// do not use thermodynamic hints by default. If you enable this, make sure to also
// enable the intensive quantity cache above to avoid getting an exception...
SET_BOOL_PROP(FvBaseDiscretization, EnableThermodynamicHints, false);

// if the deflection of the newton method is large, we do not need to solve the linear
// approximation accurately. Assuming that the value for the current solution is quite
// close to the final value, a reduction of 3 orders of magnitude in the defect should be
// sufficient...
SET_SCALAR_PROP(FvBaseDiscretization, LinearSolverTolerance, 1e-3);

//! Set the history size of the time discretization to 2 (for implicit euler)
SET_INT_PROP(FvBaseDiscretization, TimeDiscHistorySize, 2);

//! Most models don't need the gradients at the center of the SCVs, so
//! we disable them by default.
SET_BOOL_PROP(FvBaseDiscretization, RequireScvCenterGradients, false);
} // namespace Properties

/*!
 * \ingroup FiniteVolumeDiscretizations
 *
 * \brief The base class for the finite volume discretization schemes.
 */
template<class TypeTag>
class FvBaseDiscretization
{
    typedef typename GET_PROP_TYPE(TypeTag, Model) Implementation;
    typedef typename GET_PROP_TYPE(TypeTag, Discretization) Discretization;
    typedef typename GET_PROP_TYPE(TypeTag, Simulator) Simulator;
    typedef typename GET_PROP_TYPE(TypeTag, Grid) Grid;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, Evaluation) Evaluation;
    typedef typename GET_PROP_TYPE(TypeTag, ElementMapper) ElementMapper;
    typedef typename GET_PROP_TYPE(TypeTag, VertexMapper) VertexMapper;
    typedef typename GET_PROP_TYPE(TypeTag, DofMapper) DofMapper;
    typedef typename GET_PROP_TYPE(TypeTag, SolutionVector) SolutionVector;
    typedef typename GET_PROP_TYPE(TypeTag, GlobalEqVector) GlobalEqVector;
    typedef typename GET_PROP_TYPE(TypeTag, EqVector) EqVector;
    typedef typename GET_PROP_TYPE(TypeTag, RateVector) RateVector;
    typedef typename GET_PROP_TYPE(TypeTag, BoundaryRateVector) BoundaryRateVector;
    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, Linearizer) Linearizer;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;
    typedef typename GET_PROP_TYPE(TypeTag, BoundaryContext) BoundaryContext;
    typedef typename GET_PROP_TYPE(TypeTag, IntensiveQuantities) IntensiveQuantities;
    typedef typename GET_PROP_TYPE(TypeTag, ExtensiveQuantities) ExtensiveQuantities;
    typedef typename GET_PROP_TYPE(TypeTag, GradientCalculator) GradientCalculator;
    typedef typename GET_PROP_TYPE(TypeTag, Stencil) Stencil;
    typedef typename GET_PROP_TYPE(TypeTag, DiscBaseOutputModule) DiscBaseOutputModule;
    typedef typename GET_PROP_TYPE(TypeTag, GridCommHandleFactory) GridCommHandleFactory;
    typedef typename GET_PROP_TYPE(TypeTag, NewtonMethod) NewtonMethod;
    typedef typename GET_PROP_TYPE(TypeTag, ThreadManager) ThreadManager;

    typedef typename GET_PROP_TYPE(TypeTag, LocalLinearizer) LocalLinearizer;
    typedef typename GET_PROP_TYPE(TypeTag, LocalResidual) LocalResidual;

    enum {
        numEq = GET_PROP_VALUE(TypeTag, NumEq),
        historySize = GET_PROP_VALUE(TypeTag, TimeDiscHistorySize),
    };

    typedef std::vector<IntensiveQuantities> IntensiveQuantitiesVector;

    typedef typename GridView::template Codim<0>::Entity Element;
    typedef typename GridView::template Codim<0>::Iterator ElementIterator;

    typedef Opm::MathToolbox<Evaluation> Toolbox;
    typedef Dune::FieldVector<Evaluation, numEq> VectorBlock;
    typedef Dune::FieldVector<Evaluation, numEq> EvalEqVector;
    typedef Dune::BlockVector<VectorBlock> LocalBlockVector;

    class BlockVectorWrapper
    {
    protected:
        SolutionVector blockVector_;
    public:
        BlockVectorWrapper( const std::string& name, const size_t size )
            : blockVector_( size )
        {}

        SolutionVector& blockVector() { return blockVector_; }
        const SolutionVector& blockVector() const { return blockVector_; }
    };

#if HAVE_DUNE_FEM
    typedef typename GET_PROP_TYPE(TypeTag, DiscreteFunctionSpace)    DiscreteFunctionSpace;

    typedef Dune::Fem::ISTLBlockVectorDiscreteFunction<DiscreteFunctionSpace, PrimaryVariables> DiscreteFunction;

    // adaptation classes
    typedef typename GET_PROP_TYPE(TypeTag, Problem)   Problem;
    typedef typename Problem :: RestrictProlongOperator  ProblemRestrictProlongOperator;

    typedef Dune::Fem::RestrictProlongDefault< DiscreteFunction > DiscreteFunctionRestrictProlong;
    typedef Dune::Fem::RestrictProlongTuple< DiscreteFunctionRestrictProlong,  ProblemRestrictProlongOperator > RestrictProlong;
    typedef Dune::Fem::AdaptationManager<Grid, RestrictProlong  > AdaptationManager;
#else
    typedef BlockVectorWrapper  DiscreteFunction;
#endif

    // copying a discretization object is not a good idea
    FvBaseDiscretization(const FvBaseDiscretization &);

public:
    // this constructor required to be explicitly specified because
    // we've defined a constructor above which deletes all implicitly
    // generated constructors in C++.
    FvBaseDiscretization(Simulator &simulator)
        : simulator_(simulator)
        , gridView_(simulator.gridView())
        , newtonMethod_(simulator)
        , localLinearizer_(ThreadManager::maxThreads())
        , linearizer_(new Linearizer())
#if HAVE_DUNE_FEM
        , space_( simulator.gridManager().gridPart() )
#else
        , space_( asImp_().numGridDof() )
#endif
        , enableGridAdaptation_( EWOMS_GET_PARAM(TypeTag, bool, EnableGridAdaptation) )
    {
#if HAVE_DUNE_FEM
        if( enableGridAdaptation_ && ! Dune::Fem::Capabilities::isLocallyAdaptive< Grid >::v )
        {
            std::cerr << "WARNING: adaptation enabled, but chosen Grid is not capable of adaptivity" << std::endl;
            enableGridAdaptation_ = false ;
        }
#else
        if (enableGridAdaptation_)
            OPM_THROW(Opm::NotAvailable,
                      "Grid adaptation currently requires the presence of the dune-fem module");
#endif
        bool isEcfv = std::is_same<Discretization, EcfvDiscretization<TypeTag> >::value;
        if (enableGridAdaptation_ && !isEcfv)
            OPM_THROW(Opm::NotAvailable,
                      "Grid adaptation currently only works for the element-centered finite "
                      "volume discretization (is: " << Dune::className<Discretization>() << ")");

        asImp_().updateBoundary_();

        for (int timeIdx = 0; timeIdx < historySize; ++timeIdx) {
            solution_[ timeIdx ].reset( new DiscreteFunction( "solution", space_ ) );
        }

#if HAVE_DUNE_FEM
        // create adaptation objects
        restrictProlong_.reset(
            new RestrictProlong( DiscreteFunctionRestrictProlong(*(solution_[/*timeIdx=*/ 0] )),
                                 simulator.problem().restrictProlongOperator() ) );
        adaptationManager_.reset( new AdaptationManager( simulator.gridManager().grid(), *restrictProlong_ ) );
#endif
        resizeAndResetIntensiveQuantitiesCache_();
        asImp_().registerOutputModules_();
    }

    ~FvBaseDiscretization()
    {
        // delete all output modules
        auto modIt = outputModules_.begin();
        const auto &modEndIt = outputModules_.end();
        for (; modIt != modEndIt; ++modIt)
            delete *modIt;

        delete linearizer_;
    }

    /*!
     * \brief Register all run-time parameters for the model.
     */
    static void registerParameters()
    {
        Linearizer::registerParameters();
        LocalLinearizer::registerParameters();
        LocalResidual::registerParameters();
        GradientCalculator::registerParameters();
        IntensiveQuantities::registerParameters();
        ExtensiveQuantities::registerParameters();
        NewtonMethod::registerParameters();

        // register runtime parameters of the output modules
        Ewoms::VtkPrimaryVarsModule<TypeTag>::registerParameters();

        EWOMS_REGISTER_PARAM(TypeTag, bool, EnableGridAdaptation, "Enable adaptive grid refinement/coarsening");
        EWOMS_REGISTER_PARAM(TypeTag, bool, EnableVtkOutput, "Global switch for turing on writing VTK files");
        EWOMS_REGISTER_PARAM(TypeTag, bool, EnableThermodynamicHints, "Enable thermodynamic hints");
        EWOMS_REGISTER_PARAM(TypeTag, bool, EnableIntensiveQuantityCache, "Turn on caching of intensive quantities");
    }

    /*!
     * \brief Apply the initial conditions to the model.
     */
    void finishInit()
    {
        // initialize the volume of the finite volumes to zero
        int nDofs = asImp_().numGridDof();
        dofTotalVolume_.resize(nDofs);
        std::fill(dofTotalVolume_.begin(), dofTotalVolume_.end(), 0.0);

        ElementContext elemCtx(simulator_);
        gridTotalVolume_ = 0.0;

        // iterate through the grid and evaluate the initial condition
        ElementIterator elemIt = gridView_.template begin</*codim=*/0>();
        const ElementIterator &elemEndIt = gridView_.template end</*codim=*/0>();
        for (; elemIt != elemEndIt; ++elemIt) {
            const Element& elem = *elemIt;
            const bool isInteriorElement = elem.partitionType() == Dune::InteriorEntity;
            // ignore everything which is not in the interior if the
            // current process' piece of the grid
            if (!isInteriorElement)
                continue;

            // deal with the current element
            elemCtx.updateStencil(elem);
            const auto &stencil = elemCtx.stencil(/*timeIdx=*/0);

            // loop over all element vertices, i.e. sub control volumes
            for (int dofIdx = 0; dofIdx < elemCtx.numPrimaryDof(/*timeIdx=*/0); dofIdx++) {
                // map the local degree of freedom index to the global one
                unsigned globalIdx = elemCtx.globalSpaceIndex(dofIdx, /*timeIdx=*/0);

                Scalar dofVolume = stencil.subControlVolume(dofIdx).volume();
                dofTotalVolume_[globalIdx] += dofVolume;
                if (isInteriorElement)
                    gridTotalVolume_ += dofVolume;
            }
        }

        // determine which DOFs should be considered to lie fully in the interior of the
        // local process grid partition: those which do not have a non-zero volume
        // before taking the peer processes into account...
        isLocalDof_.resize(nDofs);
        for (int dofIdx = 0; dofIdx < nDofs; ++dofIdx)
            isLocalDof_[dofIdx] = (dofTotalVolume_[dofIdx] != 0.0);

        // add the volumes of the DOFs on the process boundaries
        const auto sumHandle =
            GridCommHandleFactory::template sumHandle<Scalar>(dofTotalVolume_,
                                                              asImp_().dofMapper());
        gridView_.communicate(*sumHandle,
                              Dune::Overlap_All_Interface,
                              Dune::ForwardCommunication);

        // sum up the volumes of the grid partitions
        gridTotalVolume_ = gridView_.comm().sum(gridTotalVolume_);

        linearizer_->init(simulator_);
        for (int threadId = 0; threadId < ThreadManager::maxThreads(); ++threadId)
            localLinearizer_[threadId].init(simulator_);

        if (storeIntensiveQuantities_()) {
            // invalidate all cached intensive quantities
            for (int timeIdx = 0; timeIdx < historySize; ++ timeIdx) {
                std::fill(intensiveQuantityCacheUpToDate_[timeIdx].begin(),
                          intensiveQuantityCacheUpToDate_[timeIdx].end(),
                          false);
            }
        }
    }

    /*!
     * \brief Returns whether the grid ought to be adapted to the solution during the simulation.
     */
    bool enableGridAdaptation() const
    { return enableGridAdaptation_; }

    /*!
     * \brief Applies the initial solution for all degrees of freedom to which the model
     *        applies.
     */
    void applyInitialSolution()
    {
        // first set the whole domain to zero
        SolutionVector &uCur = asImp_().solution(/*timeIdx=*/0);
        uCur = Scalar(0.0);

        ElementContext elemCtx(simulator_);

        // iterate through the grid and evaluate the initial condition
        ElementIterator elemIt = gridView_.template begin</*codim=*/0>();
        const ElementIterator &elemEndIt = gridView_.template end</*codim=*/0>();
        for (; elemIt != elemEndIt; ++elemIt) {
            const Element& elem = *elemIt;
            // ignore everything which is not in the interior if the
            // current process' piece of the grid
            if (elem.partitionType() != Dune::InteriorEntity)
                continue;

            // deal with the current element
            elemCtx.updateStencil(elem);

            // loop over all element vertices, i.e. sub control volumes
            for (int dofIdx = 0; dofIdx < elemCtx.numPrimaryDof(/*timeIdx=*/0); dofIdx++)
            {
                // map the local degree of freedom index to the global one
                unsigned globalIdx = elemCtx.globalSpaceIndex(dofIdx, /*timeIdx=*/0);

                // let the problem do the dirty work of nailing down
                // the initial solution.
                simulator_.problem().initial(uCur[globalIdx], elemCtx, dofIdx, /*timeIdx=*/0);
                asImp_().supplementInitialSolution_(uCur[globalIdx], elemCtx, dofIdx, /*timeIdx=*/0);
                uCur[globalIdx].checkDefined();
            }
        }

        // synchronize the ghost DOFs (if necessary)
        asImp_().syncOverlap();

        // also set the solution of the "previous" time steps to the
        // initial solution.
        for (int timeIdx = 1; timeIdx < historySize; ++timeIdx)
            solution(timeIdx) = solution(/*timeIdx=*/0);

        simulator_.problem().initialSolutionApplied();
    }

    /*!
     * \brief Returns the newton method object
     */
    NewtonMethod &newtonMethod()
    { return newtonMethod_; }

    /*!
     * \copydoc newtonMethod()
     */
    const NewtonMethod &newtonMethod() const
    { return newtonMethod_; }

    /*!
     * \brief Return the thermodynamic hint for a entity on the grid at given time.
     *
     * The hint is defined as a IntensiveQuantities object which is supposed to be
     * "close" to the IntensiveQuantities of the current solution. It can be used as a
     * good starting point for non-linear solvers when having to solve non-linear
     * relations while updating the intensive quantities. (This may yield a major
     * performance boost depending on how the physical models require.)
     *
     * \attention If no up-to date intensive quantities are available, or if hints have been
     *            disabled, this method will return 0.
     *
     * \param globalIdx The global space index for the entity where a hint is requested.
     * \param timeIdx The index used by the time discretization.
     */
    const IntensiveQuantities *thermodynamicHint(int globalIdx, int timeIdx) const
    {
        if (!enableThermodynamicHints_())
            return 0;

        if (intensiveQuantityCacheUpToDate_[timeIdx][globalIdx])
            return &intensiveQuantityCache_[timeIdx][globalIdx];

        // use the intensive quantities for the first up-to-date time index as hint
        for (int timeIdx2 = 0; timeIdx2 < historySize; ++timeIdx2)
            if (intensiveQuantityCacheUpToDate_[timeIdx2][globalIdx])
                return &intensiveQuantityCache_[timeIdx2][globalIdx];

        // no suitable up-to-date intensive quantities...
        return 0;
    }

    /*!
     * \brief Return the cached intensive quantities for a entity on the
     *        grid at given time.
     *
     * \attention If no up-to date intensive quantities are available,
     *            this method will return 0.
     *
     * \param globalIdx The global space index for the entity where a
     *                  hint is requested.
     * \param timeIdx The index used by the time discretization.
     */
    const IntensiveQuantities *cachedIntensiveQuantities(int globalIdx, int timeIdx) const
    {
        if (!enableIntensiveQuantitiesCache_() ||
            !intensiveQuantityCacheUpToDate_[timeIdx][globalIdx])
            return 0;

        return &intensiveQuantityCache_[timeIdx][globalIdx];
    }

    /*!
     * \brief Update the intensive quantity cache for a entity on the grid at given time.
     *
     * \param intQuants The IntensiveQuantities object hint for a given degree of freedom.
     * \param globalIdx The global space index for the entity where a
     *                  hint is to be set.
     * \param timeIdx The index used by the time discretization.
     */
    void updateCachedIntensiveQuantities(const IntensiveQuantities &intQuants,
                                         int globalIdx,
                                         int timeIdx) const
    {
        if (!storeIntensiveQuantities_())
            return;

        intensiveQuantityCache_[timeIdx][globalIdx] = intQuants;
        intensiveQuantityCacheUpToDate_[timeIdx][globalIdx] = true;
    }

    /*!
     * \brief Invalidate the cache for a given intensive quantities object.
     *
     * \param globalIdx The global space index for the entity where a
     *                  hint is to be set.
     * \param timeIdx The index used by the time discretization.
     */
    void setIntensiveQuantitiesCacheEntryValidity(int globalIdx,
                                                  int timeIdx,
                                                  bool newValue) const
    {
        if (!storeIntensiveQuantities_())
            return;

        intensiveQuantityCacheUpToDate_[timeIdx][globalIdx] = newValue;
    }

    /*!
     * \brief Move the intensive quantities for a given time index to the back.
     *
     * This method should only be called by the time discretization.
     *
     * \param numSlots The number of time step slots for which the
     *                 hints should be shifted.
     */
    void shiftIntensiveQuantityCache(int numSlots = 1)
    {
        if (!storeIntensiveQuantities_())
            return;

        for (int timeIdx = 0; timeIdx < historySize - numSlots; ++ timeIdx) {
            intensiveQuantityCache_[timeIdx + numSlots] = intensiveQuantityCache_[timeIdx];
            intensiveQuantityCacheUpToDate_[timeIdx + numSlots] = intensiveQuantityCacheUpToDate_[timeIdx];
        }

        // invalidate the cache for the most recent time index
        std::fill(intensiveQuantityCacheUpToDate_[/*timeIdx=*/0].begin(),
                  intensiveQuantityCacheUpToDate_[/*timeIdx=*/0].end(),
                  false);
    }

    /*!
     * \brief Compute the global residual for an arbitrary solution
     *        vector.
     *
     * \param dest Stores the result
     * \param u The solution for which the residual ought to be calculated
     */
    Scalar globalResidual(GlobalEqVector &dest,
                          const SolutionVector &u) const
    {
        SolutionVector tmp(asImp_().solution(/*timeIdx=*/0));
        mutableSolution(/*timeIdx=*/0) = u;
        Scalar res = asImp_().globalResidual(dest);
        mutableSolution(/*timeIdx=*/0) = tmp;
        return res;
    }

    /*!
     * \brief Compute the global residual for the current solution
     *        vector.
     *
     * \param dest Stores the result
     */
    Scalar globalResidual(GlobalEqVector &dest) const
    {
        dest = 0;

        OmpMutex mutex;
        ThreadedEntityIterator<GridView, /*codim=*/0> threadedElemIt(gridView());
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            // Attention: the variables below are thread specific and thus cannot be
            // moved in front of the #pragma!
            int threadId = ThreadManager::threadId();
            ElementContext elemCtx(simulator_);
            ElementIterator elemIt = gridView().template begin</*codim=*/0>();
            LocalBlockVector residual, storageTerm;

            for (threadedElemIt.beginParallel(elemIt);
                 !threadedElemIt.isFinished(elemIt);
                 threadedElemIt.increment(elemIt))
            {
                const Element& elem = *elemIt;
                if (elem.partitionType() != Dune::InteriorEntity)
                    continue;

                elemCtx.updateAll(elem);
                residual.resize(elemCtx.numDof(/*timeIdx=*/0));
                storageTerm.resize(elemCtx.numPrimaryDof(/*timeIdx=*/0));
                asImp_().localResidual(threadId).eval(residual, storageTerm, elemCtx);

                int numPrimaryDof = elemCtx.numPrimaryDof(/*timeIdx=*/0);
                ScopedLock addLock(mutex);
                for (int dofIdx = 0; dofIdx < numPrimaryDof; ++dofIdx) {
                    int globalI = elemCtx.globalSpaceIndex(dofIdx, /*timeIdx=*/0);
                    for (int eqIdx = 0; eqIdx < numEq; ++ eqIdx)
                        dest[globalI][eqIdx] += Toolbox::value(residual[dofIdx][eqIdx]);
                }
                addLock.unlock();
            }
        }

        // add up the residuals on the process borders
        const auto sumHandle =
            GridCommHandleFactory::template sumHandle<EqVector>(dest, asImp_().dofMapper());
        gridView_.communicate(*sumHandle,
                              Dune::InteriorBorder_InteriorBorder_Interface,
                              Dune::ForwardCommunication);

        // calculate the square norm of the residual. this is not
        // entirely correct, since the residual for the finite volumes
        // which are on the boundary are counted once for every
        // process. As often in life: shit happens (, we don't care)...
        Scalar result2 = dest.two_norm2();
        result2 = asImp_().gridView().comm().sum(result2);

        return std::sqrt(result2);
    }

    /*!
     * \brief Compute the integral over the domain of the storage
     *        terms of all conservation quantities.
     *
     * \copydetails Doxygen::storageParam
     */
    void globalStorage(EqVector &storage, int timeIdx = 0) const
    {
        storage = 0;

        OmpMutex mutex;
        ThreadedEntityIterator<GridView, /*codim=*/0> threadedElemIt(gridView());
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            // Attention: the variables below are thread specific and thus cannot be
            // moved in front of the #pragma!
            int threadId = ThreadManager::threadId();
            ElementContext elemCtx(simulator_);
            ElementIterator elemIt = gridView().template begin</*codim=*/0>();
            LocalBlockVector elemStorage;

            for (threadedElemIt.beginParallel(elemIt);
                 !threadedElemIt.isFinished(elemIt);
                 threadedElemIt.increment(elemIt))
            {
                const Element& elem = *elemIt;
                if (elem.partitionType() != Dune::InteriorEntity)
                    continue; // ignore ghost and overlap elements

                elemCtx.updateStencil(elem);
                elemCtx.updatePrimaryIntensiveQuantities(timeIdx);

                int numPrimaryDof = elemCtx.numPrimaryDof(timeIdx);
                elemStorage.resize(numPrimaryDof);

                localResidual(threadId).evalStorage(elemStorage, elemCtx, timeIdx);

                ScopedLock addLock(mutex);
                for (int dofIdx = 0; dofIdx < numPrimaryDof; ++dofIdx)
                    for (int eqIdx = 0; eqIdx < numEq; ++eqIdx)
                        storage[eqIdx] += Toolbox::value(elemStorage[dofIdx][eqIdx]);
                addLock.unlock();
            }
        }

        storage = gridView_.comm().sum(storage);
    }

    /*!
     * \brief Ensure that the difference between the storage terms of the last and of the
     *        current time step is consistent with the source and boundary terms.
     *
     * This method is purely intented for debugging purposes. If the program is compiled
     * with optimizations enabled, it becomes a no-op.
     */
    void checkConservativeness(Scalar tolerance = -1, bool verbose=false) const
    {
#ifndef NDEBUG
        EqVector storageBeginTimeStep;
        EqVector storageEndTimeStep;

        Scalar totalBoundaryArea(0.0);
        Scalar totalVolume(0.0);
        EvalEqVector totalRate(Toolbox::createConstant(0.0));

        // take the newton tolerance times the total volume of the grid if we're not
        // given an explicit tolerance...
        if (tolerance <= 0) {
            tolerance =
                simulator_.model().newtonMethod().tolerance()
                * simulator_.model().gridTotalVolume()
                * 1000;
        }

        // we assume the implicit Euler time discretization for now...
        assert(historySize == 2);

        globalStorage(storageBeginTimeStep, /*timeIdx=*/1);
        globalStorage(storageEndTimeStep, /*timeIdx=*/0);

        // calculate the rate at the boundary and the source rate
        ElementContext elemCtx(simulator_);
        auto eIt = simulator_.gridView().template begin</*codim=*/0>();
        const auto &elemEndIt = simulator_.gridView().template end</*codim=*/0>();
        for (; eIt != elemEndIt; ++eIt) {
            if (eIt->partitionType() != Dune::InteriorEntity)
                continue; // ignore ghost and overlap elements

            elemCtx.updateAll(*eIt);

            // handle the boundary terms
            if (elemCtx.onBoundary()) {
                BoundaryContext boundaryCtx(elemCtx);

                for (int faceIdx = 0; faceIdx < boundaryCtx.numBoundaryFaces(/*timeIdx=*/0); ++faceIdx) {
                    BoundaryRateVector values;
                    simulator_.problem().boundary(values,
                                                         boundaryCtx,
                                                         faceIdx,
                                                         /*timeIdx=*/0);
                    Valgrind::CheckDefined(values);

                    int dofIdx = boundaryCtx.interiorScvIndex(faceIdx, /*timeIdx=*/0);
                    const auto &insideIntQuants = elemCtx.intensiveQuantities(dofIdx, /*timeIdx=*/0);

                    Scalar bfArea =
                        boundaryCtx.boundarySegmentArea(faceIdx, /*timeIdx=*/0)
                        * insideIntQuants.extrusionFactor();

                    for (unsigned i = 0; i < values.size(); ++i)
                        values[i] *= bfArea;

                    totalBoundaryArea += bfArea;
                    totalRate += values;
                }
            }

            // deal with the source terms
            for (int dofIdx = 0; dofIdx < elemCtx.numPrimaryDof(/*timeIdx=*/0); ++ dofIdx) {
                RateVector values;
                simulator_.problem().source(values,
                                            elemCtx,
                                            dofIdx,
                                            /*timeIdx=*/0);
                Valgrind::CheckDefined(values);

                const auto &intQuants = elemCtx.intensiveQuantities(dofIdx, /*timeIdx=*/0);
                Scalar dofVolume =
                    elemCtx.dofVolume(dofIdx, /*timeIdx=*/0)
                    * intQuants.extrusionFactor();
                for (int eqIdx = 0; eqIdx < numEq; ++ eqIdx)
                    totalRate[eqIdx] += -dofVolume*Toolbox::value(values[eqIdx]);
                totalVolume += dofVolume;
            }
        }

        // summarize everything over all processes
        const auto &comm = simulator_.gridView().comm();
        totalRate = comm.sum(totalRate);
        totalBoundaryArea = comm.sum(totalBoundaryArea);
        totalVolume = comm.sum(totalVolume);

        if (comm.rank() == 0) {
            EqVector storageRate = storageBeginTimeStep;
            storageRate -= storageEndTimeStep;
            storageRate /= simulator_.timeStepSize();
            if (verbose) {
                std::cout << "storage at beginning of time step: " << storageBeginTimeStep << "\n";
                std::cout << "storage at end of time step: " << storageEndTimeStep << "\n";
                std::cout << "rate based on storage terms: " << storageRate << "\n";
                std::cout << "rate based on source and boundary terms: " << totalRate << "\n";
                std::cout << "difference in rates: ";
                for (int eqIdx = 0; eqIdx < EqVector::dimension; ++eqIdx)
                    std::cout << (storageRate[eqIdx] - Toolbox::value(totalRate[eqIdx])) << " ";
                std::cout << "\n";
            }
            for (int eqIdx = 0; eqIdx < EqVector::dimension; ++eqIdx) {
                Scalar eps =
                    (std::abs(storageRate[eqIdx]) + Toolbox::value(totalRate[eqIdx]))*tolerance;
                eps = std::max(tolerance, eps);
                assert(std::abs(storageRate[eqIdx] - Toolbox::value(totalRate[eqIdx])) <= eps);
            }
        }
#endif // NDEBUG
    }

    /*!
     * \brief Returns the volume \f$\mathrm{[m^3]}\f$ of a given control volume.
     *
     * \param globalIdx The global index of the degree of freedom
     */
    Scalar dofTotalVolume(int globalIdx) const
    { return dofTotalVolume_[globalIdx]; }

    /*!
     * \brief Returns if the overlap of the volume ofa degree of freedom is non-zero.
     *
     * \param globalIdx The global index of the degree of freedom
     */
    bool isLocalDof(int globalIdx) const
    { return isLocalDof_[globalIdx]; }

    /*!
     * \brief Returns the volume \f$\mathrm{[m^3]}\f$ of the whole grid which represents
     *        the spatial domain.
     */
    Scalar gridTotalVolume() const
    { return gridTotalVolume_; }

    /*!
     * \brief Reference to the solution at a given history index as a block vector.
     *
     * \param timeIdx The index of the solution used by the time discretization.
     */
    const SolutionVector &solution(int timeIdx) const
    {
        return solution_[timeIdx]->blockVector();
    }

    /*!
     * \copydoc solution(int) const
     */
    SolutionVector &solution(int timeIdx)
    {
        return solution_[timeIdx]->blockVector();
    }

  protected:
    /*!
     * \copydoc solution(int) const
     */
    SolutionVector &mutableSolution(int timeIdx) const
    {
        return solution_[timeIdx]->blockVector();
    }

  public:
    /*!
     * \brief Returns the operator linearizer for the global jacobian of
     *        the problem.
     */
    const Linearizer &linearizer() const
    { return *linearizer_; }

    /*!
     * \brief Returns the object which linearizes the global system of equations at the
     *        current solution.
     */
    Linearizer &linearizer()
    { return *linearizer_; }

    /*!
     * \brief Returns the local jacobian which calculates the local
     *        stiffness matrix for an arbitrary element.
     *
     * The local stiffness matrices of the element are used by
     * the jacobian linearizer to produce a global linerization of the
     * problem.
     */
    const LocalLinearizer &localLinearizer(int openMpThreadId) const
    { return localLinearizer_[openMpThreadId]; }
    /*!
     * \copydoc localLinearizer() const
     */
    LocalLinearizer &localLinearizer(int openMpThreadId)
    { return localLinearizer_[openMpThreadId]; }

    /*!
     * \brief Returns the object to calculate the local residual function.
     */
    const LocalResidual &localResidual(int openMpThreadId) const
    { return asImp_().localLinearizer(openMpThreadId).localResidual(); }
    /*!
     * \copydoc localResidual() const
     */
    LocalResidual &localResidual(int openMpThreadId)
    { return asImp_().localLinearizer(openMpThreadId).localResidual(); }

    /*!
     * \brief Returns the relative weight of a primary variable for
     *        calculating relative errors.
     *
     * \param globalDofIdx The global index of the degree of freedom
     * \param pvIdx The index of the primary variable
     */
    Scalar primaryVarWeight(int globalDofIdx, int pvIdx) const
    {
        Scalar absPv = std::abs(asImp_().solution(/*timeIdx=*/1)[globalDofIdx][pvIdx]);
        return 1.0/std::max(absPv, 1.0);
    }

    /*!
     * \brief Returns the relative weight of an equation
     *
     * \param globalVertexIdx The global index of the vertex
     * \param eqIdx The index of the equation
     */
    Scalar eqWeight(int globalVertexIdx, int eqIdx) const
    { return 1.0; }

    /*!
     * \brief Returns the relative error between two vectors of
     *        primary variables.
     *
     * \param vertexIdx The global index of the control volume's
     *                  associated vertex
     * \param pv1 The first vector of primary variables
     * \param pv2 The second vector of primary variables
     */
    Scalar relativeDofError(int vertexIdx,
                            const PrimaryVariables &pv1,
                            const PrimaryVariables &pv2) const
    {
        Scalar result = 0.0;
        for (int j = 0; j < numEq; ++j) {
            Scalar weight = asImp_().primaryVarWeight(vertexIdx, j);
            Scalar eqErr = std::abs((pv1[j] - pv2[j])*weight);
            //Scalar eqErr = std::abs(pv1[j] - pv2[j]);
            //eqErr *= std::max<Scalar>(1.0, std::abs(pv1[j] + pv2[j])/2);

            result = std::max(result, eqErr);
        }
        return result;
    }

    /*!
     * \brief Try to progress the model to the next timestep.
     *
     * \param solver The non-linear solver
     */
    bool update(NewtonMethod &solver)
    {
#if HAVE_VALGRIND
        for (size_t i = 0; i < asImp_().solution(/*timeIdx=*/0).size(); ++i)
            asImp_().solution(/*timeIdx=*/0)[i].checkDefined();
#endif // HAVE_VALGRIND

        Timer prePostProcessTimer;
        prePostProcessTimer.start();
        asImp_().updateBegin();
        prePostProcessTimer.stop();
        simulator_.addPrePostProcessTime(prePostProcessTimer.realTimeElapsed());

        bool converged = solver.apply();

        prePostProcessTimer.start();
        if (converged)
            asImp_().updateSuccessful();
        else
            asImp_().updateFailed();
        prePostProcessTimer.stop();
        simulator_.addPrePostProcessTime(prePostProcessTimer.realTimeElapsed());

#if HAVE_VALGRIND
        // make sure that the "non-pseudo" primary variables are defined. Note that
        // because of the padding, we can't just simply ask valgrind to check the whole
        // solution vectors for definedness...
        for (size_t i = 0; i < asImp_().solution(/*timeIdx=*/0).size(); ++i) {
            for (size_t eqIdx = 0; eqIdx < numEq; ++eqIdx)
                Valgrind::CheckDefined(asImp_().solution(/*timeIdx=*/0)[i][eqIdx]);
        }
#endif // HAVE_VALGRIND

        return converged;
    }

    /*!
     * \brief Syncronize the values of the primary variables on the
     *        degrees of freedom that overlap with the neighboring
     *        processes.
     *
     * By default, this method does nothing...
     */
    void syncOverlap()
    { }

    /*!
     * \brief Called by the update() method before it tries to
     *        apply the newton method. This is primary a hook
     *        which the actual model can overload.
     */
    void updateBegin()
    { updateBoundary_(); }


    /*!
     * \brief Called by the update() method if it was
     *        successful.
     */
    void updateSuccessful()
    { }

    /*!
     * \brief Called by the update() method when the grid should be refined.
     */
    void adaptGrid()
    {
#if HAVE_DUNE_FEM
        // adapt the grid if enabled and if all dependencies are available
        // adaptation is only done if markForGridAdaptation returns true
        if (enableGridAdaptation_)
        {
            // check if problem allows for adaptation and cells were marked
            if( simulator_.problem().markForGridAdaptation() )
            {
                // adapt the grid and load balance if necessary
                adaptationManager_->adapt();

                // if the grid has potentially changed, we need to re-create the
                // supporting data structures.
                resetLinearizer();
                finishInit();
                updateBoundary_();

                // notify the problem that the grid has changed
                simulator_.problem().gridChanged();

                // notify the modules for visualization output
                auto outIt = outputModules_.begin();
                auto outEndIt = outputModules_.end();
                for (; outIt != outEndIt; ++outIt)
                    (*outIt)->allocBuffers();
            }
        }
#endif
    }

    /*!
     * \brief Called by the update() method if it was
     *        unsuccessful. This is primary a hook which the actual
     *        model can overload.
     */
    void updateFailed()
    {
        // Reset the current solution to the one of the
        // previous time step so that we can start the next
        // update at a physically meaningful solution.
        intensiveQuantityCache_[/*timeIdx=*/0] = intensiveQuantityCache_[/*timeIdx=*/1];
        intensiveQuantityCacheUpToDate_[/*timeIdx=*/0] = intensiveQuantityCacheUpToDate_[/*timeIdx=*/1];

        solution(/*timeIdx=*/0) = solution(/*timeIdx=*/1);
        linearizer_->relinearizeAll();
    }

    /*!
     * \brief Called by the problem if a time integration was
     *        successful, post processing of the solution is done and
     *        the result has been written to disk.
     *
     * This should prepare the model for the next time integration.
     */
    void advanceTimeLevel()
    {
        // at this point we can adapt the grid
        asImp_().adaptGrid();

        // make the current solution the previous one.
        solution(/*timeIdx=*/1) = solution(/*timeIdx=*/0);

        // shift the intensive quantities cache by one position in the
        // history
        asImp_().shiftIntensiveQuantityCache(/*numSlots=*/1);
    }

    /*!
     * \brief Serializes the current state of the model.
     *
     * \tparam Restarter The type of the serializer class
     *
     * \param res The serializer object
     */
    template <class Restarter>
    void serialize(Restarter &res)
    {
        OPM_THROW(std::runtime_error,
                  "Not implemented: The discretization chosen for this problem does not support"
                  " restart files. (serialize() method unimplemented)");
    }

    /*!
     * \brief Deserializes the state of the model.
     *
     * \tparam Restarter The type of the serializer class
     *
     * \param res The serializer object
     */
    template <class Restarter>
    void deserialize(Restarter &res)
    {
        OPM_THROW(std::runtime_error,
                  "Not implemented: The discretization chosen for this problem does not support"
                  " restart files. (deserialize() method unimplemented)");
    }

    /*!
     * \brief Write the current solution for a degree of freedom to a
     *        restart file.
     *
     * \param outstream The stream into which the vertex data should
     *                  be serialized to
     * \param dof The Dune entity which's data should be serialized
     */
    template <class DofEntity>
    void serializeEntity(std::ostream &outstream,
                         const DofEntity &dof)
    {
#if DUNE_VERSION_NEWER(DUNE_COMMON, 2, 4)
        int dofIdx = asImp_().dofMapper().index(dof);
#else
        int dofIdx = asImp_().dofMapper().map(dof);
#endif

        // write phase state
        if (!outstream.good()) {
            OPM_THROW(std::runtime_error, "Could not serialize degree of freedom " << dofIdx);
        }

        for (int eqIdx = 0; eqIdx < numEq; ++eqIdx) {
            outstream << solution(/*timeIdx=*/0)[dofIdx][eqIdx] << " ";
        }
    }

    /*!
     * \brief Reads the current solution variables for a degree of
     *        freedom from a restart file.
     *
     * \param instream The stream from which the vertex data should
     *                  be deserialized from
     * \param dof The Dune entity which's data should be deserialized
     */
    template <class DofEntity>
    void deserializeEntity(std::istream &instream,
                           const DofEntity &dof)
    {
#if DUNE_VERSION_NEWER(DUNE_COMMON, 2, 4)
        int dofIdx = asImp_().dofMapper().index(dof);
#else
        int dofIdx = asImp_().dofMapper().map(dof);
#endif

        for (int eqIdx = 0; eqIdx < numEq; ++eqIdx) {
            if (!instream.good())
                OPM_THROW(std::runtime_error,
                          "Could not deserialize degree of freedom " << dofIdx);
            instream >> solution(/*timeIdx=*/0)[dofIdx][eqIdx];
        }
    }

    /*!
     * \brief Returns the number of degrees of freedom (DOFs) for the computational grid
     */
    size_t numGridDof() const
    { OPM_THROW(std::logic_error,
                "The discretization class must implement the numGridDof() method!"); }

    /*!
     * \brief Returns the number of degrees of freedom (DOFs) of the auxiliary equations
     */
    size_t numAuxiliaryDof() const
    {
        size_t result = 0;
        auto auxModIt = auxEqModules_.begin();
        const auto& auxModEndIt = auxEqModules_.end();
        for (; auxModIt != auxModEndIt; ++auxModIt)
            result += (*auxModIt)->numDofs();

        return result;
    }

    /*!
     * \brief Returns the total number of degrees of freedom (i.e., grid plux auxiliary DOFs)
     */
    size_t numTotalDof() const
    { return asImp_().numGridDof() + numAuxiliaryDof(); }

    /*!
     * \brief Mapper to convert the Dune entities of the
     *        discretization's degrees of freedoms are to indices.
     */
    const DofMapper &dofMapper() const
    { OPM_THROW(std::logic_error,
                "The discretization class must implement the dofMapper() method!"); }

    /*!
     * \brief Mapper for vertices to indices.
     */
    const VertexMapper &vertexMapper() const
    { return simulator_.problem().vertexMapper(); }

    /*!
     * \brief Mapper for elements to indices.
     */
    const ElementMapper &elementMapper() const
    { return simulator_.problem().elementMapper(); }

    /*!
     * \brief Resets the Jacobian matrix linearizer, so that the
     *        boundary types can be altered.
     */
    void resetLinearizer ()
    {
        delete linearizer_;
        linearizer_ = new Linearizer;
        linearizer_->init(simulator_);
    }

    /*!
     * \brief Return whether a degree of freedom is located on the
     *        domain boundary.
     *
     * \param globalIdx The global space index of the degree of freedom of interest.
     */
    bool onBoundary(int globalIdx) const
    { return onBoundary_[globalIdx]; }

    /*!
     * \brief Returns a string of discretization's human-readable name
     */
    static std::string discretizationName()
    { return ""; }

    /*!
     * \brief Given an primary variable index, return a human readable name.
     *
     * \param pvIdx The index of the primary variable of interest.
     */
    std::string primaryVarName(int pvIdx) const
    {
        std::ostringstream oss;
        oss << "primary variable_" << pvIdx;
        return oss.str();
    }

    /*!
     * \brief Given an equation index, return a human readable name.
     *
     * \param eqIdx The index of the conservation equation of interest.
     */
    std::string eqName(int eqIdx) const
    {
        std::ostringstream oss;
        oss << "equation_" << eqIdx;
        return oss.str();
    }

    /*!
     * \brief Update the weights of all primary variables within an
     *        element given the complete set of intensive quantities
     *
     * \copydetails Doxygen::ecfvElemCtxParam
     */
    void updatePVWeights(const ElementContext &elemCtx) const
    { }

    /*!
     * \brief Add an module for writing visualization output after a timestep.
     */
    void addOutputModule(BaseOutputModule<TypeTag>* newModule)
    { outputModules_.push_back(newModule); }

    /*!
     * \brief Add the vector fields for analysing the convergence of
     *        the newton method to the a VTK writer.
     *
     * \param writer The writer object to which the fields should be added.
     * \param u The solution function
     * \param deltaU The delta of the solution function before and after the Newton update
     */
    template <class VtkMultiWriter>
    void addConvergenceVtkFields(VtkMultiWriter &writer,
                                 const SolutionVector &u,
                                 const GlobalEqVector &deltaU) const
    {
        typedef std::vector<double> ScalarBuffer;

        GlobalEqVector globalResid(u.size());
        asImp_().globalResidual(globalResid, u);

        // create the required scalar fields
        unsigned numGridDof = asImp_().numGridDof();

        // global defect of the two auxiliary equations
        ScalarBuffer* def[numEq];
        ScalarBuffer* delta[numEq];
        ScalarBuffer* priVars[numEq];
        ScalarBuffer* priVarWeight[numEq];
        ScalarBuffer* relError = writer.allocateManagedScalarBuffer(numGridDof);
        ScalarBuffer* dofColor = writer.allocateManagedScalarBuffer(numGridDof);
        for (int pvIdx = 0; pvIdx < numEq; ++pvIdx) {
            priVars[pvIdx] = writer.allocateManagedScalarBuffer(numGridDof);
            priVarWeight[pvIdx] = writer.allocateManagedScalarBuffer(numGridDof);
            delta[pvIdx] = writer.allocateManagedScalarBuffer(numGridDof);
            def[pvIdx] = writer.allocateManagedScalarBuffer(numGridDof);
        }

        for (unsigned globalIdx = 0; globalIdx < numGridDof; ++ globalIdx)
        {
            for (int pvIdx = 0; pvIdx < numEq; ++pvIdx) {
                (*priVars[pvIdx])[globalIdx] = u[globalIdx][pvIdx];
                (*priVarWeight[pvIdx])[globalIdx] = asImp_().primaryVarWeight(globalIdx, pvIdx);
                (*delta[pvIdx])[globalIdx] = - deltaU[globalIdx][pvIdx];
                (*def[pvIdx])[globalIdx] = globalResid[globalIdx][pvIdx];
            }

            PrimaryVariables uOld(u[globalIdx]);
            PrimaryVariables uNew(uOld);
            uNew -= deltaU[globalIdx];
            (*relError)[globalIdx] = asImp_().relativeDofError(globalIdx, uOld, uNew);
            (*dofColor)[globalIdx] = linearizer().dofColor(globalIdx);
        }

        DiscBaseOutputModule::attachScalarDofData_(writer, *relError, "relErr");

        for (int i = 0; i < numEq; ++i) {
            std::ostringstream oss;
            oss.str(""); oss << "priVar_" << asImp_().primaryVarName(i);
            DiscBaseOutputModule::attachScalarDofData_(writer,
                                                       *priVars[i],
                                                       oss.str());

            oss.str(""); oss << "delta_" << asImp_().primaryVarName(i);
            DiscBaseOutputModule::attachScalarDofData_(writer,
                                                       *delta[i],
                                                       oss.str());

            oss.str(""); oss << "weight_" << asImp_().primaryVarName(i);
            DiscBaseOutputModule::attachScalarDofData_(writer,
                                                       *priVarWeight[i],
                                                       oss.str());

            oss.str(""); oss << "defect_" << asImp_().eqName(i);
            DiscBaseOutputModule::attachScalarDofData_(writer,
                                                       *def[i],
                                                       oss.str());
        }

        DiscBaseOutputModule::attachScalarDofData_(writer, *dofColor, "color");

        asImp_().prepareOutputFields();
        asImp_().appendOutputFields(writer);
    }

    /*!
     * \brief Prepare the quantities relevant for the current solution
     *        to be appended to the output writers.
     */
    void prepareOutputFields() const
    {
        auto modIt = outputModules_.begin();
        const auto &modEndIt = outputModules_.end();
        for (; modIt != modEndIt; ++modIt) {
            (*modIt)->allocBuffers();
        }

        // iterate over grid
        ElementContext elemCtx(simulator_);

        ElementIterator elemIt = this->gridView().template begin<0>();
        ElementIterator elemEndIt = this->gridView().template end<0>();
        for (; elemIt != elemEndIt; ++elemIt) {
            const Element& elem = *elemIt;
            if (elem.partitionType() != Dune::InteriorEntity)
                continue;

            elemCtx.updateStencil(elem);
            elemCtx.updateIntensiveQuantities(/*timeIdx=*/0);
            elemCtx.updateExtensiveQuantities(/*timeIdx=*/0);

            modIt = outputModules_.begin();
            for (; modIt != modEndIt; ++modIt)
                (*modIt)->processElement(elemCtx);
        }
    }

    /*!
     * \brief Append the quantities relevant for the current solution
     *        to an output writer.
     */
    void appendOutputFields(BaseOutputWriter &writer) const
    {
        auto modIt = outputModules_.begin();
        const auto &modEndIt = outputModules_.end();
        for (; modIt != modEndIt; ++modIt)
            (*modIt)->commitBuffers(writer);
    }

    /*!
     * \brief Reference to the grid view of the spatial domain.
     */
    const GridView &gridView() const
    { return gridView_; }

    /*!
     * \brief Add a module for an auxiliary equation.
     *
     * This module can add additional degrees of freedom and additional off-diagonal
     * elements, but the number of equations per DOF needs to be the same as for the
     * "main" model.
     *
     * For example, auxiliary modules can be used to specify non-neighboring connections,
     * well equations or model couplings via mortar DOFs. Auxiliary equations are
     * completely optional, though.
     */
    void addAuxiliaryModule(std::shared_ptr<BaseAuxiliaryModule<TypeTag> > auxMod)
    {
        auxMod->setDofOffset(numTotalDof());
        auxEqModules_.push_back(auxMod);

        // resize the solutions
        if( enableGridAdaptation_ &&  ! std::is_same< DiscreteFunction, BlockVectorWrapper >:: value )
        {
            OPM_THROW(Opm::NotAvailable,
                      "Problems which require auxiliary modules cannot be used in conjunction "
                      "with dune-fem");
        }
        const size_t nDofs = numTotalDof();
        for (int timeIdx = 0; timeIdx < historySize; ++timeIdx) {
            solution(timeIdx).resize(nDofs);
        }

        auxMod->applyInitial();
    }

    /*!
     * \brief Causes the list of auxiliary equations to be cleared
     *
     * Note that this method implies recreateMatrix()
     */
    void clearAuxiliaryModules()
    { auxEqModules_.clear(); }

    /*!
     * \brief Returns the number of modules for auxiliary equations
     */
    size_t numAuxiliaryModules() const
    { return auxEqModules_.size(); }

    /*!
     * \brief Returns a given module for auxiliary equations
     */
    std::shared_ptr<BaseAuxiliaryModule<TypeTag> > auxiliaryModule(int auxEqModIdx)
    { return auxEqModules_[auxEqModIdx]; }

    /*!
     * \brief Returns a given module for auxiliary equations
     */
    std::shared_ptr<const BaseAuxiliaryModule<TypeTag> > auxiliaryModule(int auxEqModIdx) const
    { return auxEqModules_[auxEqModIdx]; }

protected:
    void resizeAndResetIntensiveQuantitiesCache_()
    {
        if (storeIntensiveQuantities_()) {
            const int nDofs = asImp_().numGridDof();
            for( int timeIdx=0; timeIdx<historySize; ++timeIdx )
            {
              intensiveQuantityCache_[timeIdx].resize(nDofs);
              intensiveQuantityCacheUpToDate_[timeIdx].resize(nDofs);
              std::fill( intensiveQuantityCacheUpToDate_[timeIdx].begin(),
                         intensiveQuantityCacheUpToDate_[timeIdx].end(), false );
            }
        }
    }
    template <class Context>
    void supplementInitialSolution_(PrimaryVariables &priVars,
                                    const Context &context, int dofIdx, int timeIdx)
    { }

    static bool storeIntensiveQuantities_()
    { return enableIntensiveQuantitiesCache_() || enableThermodynamicHints_(); }

    static bool enableIntensiveQuantitiesCache_()
    { return EWOMS_GET_PARAM(TypeTag, bool, EnableIntensiveQuantityCache); }

    static bool enableThermodynamicHints_()
    { return EWOMS_GET_PARAM(TypeTag, bool, EnableThermodynamicHints); }

    /*!
     * \brief Register all output modules which make sense for the model.
     *
     * This method is supposed to be overloaded by the actual models,
     * or else only the primary variables can be written to the result
     * files.
     */
    void registerOutputModules_()
    {
        // add the output modules available on all model
        auto *mod = new Ewoms::VtkPrimaryVarsModule<TypeTag>(simulator_);
        this->outputModules_.push_back(mod);
    }

    /*!
     * \brief Reference to the local residal object
     */
    LocalResidual &localResidual_()
    { return localLinearizer_.localResidual(); }

    /*!
     * \brief Find the degrees of freedoms adjacent to the grid boundary.
     */
    void updateBoundary_()
    {
        // resize the vectors and set everything to not being on the boundary
        onBoundary_.resize(asImp_().numGridDof());
        std::fill(onBoundary_.begin(), onBoundary_.end(), /*value=*/false);

        // loop over all elements of the grid
        Stencil stencil(gridView_);
        ElementIterator elemIt = gridView_.template begin<0>();
        const ElementIterator elemEndIt = gridView_.template end<0>();
        for (; elemIt != elemEndIt; ++elemIt) {
            stencil.update(*elemIt);

            // do nothing if the element does not have boundary intersections
            if (stencil.numBoundaryFaces() == 0)
                continue;

            for (int dofIdx = 0; dofIdx < stencil.numPrimaryDof(); ++dofIdx) {
                int globalIdx = stencil.globalSpaceIndex(dofIdx);
                onBoundary_[globalIdx] = true;
            }
        }
    }

    /*!
     * \brief Returns whether messages should be printed
     */
    bool verbose_() const
    { return gridView_.comm().rank() == 0; }

    Implementation &asImp_()
    { return *static_cast<Implementation*>(this); }
    const Implementation &asImp_() const
    { return *static_cast<const Implementation*>(this); }

    // the problem we want to solve. defines the constitutive
    // relations, matxerial laws, etc.
    Simulator &simulator_;

    // the representation of the spatial domain of the problem
    GridView gridView_;

    // a vector with all auxiliary equations to be considered
    std::vector<std::shared_ptr<BaseAuxiliaryModule<TypeTag> > > auxEqModules_;

    NewtonMethod newtonMethod_;

    // calculates the local jacobian matrix for a given element
    std::vector<LocalLinearizer> localLinearizer_;
    // Linearizes the problem at the current time step using the
    // local jacobian
    Linearizer *linearizer_;

    // cur is the current iterative solution, prev the converged
    // solution of the previous time step
    mutable IntensiveQuantitiesVector intensiveQuantityCache_[historySize];
    mutable std::vector<bool> intensiveQuantityCacheUpToDate_[historySize];


#if HAVE_DUNE_FEM
    DiscreteFunctionSpace space_;
    std::unique_ptr< RestrictProlong  > restrictProlong_;
    std::unique_ptr< AdaptationManager> adaptationManager_;
#else
    const size_t space_;
#endif
    mutable std::array< std::unique_ptr< DiscreteFunction >, historySize > solution_;

    // all the index of the BoundaryTypes object for a vertex
    std::vector<bool> onBoundary_;

    std::list<BaseOutputModule<TypeTag>*> outputModules_;

    Scalar gridTotalVolume_;
    std::vector<Scalar> dofTotalVolume_;
    std::vector<bool> isLocalDof_;

    bool enableGridAdaptation_;
};
} // namespace Ewoms

#endif
