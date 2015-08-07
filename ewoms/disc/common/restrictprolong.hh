#ifndef EWOMS_COPYRESTRICTPROLONG_HH
#define EWOMS_COPYRESTRICTPROLONG_HH

namespace Ewoms
{
    template< class Grid, class Container >
    class CopyRestrictProlong
    {
      typedef CopyRestrictProlong< Grid, Container > ThisType;

      Container& container_;
    public:
      explicit CopyRestrictProlong( Container& container )
        : container_( container )
      {}

      /** \brief explicit set volume ratio of son and father
       *
       *  \param[in]  weight  volume of son / volume of father
       *
       *  \note If this ratio is set, it is assume to be constant.
       */
      template <class Field>
      void setFatherChildWeight ( const Field &weight ) const
      {
      }

      //! restrict data to father
      template< class Entity >
      void restrictLocal ( const Entity &father, const Entity &son, bool initialize ) const
      {
        container_.resize();
        assert( container_.codimension() == 0 );
        if( initialize )
        {
          // copy values from son to father
          container_[ father ] = container_[ son ];
        }
      }

      //! restrict data to father
      template< class Entity, class LocalGeometry >
      void restrictLocal ( const Entity &father, const Entity &son,
                           const LocalGeometry &geometryInFather,
                           bool initialize ) const
      {
        restrictLocal( father, son, initialize );
      }

      //! prolong data to children
      template< class Entity >
      void prolongLocal ( const Entity &father, const Entity &son, bool initialize ) const
      {
        container_.resize();
        assert( container_.codimension() == 0 );
        if( initialize )
        {
          // copy values from father to son
          container_[ son ] = container_[ father ];
        }
      }

      //! prolong data to children
      template< class Entity, class LocalGeometry >
      void prolongLocal ( const Entity &father, const Entity &son,
                          const LocalGeometry &geometryInFather,
                          bool initialize ) const
      {
        prolongLocal( father, son, initialize );
      }

      /** \brief add discrete function to communicator
       *  \param[in]  comm  Communicator to add the discrete functions to
       */
      template< class Communicator >
      void addToList ( Communicator &comm )
      {
        // TODO
      }

      /** \brief add discrete function to load balancer
       *  \param[in]  lb LoadBalancer to add the discrete functions to
       */
      template< class LoadBalancer >
      void addToLoadBalancer ( LoadBalancer &lb )
      {
        // TODO
      }

    };

    class EmptyRestrictProlong
    {
      typedef EmptyRestrictProlong ThisType;

    public:
      /** \brief explicit set volume ratio of son and father
       *
       *  \param[in]  weight  volume of son / volume of father
       *
       *  \note If this ratio is set, it is assume to be constant.
       */
      template <class Field>
      void setFatherChildWeight ( const Field &weight ) const
      {
      }

      //! restrict data to father
      template< class Entity >
      void restrictLocal ( const Entity &father, const Entity &son, bool initialize ) const
      {
      }

      //! restrict data to father
      template< class Entity, class LocalGeometry >
      void restrictLocal ( const Entity &father, const Entity &son,
                           const LocalGeometry &geometryInFather,
                           bool initialize ) const
      {
      }

      //! prolong data to children
      template< class Entity >
      void prolongLocal ( const Entity &father, const Entity &son, bool initialize ) const
      {
      }

      //! prolong data to children
      template< class Entity, class LocalGeometry >
      void prolongLocal ( const Entity &father, const Entity &son,
                          const LocalGeometry &geometryInFather,
                          bool initialize ) const
      {
      }

      /** \brief add discrete function to communicator
       *  \param[in]  comm  Communicator to add the discrete functions to
       */
      template< class Communicator >
      void addToList ( Communicator &comm )
      {
      }

      /** \brief add discrete function to load balancer
       *  \param[in]  lb LoadBalancer to add the discrete functions to
       */
      template< class LoadBalancer >
      void addToLoadBalancer ( LoadBalancer &lb )
      {
      }
    };

} // namespace Ewoms

#endif
