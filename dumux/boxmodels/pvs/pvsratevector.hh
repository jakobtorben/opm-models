// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2011-2012 by Andreas Lauser                               *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 *
 * \brief Implements a vector representing molar rates for the primary variable switching compositional model.
 *
 * This class is basically a Dune::FieldVector which can be set using
 * either mass, molar or volumetric rates.
 */
#ifndef DUMUX_BOX_PVS_RATE_VECTOR_HH
#define DUMUX_BOX_PVS_RATE_VECTOR_HH

#include "pvsindices.hh"

#include <dumux/boxmodels/modules/energy/boxmultiphaseenergymodule.hh>
#include <dumux/material/constraintsolvers/ncpflash.hh>
#include <dumux/common/valgrind.hh>

#include <dune/common/fvector.hh>

namespace Dumux
{
/*!
 * \ingroup PvsModel
 *
 * \brief Implements a vector representing molar rates.
 *
 * This class is basically a Dune::FieldVector which can be set using
 * either mass, molar or volumetric rates.
 */
template <class TypeTag>
class PvsRateVector
    : public Dune::FieldVector<typename GET_PROP_TYPE(TypeTag, Scalar),
                               GET_PROP_VALUE(TypeTag, NumEq) >
{
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;

    enum { conti0EqIdx = Indices::conti0EqIdx };
    enum { numComponents = GET_PROP_VALUE(TypeTag, NumComponents) };
    enum { numEq = GET_PROP_VALUE(TypeTag, NumEq) };
    enum { enableEnergy = GET_PROP_VALUE(TypeTag, EnableEnergy) };

    typedef PvsRateVector<TypeTag> ThisType;
    typedef Dune::FieldVector<Scalar, numEq> ParentType;
    typedef BoxMultiPhaseEnergyModule<TypeTag, enableEnergy> EnergyModule;

public:
    /*!
     * \brief Default constructor
     */
    PvsRateVector()
        : ParentType()
    { Valgrind::SetUndefined(*this); }

    /*!
     * \brief Constructor with assignment from scalar
     */
    explicit PvsRateVector(Scalar value)
        : ParentType(value)
    { }

    /*!
     * \brief Copy constructor
     */
    PvsRateVector(const PvsRateVector &value)
        : ParentType(value)
    { }

    /*!
     * \brief Set a mass rate of the conservation quantities.
     *
     * Enthalpy is _not_ taken into account seperately here. This
     * means that it must be set to the desired value in the
     * parameter.
     */
    void setMassRate(const ParentType &value)
    {
        // convert to molar rates
        ParentType molarRate(value);
        for (int compIdx = 0; compIdx < numComponents; ++compIdx)
            molarRate[conti0EqIdx + compIdx] /= FluidSystem::molarMass(compIdx);
        
        setMolarRate(molarRate);
    }

    /*!
     * \brief Set a molar rate of the conservation quantities.
     *
     * Enthalpy is _not_ taken into account seperately here. This
     * means that it must be set to the desired value in the
     * parameter.
     */
    void setMolarRate(const ParentType &value)
    { ParentType::operator=(value); }

    /*!
     * \brief Set an enthalpy rate [J/As] where \f$A \in \{m^2, m^3\}\f$
     */
    void setEnthalpyRate(Scalar rate)
    { EnergyModule::setEnthalpyRate(*this, rate); }

    /*!
     * \brief Set a volumetric rate of a phase.
     *
     * Enthalpy is taken into account here.
     */
    template <class FluidState>
    void setVolumetricRate(const FluidState &fluidState, 
                           int phaseIdx, 
                           Scalar volume)
    {
        for (int compIdx = 0; compIdx < numComponents; ++compIdx)
            (*this)[conti0EqIdx + compIdx] = 
                fluidState.density(phaseIdx, compIdx)
                * fluidState.moleFraction(phaseIdx, compIdx)
                * volume;
        
        EnergyModule::setEnthalpyRate(*this, fluidState, phaseIdx, volume);
    }

    /*!
     * \brief Assign the rate vector from another rate vector
     */
    ThisType &operator=(const ParentType &value)
    { ParentType::operator=(value); return *this; }

    /*!
     * \brief Set all entries of the rate vector to a scalar value.
     */
    ThisType &operator=(Scalar value)
    { ParentType::operator=(value); return *this; }
};

} // end namepace

#endif
