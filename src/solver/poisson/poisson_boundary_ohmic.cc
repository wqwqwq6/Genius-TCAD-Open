/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/

//  $Id: poisson_boundary_ohmic.cc,v 1.12 2008/07/09 07:53:36 gdiso Exp $


#include "simulation_system.h"
#include "semiconductor_region.h"
#include "conductor_region.h"
#include "insulator_region.h"
#include "boundary_condition_ohmic.h"

using PhysicalUnit::kb;
using PhysicalUnit::e;


/*---------------------------------------------------------------------
 * set scaling constant
 */
void OhmicContactBC::Poissin_Fill_Value(Vec , Vec L)
{

  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // typedef std::multimap<SimulationRegionType, std::pair<SimulationRegion *, FVM_Node *> >::iterator region_node_iterator;
    // search all the regions that has *node_it
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin ( *node_it );
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end ( *node_it );
    for ( ; rnode_it!=end_rnode_it; ++rnode_it )
    {
      const SimulationRegion * region = ( *rnode_it ).second.first;
      switch ( region->type() )
      {
          case SemiconductorRegion:
          {
            const FVM_Node * fvm_node = ( *rnode_it ).second.second;
            VecSetValue(L, fvm_node->global_offset(), 1.0, INSERT_VALUES);
            break;
          }

          case MetalRegion:
          case ElectrodeRegion :
          {
            const FVM_Node * fvm_node = ( *rnode_it ).second.second;
            VecSetValue(L, fvm_node->global_offset(), 1.0, INSERT_VALUES);
            break;
          }

          case InsulatorRegion:
          {
            const FVM_Node * fvm_node = ( *rnode_it ).second.second;
            VecSetValue(L, fvm_node->global_offset(), 1.0, INSERT_VALUES);
            break;
          }
          default: genius_error();
      }
    }
  }

}

///////////////////////////////////////////////////////////////////////
//----------------Function and Jacobian evaluate---------------------//
///////////////////////////////////////////////////////////////////////



/*---------------------------------------------------------------------
 * do pre-process to function for poisson solver
 */
void OhmicContactBC::Poissin_Function_Preprocess(PetscScalar *, Vec f, std::vector<PetscInt> &src_row,
    std::vector<PetscInt> &dst_row, std::vector<PetscInt> &clear_row)
{
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // search all the fvm_node which has *node_it as root node
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);

    // should clear all the rows related with ohmic boundary condition
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node *  fvm_node = (*rnode_it).second.second ;
      switch ( region->type() )
      {
        case SemiconductorRegion:
        {
          PetscInt row = fvm_node->global_offset();
          clear_row.push_back(row);
          break;
        }
        case MetalRegion:
        case ElectrodeRegion:
        case InsulatorRegion:
        {
          PetscInt row = fvm_node->global_offset();
          clear_row.push_back(row);
          break;
        }
        case VacuumRegion:
          break;
        default: genius_error(); //we should never reach here
      }
    }
  }


}

/*---------------------------------------------------------------------
 * build function and its jacobian for poisson solver
 */
void OhmicContactBC::Poissin_Function(PetscScalar * x, Vec f, InsertMode &add_value_flag)
{
  // Ohmic boundary condition is processed here

  // note, we will use ADD_VALUES to set values of vec f
  // if the previous operator is not ADD_VALUES, we should assembly the vec
  if( (add_value_flag != ADD_VALUES) && (add_value_flag != NOT_SET_VALUES) )
  {
    VecAssemblyBegin(f);
    VecAssemblyEnd(f);
  }

  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // buffer for saving regions and fvm_nodes this *node_it involves
    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node, these fvm_nodes have the same location in geometry,
    // but belong to different regions in logic.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      regions.push_back( (*rnode_it).second.first );
      fvm_nodes.push_back( (*rnode_it).second.second );

      const FVM_NodeData * node_data = fvm_nodes[i]->node_data();

      switch ( regions[i]->type() )
      {
          // Semiconductor Region of course owns OhmicContactBC
          case SemiconductorRegion:
          {
            // semiconductor region should be the first region
            const SemiconductorSimulationRegion * semi_region = dynamic_cast<const SemiconductorSimulationRegion *>(regions[i]);
            // psi of this node
            PetscScalar V = x[fvm_nodes[i]->local_offset()];

            PetscScalar T = T_external();
            semi_region->material()->mapping(fvm_nodes[i]->root_node(), node_data, 0.0);
            PetscScalar ni  = semi_region->material()->band->ni(T);

            PetscScalar Nc  = node_data->Nc();
            PetscScalar Nv  = node_data->Nv();
            PetscScalar Eg  = semi_region->material()->band->Eg(T);
            // intrinsic Fermi potential.
            PetscScalar V_i =  V + node_data->affinity()/e + Eg/(2*e) + kb*T*log(Nc/Nv)/(2*e);
            // electron density
            PetscScalar n    =  ni*exp(e/(kb*T)*V_i);
            // hole density
            PetscScalar p    =  ni*exp(-e/(kb*T)*V_i);

            // consider bandgap narrow
            PetscScalar nie = semi_region->material()->band->nie(p, n, T);

            //governing equation for Ohmic contact boundary
            PetscScalar ff = V - kb*T/e*asinh(node_data->Net_doping()/(2*nie))
                             + Eg/(2*e)
                             + kb*T*log(Nc/Nv)/(2*e)
                             + node_data->affinity()/e
                             - ext_circuit()->Vapp();

            // set governing equation to function vector
            VecSetValue(f, fvm_nodes[i]->global_offset(), ff, ADD_VALUES);
            break;
          }
          // insulator region. if a corner where semiconductor region, insulator region and  conductor region meet.
          // the boundary for the corner point may be Ohmic. (not a nice behavier)
          case InsulatorRegion:
          {
            // psi of this node
            PetscScalar V = x[fvm_nodes[i]->local_offset()];

            // since the region is sorted, we know region[0] is semiconductor region
            // as a result, x[fvm_nodes[0]->local_offset()] is psi for corresponding semiconductor region
            //genius_assert( regions[0]->type()==SemiconductorRegion );
            PetscScalar V_ref = x[fvm_nodes[0]->local_offset()];

            // the psi of this node is equal to corresponding psi of semiconductor node
            PetscScalar ff = V - V_ref;
            // set governing equation to function vector
            VecSetValue(f, fvm_nodes[i]->global_offset(), ff, ADD_VALUES);

            break;
          }
          // conductor region which has an interface with OhmicContact boundary to semiconductor region
          case MetalRegion:
          case ElectrodeRegion:
          {

            // psi of this node
            PetscScalar V = x[fvm_nodes[i]->local_offset()];

            // since the region is sorted, we know region[0] is semiconductor region
            // as a result, x[fvm_nodes[0]->local_offset()] is psi for corresponding semiconductor region
            //genius_assert( regions[0]->type()==SemiconductorRegion );
            PetscScalar V_ref = x[fvm_nodes[0]->local_offset()];

            // the psi of this node is equal to corresponding psi of semiconductor node
            PetscScalar ff = V - V_ref;
            // set governing equation to function vector
            VecSetValue(f, fvm_nodes[i]->global_offset(), ff, ADD_VALUES);

            break;
          }

          case VacuumRegion: break;
          default: genius_error(); //we should never reach here
      }
    }

  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif
}




/*---------------------------------------------------------------------
 * do pre-process to jacobian matrix for poisson solver
 */
void OhmicContactBC::Poissin_Jacobian_Preprocess(PetscScalar *, SparseMatrix<PetscScalar> *jac, std::vector<PetscInt> &src_row,
    std::vector<PetscInt> &dst_row, std::vector<PetscInt> &clear_row)
{
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // search all the fvm_node which has *node_it as root node
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);

    // should clear all the rows related with ohmic boundary condition
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node *  fvm_node = (*rnode_it).second.second ;
      switch ( region->type() )
      {
        case SemiconductorRegion:
        {
          PetscInt row = fvm_node->global_offset();
          clear_row.push_back(row);
          break;
        }
        case MetalRegion:
        case ElectrodeRegion:
        case InsulatorRegion:
        {
          PetscInt row = fvm_node->global_offset();
          clear_row.push_back(row);
          break;
        }
        case VacuumRegion:
          break;
          default: genius_error(); //we should never reach here
      }
    }
  }


}

/*---------------------------------------------------------------------
 * build function and its jacobian for poisson solver
 */
void OhmicContactBC::Poissin_Jacobian(PetscScalar * x, SparseMatrix<PetscScalar> *jac, InsertMode &add_value_flag)
{
  // the Jacobian of Ohmic boundary condition is processed here
  // we use AD again. no matter it is overkill here.

  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(node_it = nodes_begin(); node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // buffer for saving regions and fvm_nodes this *node_it involves
    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node, these fvm_nodes have the same location in geometry,
    // but belong to different regions in logic.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      regions.push_back( (*rnode_it).second.first );
      fvm_nodes.push_back( (*rnode_it).second.second );

      const FVM_NodeData * node_data = fvm_nodes[i]->node_data();

      switch ( regions[i]->type() )
      {
          // Semiconductor Region of course owns OhmicContactBC
          case SemiconductorRegion:
          {
            // semiconductor region should be the first region

            //the indepedent variable number, we only need 1 here.
            adtl::AutoDScalar::numdir=1;

            const SemiconductorSimulationRegion * semi_region = dynamic_cast<const SemiconductorSimulationRegion *>(regions[i]);
            // psi of this node
            AutoDScalar V = x[fvm_nodes[i]->local_offset()]; V.setADValue(0,1.0);

            PetscScalar T = T_external();
            semi_region->material()->mapping(fvm_nodes[i]->root_node(), node_data, 0.0);
            PetscScalar ni  = semi_region->material()->band->ni(T);

            PetscScalar Nc  = node_data->Nc();
            PetscScalar Nv  = node_data->Nv();
            PetscScalar Eg  = semi_region->material()->band->Eg(T);

            // intrinsic Fermi potential.
            AutoDScalar V_i =  V + node_data->affinity()/e + Eg/(2*e) + kb*T*log(Nc/Nv)/(2*e);
            // electron density
            AutoDScalar n    =  ni*exp(e/(kb*T)*V_i);
            // hole density
            AutoDScalar p    =  ni*exp(-e/(kb*T)*V_i);

            // consider bandgap narrow
            AutoDScalar nie = semi_region->material()->band->nie(p, n, T);

            //governing equation for Ohmic contact boundary
            AutoDScalar ff = V - kb*T/e*asinh(node_data->Net_doping()/(2*nie))
                             + Eg/(2*e)
                             + kb*T*log(Nc/Nv)/(2*e)
                             + node_data->affinity()/e
                             - ext_circuit()->Vapp();

            // set Jacobian of governing equation ff
            jac->add(fvm_nodes[i]->global_offset(), fvm_nodes[i]->global_offset(), ff.getADValue(0));
            break;
          }
          // insulator region. if a corner where semiconductor region, insulator region and  conductor region meet.
          // the boundary for the corner point may be Ohmic.
          case InsulatorRegion:
          {
            //the indepedent variable number, we need 2 here.
            adtl::AutoDScalar::numdir=2;

            // psi of this node
            AutoDScalar  V = x[fvm_nodes[i]->local_offset()]; V.setADValue(0,1.0);

            // since the region is sorted, we know region[0] is semiconductor region
            // as a result, x[fvm_nodes[0]->local_offset()] is psi for corresponding semiconductor region
            //genius_assert( regions[0]->type()==SemiconductorRegion );
            AutoDScalar  V_ref = x[fvm_nodes[0]->local_offset()]; V_ref.setADValue(1,1.0);

            // the psi of this node is equal to corresponding psi of semiconductor node
            AutoDScalar  ff = V - V_ref;

            // set Jacobian of governing equation ff
            jac->add(fvm_nodes[i]->global_offset(), fvm_nodes[i]->global_offset(), ff.getADValue(0));
            jac->add(fvm_nodes[i]->global_offset(), fvm_nodes[0]->global_offset(), ff.getADValue(1));

            break;
          }
          // conductor region which has an interface with OhmicContact boundary to semiconductor region
          case MetalRegion:
          case ElectrodeRegion:
          {
            //the indepedent variable number, we need 2 here.
            adtl::AutoDScalar::numdir=2;

            // psi of this node
            AutoDScalar  V = x[fvm_nodes[i]->local_offset()]; V.setADValue(0,1.0);

            // since the region is sorted, we know region[0] is semiconductor region
            // as a result, x[fvm_nodes[0]->local_offset()] is psi for corresponding semiconductor region
            //genius_assert( regions[0]->type()==SemiconductorRegion );
            AutoDScalar  V_ref = x[fvm_nodes[0]->local_offset()]; V_ref.setADValue(1,1.0);

            // the psi of this node is equal to corresponding psi of semiconductor node
            AutoDScalar  ff = V - V_ref;

            // set Jacobian of governing equation ff
            jac->add(fvm_nodes[i]->global_offset(), fvm_nodes[i]->global_offset(), ff.getADValue(0));
            jac->add(fvm_nodes[i]->global_offset(), fvm_nodes[0]->global_offset(), ff.getADValue(1));

            break;
          }

          case VacuumRegion:
          break;
          default: genius_error(); //we should never reach here
      }
    }

  }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;

}



/*---------------------------------------------------------------------
 * update electrode potential
 */
void OhmicContactBC::Poissin_Update_Solution(PetscScalar *)
{
  this->ext_circuit()->potential() = this->ext_circuit()->Vapp();
}
