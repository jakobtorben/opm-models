// $Id$

/*****************************************************************************
 *   Copyright (C) 2008 by Andreas Lauser                                    *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version, as long as this copyright notice    *
 *   is included in its original form.                                       *
 *                                                                           *
 *   This program is distributed WITHOUT ANY WARRANTY.                       *
 *****************************************************************************/
/*!
 * \file
 * \brief Simplyfies writing multi-file VTK datasets.
 */
#ifndef VTK_MULTI_WRITER_HH
#define VTK_MULTI_WRITER_HH

#include <dune/grid/io/file/vtk/vtkwriter.hh>
#include <dune/grid/common/genericreferenceelements.hh>


#include <dune/common/fvector.hh>
#include <dune/istl/bvector.hh>

#include <boost/format.hpp>

#include <list>
#include <iostream>
#include <string>

namespace Dune {
/*!
 * \brief Simplyfies writing multi-file VTK datasets.
 *
 * This class automatically keeps the meta file up to date and
 * simplifies writing datasets consisting of multiple files. (i.e.
 * multiple timesteps or grid refinements within a timestep.)
 */
template<class GridView>
class VtkMultiWriter
{
public:

    typedef Dune::VTKWriter<GridView> VtkWriter;
    VtkMultiWriter(const std::string &simName = "", std::string multiFileName = "")
    {
        simName_ = (simName.empty())?"sim":simName;
        multiFileName_ = multiFileName;
        if (multiFileName_.empty())
            multiFileName_ = (boost::format("%s.pvd")%simName_).str();

        writerNum_ = 0;
        commRank_ = 0;
        commSize_ = 1;
        wasRestarted_ = false;
    }

    ~VtkMultiWriter()
    {
        endMultiFile_();

        if (commRank_ == 0)
            multiFile_.close();
    }

    /*!
     * \brief Called when ever a new timestep or a new grid
     *        must be written.
     */
    void beginTimestep(double t, const GridView &gridView)
    {
        curGridView_ = &gridView;

        if (!multiFile_.is_open()) {
            commRank_ = gridView.comm().rank();
            commSize_ = gridView.comm().size();

            beginMultiFile_(multiFileName_);
        }


        curWriter_ = new VtkWriter(gridView);
        ++writerNum_;

        curTime_ = t;
        curOutFileName_ = fileName_();
    }

    /*!
     * \brief Write a vertex centered vector field to disk.
     */
    template <class Scalar, int nComp>
    Dune::BlockVector<Dune::FieldVector<Scalar, nComp> > *createField(int nEntities)
    {
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, nComp> > VectorField;

        VtkVectorFieldStoreImpl_<VectorField> *vfs =
            new VtkVectorFieldStoreImpl_<VectorField>(nEntities);
        vectorFields_.push_back(vfs);
        return &(vfs->vf);
    }

    /*!
     * \brief Add a finished vertex centered vector field to the
     *        output. The field must have been created using
     *        createField() and may not be modified after calling
     *        this method.
     */
    template <class VectorField>
    void addVertexData(VectorField *field, const char *name)
    {
        curWriter_->addVertexData(*field, name);
    }

    /*!
     * \brief Add a finished cell centered vector field to the
     *        output. The field must have been created using
     *        createField() and may not be modified after calling
     *        this method.
     */
    template <class VectorField>
    void addCellData(VectorField *field, const char *name)
    {
        curWriter_->addCellData(*field, name);
    }

    /*!
     * \brief Evaluates a single component of a function defined on the grid at the
     *        vertices and appends it to the writer.
     */
    template <class Function>
    void addScalarVertexFunction(const char *name,
                                 const Function &fn,
                                 int comp)
    {
        /*
        // useful typedefs
        typedef typename Function::RangeFieldType                      Scalar;
        typedef typename GridView::Traits::template Codim<GridView::dimension> VertexTraits;
        typedef typename VertexTraits::Entity                          Vertex;
        typedef typename VertexTraits::LeafIterator                    VertexIterator;
        typedef Dune::ReferenceElement<typename GridView::ctype, 0>        VertexReferenceElement;
        typedef Dune::ReferenceElements<typename GridView::ctype, 0>       VertexReferenceElements;
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, 1> >       ScalarField;

        // create a vertex based scalar field.
        ScalarField *field = createField<Scalar, 1>(vertexMap.size());
        std::vector<bool> vertexVisited(vertexMap.size(), false);

        // fill the Scalar field
        VertexIterator it = curGridView_->template leafbegin<GridView::dimension>();
        VertexIterator endIt = curGridView_->template leafend<GridView::dimension>();
        for (; it != endIt; ++it) {
        // extract the current solution's Sn component
        const VertexReferenceElement &refElem =
        VertexReferenceElements::general(it->geometry().type());
        Scalar compValue = fn.evallocal(comp,
        *it,
        refElem.position(0,0));

        // find out the cell's index
        unsigned vertexIndex = vertexMap.map(*it);
        (*field)[vertexIndex] = compValue;
        }

        addVertexData(field, name);
        */

        // this is pretty hacky as it assumes that the mapping
        // to the vertices is the same for the function a and
        // the vertex mapper
        typedef typename Function::RangeFieldType                Scalar;
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, 1> > ScalarField;

        unsigned nVerts = (*fn).size();
        ScalarField *field = createField<Scalar, 1>(nVerts);
        for (int i = 0; i < (int) (*fn).size(); i++) {
            (*field)[i] = (*fn)[i][comp];
        }

        addVertexData(field, name);
    }

    /*!
     * \brief Evaluates a single component of a function defined on the grid at the
     *        cell centers and appends it to the writer.
     */
    template <class Function, class CellMap>
    void addScalarCellFunction(const char *name,
                               const Function &fn,
                               const CellMap &cellMap,
                               int comp)
    {
        // some typedefs

        typedef typename Function::RT                                        Scalar;
        typedef typename GridView::template Codim<0>::Entity                 Cell;
        typedef typename GridView::template Codim<0>::Iterator               CellIterator;
        typedef Dune::GenericReferenceElement<typename GridView::ctype, GridView::dimgrid>  CellReferenceElement;
        typedef Dune::GenericReferenceElements<typename GridView::ctype, GridView::dimgrid> CellReferenceElements;
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, 1> >             ScalarField;

        // create a cell based scalar field.
        ScalarField *field = createField<Scalar, 1>(cellMap.size());

        // fill the Scalar field
        CellIterator it = curGridView_->template begin<0>();
        CellIterator endIt = curGridView_->template end<0>();
        for (; it != endIt; ++it) {
            // extract the current solution's Sn component
            const CellReferenceElement &refElem =
                CellReferenceElements::general(it->geometry().type());
            Scalar compValue = fn.evallocal(comp,
                                            *it,
                                            refElem.position(0,0));

            // find out the cell's index
            unsigned cellIndex = cellMap.map(*it);
            (*field)[cellIndex] = compValue;
        }

        addCellData(field, name);
    }

    /*!
     * \brief Finalizes the current writer.
     *
     * This means that everything will be written to disk.
     */
    void endTimestep(bool onlyDiscard=false)
    {
    	if (!onlyDiscard) {
    		curWriter_->write(curOutFileName_.c_str(),
							  Dune::VTKOptions::ascii);

    		// determine name to write into the multi-file for the
            // current time step
            std::string fileName;
            std::string suffix = fileSuffix_();
            if (commSize_ == 1) {
                fileName = curOutFileName_;
                multiFile_ << (boost::format("   <DataSet timestep=\"%lg\" file=\"%s.%s\"/>\n")
                               %curTime_%fileName%suffix);
            }
            if (commSize_ > 1 && commRank_ == 0)  {
                // only the first process updates the multi-file
                for (int part=0; part < commSize_; ++part) {
                    fileName = fileName_(part);
                    multiFile_ << (boost::format("   <DataSet part=\"%d\" timestep=\"%lg\" file=\"%s.%s\"/>\n")
                                   %part%curTime_%fileName%suffix);
                }
            }

    	}
    	else
    		-- writerNum_;

        delete curWriter_;
        while (vectorFields_.begin() != vectorFields_.end()) {
            delete vectorFields_.front();
            vectorFields_.pop_front();
        }

        // temporarily write the closing XML mumbo-jumbo to
        // the mashup file so that the data set can be loaded
        // even if the programm is aborted
        endMultiFile_();
    };

    /*!
     * \brief Write the multi-writer's state to a restart file.
     */
    template <class Restarter>
    void serialize(Restarter &res)
    {
        res.serializeSection("VTKMultiWriter");
        res.serializeStream() << writerNum_ - 1 << "\n";

        if (commRank_ == 0) {
            // write the meta file into the restart file
            size_t filePos = multiFile_.tellp();
            multiFile_.seekp(0, std::ios::end);
            size_t fileLen = multiFile_.tellp();
            multiFile_.seekp(filePos);

            res.serializeStream() << fileLen << "  " << filePos << "\n";

            std::ifstream multiFileIn(multiFileName_.c_str());
            char *tmp = new char[fileLen];
            multiFileIn.read(tmp, fileLen);
            res.serializeStream().write(tmp, fileLen);
            delete[] tmp;
        }
    }

    /*!
     * \brief Read the multi-writer's state from a restart file.
     */
    template <class Restarter>
    void deserialize(Restarter &res)
    {
        wasRestarted_ = true;

        res.deserializeSection("VTKMultiWriter");
        res.deserializeStream() >> writerNum_;

        std::string dummy;
        std::getline(res.deserializeStream(), dummy);

        if (commRank_ == 0) {
            // recreate the meta file from the restart file
            size_t filePos, fileLen;
            res.deserializeStream() >> fileLen >> filePos;
            std::getline(res.deserializeStream(), dummy);

            multiFile_.close();
            multiFile_.open(multiFileName_.c_str());

            char *tmp = new char[fileLen];
            res.deserializeStream().read(tmp, fileLen);
            multiFile_.write(tmp, fileLen);
            delete[] tmp;

            multiFile_.seekp(filePos);
        }
    }


private:
    std::string fileName_()
    {
        return (boost::format("%s-%05d")
                %simName_%writerNum_).str();
    }

    std::string fileName_(int rank)
    {
        if (commSize_ > 1) {
            return (boost::format("s%04d:p%04d:%s-%05d")
                    %commSize_
                    %rank
                    %simName_
                    %writerNum_).str();
        }
        else {
            return (boost::format("%s-%05d")
                    %simName_%writerNum_).str();
        }
    }

    std::string fileSuffix_()
    {
        return (GridView::dimension == 1)?"vtp":"vtu";
    }


    void beginMultiFile_(const std::string &multiFileName)
    {
        // if the multi writer was deserialized from a restart file,
        // we don't create a new multi file, but recycle the old one..
        if (wasRestarted_)
            return;

        // only the first process writes to the multi-file
        if (commRank_ == 0) {
            // generate one meta vtk-file holding the individual timesteps
            multiFile_.open(multiFileName.c_str());
            multiFile_ <<
                "<?xml version=\"1.0\"?>\n"
                "<VTKFile type=\"Collection\"\n"
                "         version=\"0.1\"\n"
                "         byte_order=\"LittleEndian\"\n"
                "         compressor=\"vtkZLibDataCompressor\">\n"
                " <Collection>\n";
        }
    }

    void endMultiFile_()
    {
        // only the first process writes to the multi-file
        if (commRank_ == 0) {
            // make sure that we always have a working meta file
            std::ofstream::pos_type pos = multiFile_.tellp();
            multiFile_ <<
                " </Collection>\n"
                "</VTKFile>\n";
            multiFile_.seekp(pos);
            multiFile_.flush();
        }
    }

    //////////////////////////////
    // HACK: when ever we attach some data we need to copy the
    //       vector field (that's because Dune::VTKWriter is not
    //       able to write fields one at a time and using
    //       VTKWriter::add*Data doesn't copy the data's
    //       representation so that once we arrive at the point
    //       where we want to write the data to disk, it might
    //       exist anymore). The problem we encounter there is
    //       that add*Data accepts arbitrary types as vector
    //       fields, but we need a single type for the linked list
    //       which keeps track of the data added. The trick we use
    //       here is to define a non-template base class with a
    //       virtual destructor for the type given to the linked
    //       list and a derived template class which actually
    //       knows the type of the vector field it must delete.

    /** \todo Please doc me! */

    class VtkVectorFieldStoreBase_
    {
    public:
        virtual ~VtkVectorFieldStoreBase_()
        {}
    };

    /** \todo Please doc me! */

    template <class VF>
    class VtkVectorFieldStoreImpl_ : public VtkVectorFieldStoreBase_
    {
    public:
        VtkVectorFieldStoreImpl_(int size)
            : vf(size)
        { }
        VF vf;
    };
    // end hack
    ////////////////////////////////////

    bool wasRestarted_;

    std::string     simName_;
    std::ofstream   multiFile_;
    std::string     multiFileName_;

    int commSize_; // number of processes in the communicator
    int commRank_; // rank of the current process in the communicator

    VtkWriter     * curWriter_;
    double          curTime_;
    const GridView* curGridView_;
    std::string     curOutFileName_;
    int             writerNum_;

    std::list<VtkVectorFieldStoreBase_*> vectorFields_;
};
}

#endif
