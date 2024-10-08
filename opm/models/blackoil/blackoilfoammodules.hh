// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
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

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \brief Contains the classes required to extend the black-oil model to include the effects of foam.
 */
#ifndef EWOMS_BLACK_OIL_FOAM_MODULE_HH
#define EWOMS_BLACK_OIL_FOAM_MODULE_HH

#include "blackoilproperties.hh"

#include <dune/common/fvector.hh>

#include <opm/common/OpmLog/OpmLog.hpp>

#if HAVE_ECL_INPUT
#include <opm/input/eclipse/EclipseState/EclipseState.hpp>
#include <opm/input/eclipse/EclipseState/Tables/FoamadsTable.hpp>
#include <opm/input/eclipse/EclipseState/Tables/FoammobTable.hpp>
#endif

#include <opm/models/blackoil/blackoilfoamparams.hh>

#include <opm/models/discretization/common/fvbaseparameters.hh>
#include <opm/models/discretization/common/fvbaseproperties.hh>

#include <string>

namespace Opm {

/*!
 * \ingroup BlackOil
 * \brief Contains the high level supplements required to extend the black oil
 *        model to include the effects of foam.
 */
template <class TypeTag, bool enableFoamV = getPropValue<TypeTag, Properties::EnableFoam>()>
class BlackOilFoamModule
{
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using PrimaryVariables = GetPropType<TypeTag, Properties::PrimaryVariables>;
    using IntensiveQuantities = GetPropType<TypeTag, Properties::IntensiveQuantities>;
    using ExtensiveQuantities = GetPropType<TypeTag, Properties::ExtensiveQuantities>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using Model = GetPropType<TypeTag, Properties::Model>;
    using Simulator = GetPropType<TypeTag, Properties::Simulator>;
    using EqVector = GetPropType<TypeTag, Properties::EqVector>;
    using RateVector = GetPropType<TypeTag, Properties::RateVector>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;

    using Toolbox = MathToolbox<Evaluation>;

    using TabulatedFunction = typename BlackOilFoamParams<Scalar>::TabulatedFunction;

    static constexpr unsigned foamConcentrationIdx = Indices::foamConcentrationIdx;
    static constexpr unsigned contiFoamEqIdx = Indices::contiFoamEqIdx;
    static constexpr unsigned gasPhaseIdx = FluidSystem::gasPhaseIdx;
    static constexpr unsigned waterPhaseIdx = FluidSystem::waterPhaseIdx;

    static constexpr unsigned enableFoam = enableFoamV;

    static constexpr unsigned numEq = getPropValue<TypeTag, Properties::NumEq>();
    static constexpr unsigned numPhases = FluidSystem::numPhases;

    enum { enableSolvent = getPropValue<TypeTag, Properties::EnableSolvent>() };

public:
#if HAVE_ECL_INPUT
    /*!
     * \brief Initialize all internal data structures needed by the foam module
     */
    static void initFromState(const EclipseState& eclState)
    {
        // some sanity checks: if foam is enabled, the FOAM keyword must be
        // present, if foam is disabled the keyword must not be present.
        if (enableFoam && !eclState.runspec().phases().active(Phase::FOAM)) {
            throw std::runtime_error("Non-trivial foam treatment requested at compile time, but "
                                     "the deck does not contain the FOAM keyword");
        }
        else if (!enableFoam && eclState.runspec().phases().active(Phase::FOAM)) {
            throw std::runtime_error("Foam treatment disabled at compile time, but the deck "
                                     "contains the FOAM keyword");
        }

        if (!eclState.runspec().phases().active(Phase::FOAM)) {
            return; // foam treatment is supposed to be disabled
        }

        params_.transport_phase_ = eclState.getInitConfig().getFoamConfig().getTransportPhase();

        if (eclState.getInitConfig().getFoamConfig().getMobilityModel() != FoamConfig::MobilityModel::TAB) {
            throw std::runtime_error("In FOAMOPTS, only TAB is allowed for the gas mobility factor reduction model.");
        }

        const auto& tableManager = eclState.getTableManager();
        const unsigned int numSatRegions = tableManager.getTabdims().getNumSatTables();
        params_.setNumSatRegions(numSatRegions);
        const unsigned int numPvtRegions = tableManager.getTabdims().getNumPVTTables();
        params_.gasMobilityMultiplierTable_.resize(numPvtRegions);

        // Get and check FOAMROCK data.
        const FoamConfig& foamConf = eclState.getInitConfig().getFoamConfig();
        if (numSatRegions != foamConf.size()) {
            throw std::runtime_error("Inconsistent sizes, number of saturation regions differ from the number of elements "
                                     "in FoamConfig, which typically corresponds to the number of records in FOAMROCK.");
        }

        // Get and check FOAMADS data.
        const auto& foamadsTables = tableManager.getFoamadsTables();
        if (foamadsTables.empty()) {
            throw std::runtime_error("FOAMADS must be specified in FOAM runs");
        }
        if (numSatRegions != foamadsTables.size()) {
            throw std::runtime_error("Inconsistent sizes, number of saturation regions differ from the "
                                     "number of FOAMADS tables.");
        }

        // Set data that vary with saturation region.
        for (std::size_t satReg = 0; satReg < numSatRegions; ++satReg) {
            const auto& rec = foamConf.getRecord(satReg);
            params_.foamCoefficients_[satReg] = typename BlackOilFoamParams<Scalar>::FoamCoefficients();
            params_.foamCoefficients_[satReg].fm_min = rec.minimumSurfactantConcentration();
            params_.foamCoefficients_[satReg].fm_surf = rec.referenceSurfactantConcentration();
            params_.foamCoefficients_[satReg].ep_surf = rec.exponent();
            params_.foamRockDensity_[satReg] = rec.rockDensity();
            params_.foamAllowDesorption_[satReg] = rec.allowDesorption();
            const auto& foamadsTable = foamadsTables.template getTable<FoamadsTable>(satReg);
            const auto& conc = foamadsTable.getFoamConcentrationColumn();
            const auto& ads = foamadsTable.getAdsorbedFoamColumn();
            params_.adsorbedFoamTable_[satReg].setXYContainers(conc, ads);
        }

        // Get and check FOAMMOB data.
        const auto& foammobTables = tableManager.getFoammobTables();
        if (foammobTables.empty()) {
            // When in the future adding support for the functional
            // model, FOAMMOB will not be required anymore (functional
            // family of keywords can be used instead, FOAMFSC etc.).
            throw std::runtime_error("FOAMMOB must be specified in FOAM runs");
        }
        if (numPvtRegions != foammobTables.size()) {
            throw std::runtime_error("Inconsistent sizes, number of PVT regions differ from the "
                                     "number of FOAMMOB tables.");
        }

        // Set data that vary with PVT region.
        for (std::size_t pvtReg = 0; pvtReg < numPvtRegions; ++pvtReg) {
            const auto& foammobTable = foammobTables.template getTable<FoammobTable>(pvtReg);
            const auto& conc = foammobTable.getFoamConcentrationColumn();
            const auto& mobMult = foammobTable.getMobilityMultiplierColumn();
            params_.gasMobilityMultiplierTable_[pvtReg].setXYContainers(conc, mobMult);
        }
    }
#endif

    /*!
     * \brief Register all run-time parameters for the black-oil foam module.
     */
    static void registerParameters()
    {
    }

    /*!
     * \brief Register all foam specific VTK and ECL output modules.
     */
    static void registerOutputModules(Model&,
                                      Simulator&)
    {
        if constexpr (enableFoam) {
            if (Parameters::Get<Parameters::EnableVtkOutput>()) {
                OpmLog::warning("VTK output requested, currently unsupported by the foam module.");
            }
        }
        //model.addOutputModule(new VtkBlackOilFoamModule<TypeTag>(simulator));
    }

    static bool primaryVarApplies(unsigned pvIdx)
    {
        if constexpr (enableFoam)
            return pvIdx == foamConcentrationIdx;
        else
            return false;
    }

    static std::string primaryVarName([[maybe_unused]] unsigned pvIdx)
    {
        assert(primaryVarApplies(pvIdx));
        return "foam_concentration";
    }

    static Scalar primaryVarWeight([[maybe_unused]] unsigned pvIdx)
    {
       assert(primaryVarApplies(pvIdx));

       // TODO: it may be beneficial to chose this differently.
       return static_cast<Scalar>(1.0);
    }

    static bool eqApplies(unsigned eqIdx)
    {
        if constexpr (enableFoam)
            return eqIdx == contiFoamEqIdx;
        else
            return false;

    }

    static std::string eqName([[maybe_unused]] unsigned eqIdx)
    {
        assert(eqApplies(eqIdx));

        return "conti^foam";
    }

    static Scalar eqWeight([[maybe_unused]] unsigned eqIdx)
    {
       assert(eqApplies(eqIdx));

       // TODO: it may be beneficial to chose this differently.
       return static_cast<Scalar>(1.0);
    }

    // must be called after water storage is computed
    template <class LhsEval>
    static void addStorage(Dune::FieldVector<LhsEval, numEq>& storage,
                           const IntensiveQuantities& intQuants)
    {
        if constexpr (enableFoam) {
            const auto& fs = intQuants.fluidState();

            LhsEval surfaceVolume = Toolbox::template decay<LhsEval>(intQuants.porosity());
            if (params_.transport_phase_ == Phase::WATER) {
                surfaceVolume *= (Toolbox::template decay<LhsEval>(fs.saturation(waterPhaseIdx))
                * Toolbox::template decay<LhsEval>(fs.invB(waterPhaseIdx)));
            } else if (params_.transport_phase_ == Phase::GAS) {
                surfaceVolume *= (Toolbox::template decay<LhsEval>(fs.saturation(gasPhaseIdx))
                * Toolbox::template decay<LhsEval>(fs.invB(gasPhaseIdx)));
            } else if (params_.transport_phase_ == Phase::SOLVENT) {
                if constexpr (enableSolvent) {
                    surfaceVolume *= (Toolbox::template decay<LhsEval>( intQuants.solventSaturation())
                    *  Toolbox::template decay<LhsEval>(intQuants.solventInverseFormationVolumeFactor()));
                }
            } else {
                throw std::runtime_error("Transport phase is GAS/WATER/SOLVENT");
            }

            // Avoid singular matrix if no gas is present.
            surfaceVolume = max(surfaceVolume, 1e-10);

            // Foam/surfactant in free phase.
            const LhsEval freeFoam = surfaceVolume
                * Toolbox::template decay<LhsEval>(intQuants.foamConcentration());

            // Adsorbed foam/surfactant.
            const LhsEval adsorbedFoam =
                Toolbox::template decay<LhsEval>(1.0 - intQuants.porosity())
                * Toolbox::template decay<LhsEval>(intQuants.foamRockDensity())
                * Toolbox::template decay<LhsEval>(intQuants.foamAdsorbed());

            LhsEval accumulationFoam = freeFoam + adsorbedFoam;
            storage[contiFoamEqIdx] += accumulationFoam;
        }
    }

    static void computeFlux([[maybe_unused]] RateVector& flux,
                            [[maybe_unused]] const ElementContext& elemCtx,
                            [[maybe_unused]] unsigned scvfIdx,
                            [[maybe_unused]] unsigned timeIdx)

    {
        if constexpr (enableFoam) {
            const auto& extQuants = elemCtx.extensiveQuantities(scvfIdx, timeIdx);
            const unsigned inIdx = extQuants.interiorIndex();

            // The effect of the mobility reduction factor is
            // incorporated in the mobility for the relevant phase,
            // so fluxes do not need modification here.
            switch (transportPhase()) {
                case Phase::WATER: {
                    const unsigned upIdx = extQuants.upstreamIndex(waterPhaseIdx);
                    const auto& up = elemCtx.intensiveQuantities(upIdx, timeIdx);
                    if (upIdx == inIdx) {
                        flux[contiFoamEqIdx] =
                            extQuants.volumeFlux(waterPhaseIdx)
                            *up.fluidState().invB(waterPhaseIdx)
                            *up.foamConcentration();
                    } else {
                        flux[contiFoamEqIdx] =
                            extQuants.volumeFlux(waterPhaseIdx)
                            *decay<Scalar>(up.fluidState().invB(waterPhaseIdx))
                            *decay<Scalar>(up.foamConcentration());
                    }
                    break;
                }
                case Phase::GAS: {
                    const unsigned upIdx = extQuants.upstreamIndex(gasPhaseIdx);
                    const auto& up = elemCtx.intensiveQuantities(upIdx, timeIdx);
                    if (upIdx == inIdx) {
                        flux[contiFoamEqIdx] =
                            extQuants.volumeFlux(gasPhaseIdx)
                            *up.fluidState().invB(gasPhaseIdx)
                            *up.foamConcentration();
                    } else {
                        flux[contiFoamEqIdx] =
                            extQuants.volumeFlux(gasPhaseIdx)
                            *decay<Scalar>(up.fluidState().invB(gasPhaseIdx))
                            *decay<Scalar>(up.foamConcentration());
                    }
                    break;
                }
                case Phase::SOLVENT: {
                    if constexpr (enableSolvent) {
                        const unsigned upIdx = extQuants.solventUpstreamIndex();
                        const auto& up = elemCtx.intensiveQuantities(upIdx, timeIdx);
                        if (upIdx == inIdx) {
                            flux[contiFoamEqIdx] =
                                extQuants.solventVolumeFlux()
                                *up.solventInverseFormationVolumeFactor()
                                *up.foamConcentration();
                        } else {
                            flux[contiFoamEqIdx] =
                                extQuants.solventVolumeFlux()
                                *decay<Scalar>(up.solventInverseFormationVolumeFactor())
                                *decay<Scalar>(up.foamConcentration());
                        }
                    } else {
                        throw std::runtime_error("Foam transport phase is SOLVENT but SOLVENT is not activated.");
                    }
                    break;
                }
                default: {
                    throw std::runtime_error("Foam transport phase must be GAS/WATER/SOLVENT.");
                }
            }
        }
    }

    /*!
     * \brief Return how much a Newton-Raphson update is considered an error
     */
    static Scalar computeUpdateError(const PrimaryVariables&,
                                     const EqVector&)
    {
        // do not consider the change of foam primary variables for convergence
        // TODO: maybe this should be changed
        return static_cast<Scalar>(0.0);
    }

    template <class DofEntity>
    static void serializeEntity([[maybe_unused]] const Model& model,
                                [[maybe_unused]] std::ostream& outstream,
                                [[maybe_unused]] const DofEntity& dof)
    {
        if constexpr (enableFoam) {
            unsigned dofIdx = model.dofMapper().index(dof);
            const PrimaryVariables& priVars = model.solution(/*timeIdx=*/0)[dofIdx];
            outstream << priVars[foamConcentrationIdx];
        }
    }

    template <class DofEntity>
    static void deserializeEntity([[maybe_unused]] Model& model,
                                  [[maybe_unused]] std::istream& instream,
                                  [[maybe_unused]] const DofEntity& dof)
    {
        if constexpr (enableFoam) {
            unsigned dofIdx = model.dofMapper().index(dof);
            PrimaryVariables& priVars0 = model.solution(/*timeIdx=*/0)[dofIdx];
            PrimaryVariables& priVars1 = model.solution(/*timeIdx=*/1)[dofIdx];

            instream >> priVars0[foamConcentrationIdx];

            // set the primary variables for the beginning of the current time step.
            priVars1[foamConcentrationIdx] = priVars0[foamConcentrationIdx];
        }
    }

    static const Scalar foamRockDensity(const ElementContext& elemCtx,
                                        unsigned scvIdx,
                                        unsigned timeIdx)
    {
        unsigned satnumRegionIdx = elemCtx.problem().satnumRegionIndex(elemCtx, scvIdx, timeIdx);
        return params_.foamRockDensity_[satnumRegionIdx];
    }

    static bool foamAllowDesorption(const ElementContext& elemCtx,
                                    unsigned scvIdx,
                                    unsigned timeIdx)
    {
        unsigned satnumRegionIdx = elemCtx.problem().satnumRegionIndex(elemCtx, scvIdx, timeIdx);
        return params_.foamAllowDesorption_[satnumRegionIdx];
    }

    static const TabulatedFunction& adsorbedFoamTable(const ElementContext& elemCtx,
                                                      unsigned scvIdx,
                                                      unsigned timeIdx)
    {
       unsigned satnumRegionIdx = elemCtx.problem().satnumRegionIndex(elemCtx, scvIdx, timeIdx);
       return params_.adsorbedFoamTable_[satnumRegionIdx];
    }

    static const TabulatedFunction& gasMobilityMultiplierTable(const ElementContext& elemCtx,
                                                               unsigned scvIdx,
                                                               unsigned timeIdx)
    {
       unsigned pvtnumRegionIdx = elemCtx.problem().pvtRegionIndex(elemCtx, scvIdx, timeIdx);
       return params_.gasMobilityMultiplierTable_[pvtnumRegionIdx];
    }

    static const typename BlackOilFoamParams<Scalar>::FoamCoefficients&
    foamCoefficients(const ElementContext& elemCtx,
                     const unsigned scvIdx,
                     const unsigned timeIdx)
    {
        unsigned satnumRegionIdx = elemCtx.problem().satnumRegionIndex(elemCtx, scvIdx, timeIdx);
        return params_.foamCoefficients_[satnumRegionIdx];
    }

    static Phase transportPhase() {
        return params_.transport_phase_;
    }

private:
    static BlackOilFoamParams<Scalar> params_;
};

template <class TypeTag, bool enableFoam>
BlackOilFoamParams<typename BlackOilFoamModule<TypeTag, enableFoam>::Scalar>
BlackOilFoamModule<TypeTag, enableFoam>::params_;

/*!
 * \ingroup BlackOil
 * \class Opm::BlackOilFoamIntensiveQuantities
 *
 * \brief Provides the volumetric quantities required for the equations needed by the
 *        polymers extension of the black-oil model.
 */
template <class TypeTag, bool enableFoam = getPropValue<TypeTag, Properties::EnableFoam>()>
class BlackOilFoamIntensiveQuantities
{
    using Implementation = GetPropType<TypeTag, Properties::IntensiveQuantities>;

    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using PrimaryVariables = GetPropType<TypeTag, Properties::PrimaryVariables>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using MaterialLaw = GetPropType<TypeTag, Properties::MaterialLaw>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;

    using FoamModule = BlackOilFoamModule<TypeTag>;

    enum { numPhases = getPropValue<TypeTag, Properties::NumPhases>() };
    enum { enableSolvent = getPropValue<TypeTag, Properties::EnableSolvent>() };

    static constexpr int foamConcentrationIdx = Indices::foamConcentrationIdx;
    static constexpr unsigned waterPhaseIdx = FluidSystem::waterPhaseIdx;
    static constexpr unsigned oilPhaseIdx = FluidSystem::oilPhaseIdx;
    static constexpr int gasPhaseIdx = FluidSystem::gasPhaseIdx;

public:

    /*!
     * \brief Update the intensive properties needed to handle polymers from the
     *        primary variables
     *
     */
    void foamPropertiesUpdate_(const ElementContext& elemCtx,
                               unsigned dofIdx,
                               unsigned timeIdx)
    {
        const PrimaryVariables& priVars = elemCtx.primaryVars(dofIdx, timeIdx);
        foamConcentration_ = priVars.makeEvaluation(foamConcentrationIdx, timeIdx);
        const auto& fs = asImp_().fluidState_;

        // Compute gas mobility reduction factor
        Evaluation mobilityReductionFactor = 1.0;
        if (false) {
            // The functional model is used.
            // TODO: allow this model.
            // In order to do this we must allow transport to be in the water phase, not just the gas phase.
            const auto& foamCoefficients = FoamModule::foamCoefficients(elemCtx, dofIdx, timeIdx);

            const Scalar fm_mob = foamCoefficients.fm_mob;

            const Scalar fm_surf = foamCoefficients.fm_surf;
            const Scalar ep_surf = foamCoefficients.ep_surf;

            const Scalar fm_oil = foamCoefficients.fm_oil;
            const Scalar fl_oil = foamCoefficients.fl_oil;
            const Scalar ep_oil = foamCoefficients.ep_oil;

            const Scalar fm_dry = foamCoefficients.fm_dry;
            const Scalar ep_dry = foamCoefficients.ep_dry;

            const Scalar fm_cap = foamCoefficients.fm_cap;
            const Scalar ep_cap = foamCoefficients.ep_cap;

            const Evaluation C_surf = foamConcentration_;
            const Evaluation Ca = 1e10; // TODO: replace with proper capillary number.
            const Evaluation S_o = fs.saturation(oilPhaseIdx);
            const Evaluation S_w = fs.saturation(waterPhaseIdx);

            Evaluation F1 = pow(C_surf/fm_surf, ep_surf);
            Evaluation F2 = pow((fm_oil-S_o)/(fm_oil-fl_oil), ep_oil);
            Evaluation F3 = pow(fm_cap/Ca, ep_cap);
            Evaluation F7 = 0.5 + atan(ep_dry*(S_w-fm_dry))/M_PI;

            mobilityReductionFactor = 1./(1. + fm_mob*F1*F2*F3*F7);
        } else {
            // The tabular model is used.
            // Note that the current implementation only includes the effect of foam concentration (FOAMMOB),
            // and not the optional pressure dependence (FOAMMOBP) or shear dependence (FOAMMOBS).
            const auto& gasMobilityMultiplier = FoamModule::gasMobilityMultiplierTable(elemCtx, dofIdx, timeIdx);
            mobilityReductionFactor = gasMobilityMultiplier.eval(foamConcentration_, /* extrapolate = */ true);
        }

        // adjust mobility
        switch (FoamModule::transportPhase()) {
            case Phase::WATER: {
                asImp_().mobility_[waterPhaseIdx] *= mobilityReductionFactor;
                break;
            }
            case Phase::GAS: {
                asImp_().mobility_[gasPhaseIdx] *= mobilityReductionFactor;
                break;
            }
            case Phase::SOLVENT: {
                if constexpr (enableSolvent) {
                    asImp_().solventMobility_ *= mobilityReductionFactor;
                } else {
                    throw std::runtime_error("Foam transport phase is SOLVENT but SOLVENT is not activated.");
                }
                break;
            }
            default: {
                throw std::runtime_error("Foam transport phase must be GAS/WATER/SOLVENT.");
            }
        }

        foamRockDensity_ = FoamModule::foamRockDensity(elemCtx, dofIdx, timeIdx);

        const auto& adsorbedFoamTable = FoamModule::adsorbedFoamTable(elemCtx, dofIdx, timeIdx);
        foamAdsorbed_ = adsorbedFoamTable.eval(foamConcentration_, /*extrapolate=*/true);
        if (!FoamModule::foamAllowDesorption(elemCtx, dofIdx, timeIdx)) {
            throw std::runtime_error("Foam module does not support the 'no desorption' option.");
        }
    }

    const Evaluation& foamConcentration() const
    { return foamConcentration_; }

    Scalar foamRockDensity() const
    { return foamRockDensity_; }

    const Evaluation& foamAdsorbed() const
    { return foamAdsorbed_; }

protected:
    Implementation& asImp_()
    { return *static_cast<Implementation*>(this); }

    Evaluation foamConcentration_;
    Scalar foamRockDensity_;
    Evaluation foamAdsorbed_;
};

template <class TypeTag>
class BlackOilFoamIntensiveQuantities<TypeTag, false>
{
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;

public:
    void foamPropertiesUpdate_(const ElementContext&,
                                  unsigned,
                                  unsigned)
    { }


    const Evaluation& foamConcentration() const
    { throw std::runtime_error("foamConcentration() called but foam is disabled"); }

    Scalar foamRockDensity() const
    { throw std::runtime_error("foamRockDensity() called but foam is disabled"); }

    Scalar foamAdsorbed() const
    { throw std::runtime_error("foamAdsorbed() called but foam is disabled"); }
};

} // namespace Opm

#endif
