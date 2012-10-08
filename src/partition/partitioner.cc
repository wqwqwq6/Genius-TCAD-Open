// $Id: partitioner.cc,v 1.4 2008/05/22 04:53:44 gdiso Exp $

// The libMesh Finite Element Library.
// Copyright (C) 2002-2007  Benjamin S. Kirk, John W. Peterson

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



// C++ Includes   -----------------------------------


// Local Includes -----------------------------------
#include "mesh_base.h"
#include "partitioner.h"
#include "mesh_base.h"
#include "perf_log.h"
#include "elem.h"


#if defined(HAVE_TR1_UNORDERED_SET)
#include <tr1/unordered_set>
#elif defined(HAVE_TR1_UNORDERED_SET_WITH_STD_HEADER) || defined(HAVE_UNORDERED_SET)
#include <unordered_set>
#endif


Partitioner::Partitioner()
{}


Partitioner::~Partitioner()
{
  this->_clear_cluster();
}


// ------------------------------------------------------------
// Partitioner implementation


void Partitioner::partition (MeshBase& mesh, const std::vector<std::vector<unsigned int> > *cluster, const unsigned int n)
{
  // Set the number of partitions in the mesh
  mesh.set_n_partitions()=n;

  // clear cluster if data exist
  this->_clear_cluster();

  this->_build_flat_cluster(mesh);

  this->_merge_elem_to_cluster(mesh, cluster);

  // Call the partitioning function
  this->_do_partition(mesh,n);

  // clear cluster for saving memory
  this->_clear_cluster();

  // Set the node's processor ids
  this->_set_node_processor_ids(mesh);
}





void Partitioner::repartition(MeshBase& mesh, const std::vector<std::vector<unsigned int> > *cluster, const unsigned int n)
{
  // Set the number of partitions in the mesh
  mesh.set_n_partitions()=n;

  // clear cluster if data exist
  this->_clear_cluster();

  this->_build_flat_cluster(mesh);

  this->_merge_elem_to_cluster(mesh, cluster);

  // Call the partitioning function
  this->_do_repartition(mesh,n);

  // clear cluster for saving memory
  this->_clear_cluster();

  // Set the node's processor ids
  this->_set_node_processor_ids(mesh);
}





void Partitioner::single_partition (MeshBase& mesh)
{
  START_LOG("partition()", "Single Partitioner");

  // Loop over all the elements and assign them to processor 0.
  MeshBase::element_iterator       elem_it  = mesh.elements_begin();
  const MeshBase::element_iterator elem_end = mesh.elements_end();

  for ( ; elem_it != elem_end; ++elem_it)
  {
    (*elem_it)->processor_id() = 0;
    (*elem_it)->on_local() = true;
  }

  // For a single partition, all the nodes are on processor 0
  MeshBase::node_iterator       node_it  = mesh.nodes_begin();
  const MeshBase::node_iterator node_end = mesh.nodes_end();

  for ( ; node_it != node_end; ++node_it)
  {
    (*node_it)->processor_id() = 0;
    (*node_it)->on_local() = true;
  }

  STOP_LOG("partition()", "Single Partitioner");
}




void Partitioner::_set_node_processor_ids(MeshBase& mesh)
{
  START_LOG("set_node_processor_ids()", "Partitioner");

  // Unset any previously-set node processor ids
  // (maybe from previous partitionings).
  MeshBase::node_iterator       node_it  = mesh.nodes_begin();
  const MeshBase::node_iterator node_end = mesh.nodes_end();

  for ( ; node_it != node_end; ++node_it)
  {
    (*node_it)->invalidate_processor_id();
    (*node_it)->on_local() = false;
  }

  // Loop over all the elements
  MeshBase::element_iterator       elem_it  = mesh.elements_begin();
  const MeshBase::element_iterator elem_end = mesh.elements_end();

  for ( ; elem_it != elem_end; ++elem_it)
  {
    Elem* elem = *elem_it;
    elem->on_local() = false;

    // For each node, set the processor ID to the min of
    // its current value and this Element's processor id.
    for (unsigned int n=0; n<elem->n_nodes(); ++n)
      elem->get_node(n)->processor_id() = std::min(elem->get_node(n)->processor_id(),
                                          elem->processor_id());
  }

  // elem has on processor node
#if defined(HAVE_UNORDERED_SET)
  std::unordered_set<const Elem *> elem_has_on_process_node;
#elif defined(HAVE_TR1_UNORDERED_SET) || defined(HAVE_TR1_UNORDERED_SET_WITH_STD_HEADER)
  std::tr1::unordered_set<const Elem *> elem_has_on_process_node;
#else
  std::set<const Elem *> elem_has_on_process_node;
#endif
  for (elem_it  = mesh.elements_begin() ; elem_it != elem_end; ++elem_it)
  {
    const Elem* elem = *elem_it;
    for( unsigned int n=0; n<elem->n_nodes(); n++ )
      if( elem->get_node(n)->processor_id() == Genius::processor_id() )
      { elem_has_on_process_node.insert(elem); break; }
  }


  // now we determine local elements and nodes

  for (elem_it  = mesh.elements_begin() ; elem_it != elem_end; ++elem_it)
  {
    Elem* elem = *elem_it;

    // the element is belongs to this processor
    if(elem->processor_id() == Genius::processor_id())
      goto on_local;

    // otherwise, it has at lease one node belongs to this processor
    // or my neighbor on this processor
    else
    {
      if( elem_has_on_process_node.find(elem) != elem_has_on_process_node.end() )
        goto on_local;
    }

    // also, if elem has a on_local neighbor
    for( unsigned int s=0; s<elem->n_sides(); s++ )
    {
      Elem* neighbor_elem = elem->neighbor(s);
      if(!neighbor_elem) continue;

      if(neighbor_elem->processor_id() == Genius::processor_id())
        goto on_local;
      else
      {
        if( elem_has_on_process_node.find(neighbor_elem) != elem_has_on_process_node.end() )
          goto on_local;
      }
    }

    continue;

  on_local:
    {
      elem->on_local() = true;
      // if we find it, make all its node on_local
      for( unsigned int n=0; n<elem->n_nodes(); n++ )
        elem->get_node(n)->on_local() = true;
    }
  }


#if 0
  MeshBase::element_iterator       el  = mesh.local_elements_begin();
  const MeshBase::element_iterator end = mesh.local_elements_end();
  for (; el != end; ++el)
  {
    Elem * elem = *el;
    genius_assert(elem->on_local());

    for (unsigned int n=0; n<elem->n_nodes(); n++)
    {
      Node * node = elem->get_node(n);
      genius_assert( node->on_local() );
    }
  }
#endif

  STOP_LOG("set_node_processor_ids()", "Partitioner");
}



void Partitioner::_build_flat_cluster(MeshBase& mesh)
{
  START_LOG("build_flat_cluster()", "Partitioner");

  MeshBase::element_iterator       elem_it  = mesh.active_elements_begin();
  const MeshBase::element_iterator elem_end = mesh.active_elements_end();

  _elem_cluster_map.resize(mesh.n_elem(), 0);

  for (; elem_it != elem_end; ++elem_it)
  {
    const Elem* elem = *elem_it;

    Cluster * cluster = new Cluster;

    cluster->id = elem->id();
    cluster->elems.push_back(elem);

    _clusters.push_back(cluster);
    _elem_cluster_map[elem->id()] = cluster;
  } // end for elem_it

  STOP_LOG("build_flat_cluster()", "Partitioner");
}



void Partitioner::_merge_elem_to_cluster(MeshBase& mesh, const std::vector<std::vector<unsigned int> > *cluster_elems)
{

  if(!cluster_elems) return;

  START_LOG("merge_elem_to_cluster()", "Partitioner");

  // create new cluster
  std::vector<Cluster *> new_clusters;
  std::map<const Elem *, const Cluster *> new_elem_cluster_map;
  for(unsigned int n=0; n<cluster_elems->size(); ++n)
  {
    // create new Cluster
    Cluster * cluster = new Cluster;
    // set the elems of this cluster
    for(unsigned int m=0; m<(*cluster_elems)[n].size(); ++m)
      cluster->elems.push_back(mesh.elem((*cluster_elems)[n][m]));
    // give a unique id to new cluster
    cluster->id = _clusters.size() + n;
    new_clusters.push_back(cluster);
    // build elem to cluster map
    for(unsigned int m=0; m<cluster->elems.size(); ++m)
      new_elem_cluster_map.insert(std::make_pair(cluster->elems[m], cluster));
  }

  // replace old (flat) cluster by new cluster
  std::map<const Elem *, const Cluster *>::iterator elem_cluster_map_it = new_elem_cluster_map.begin();
  for( ;elem_cluster_map_it != new_elem_cluster_map.end(); ++elem_cluster_map_it)
  {
    const Elem * elem = elem_cluster_map_it->first;
    const Cluster * cluster = elem_cluster_map_it->second;
    const Cluster * old_cluster = _elem_cluster_map[elem->id()];
    delete old_cluster;
    _elem_cluster_map[elem->id()] = cluster;
  }

  // can be free here
  new_elem_cluster_map.clear();

  // rebuild _clusters array, the order should keeps the same between processors
  std::map<unsigned int, Cluster *> _cluster_map; //order the cluster by its id
  for( unsigned int n = 0; n < _elem_cluster_map.size(); ++n)
  {
    if(!_elem_cluster_map[n]) continue; // _elem_cluster_map may have empty entries!
    Cluster * cluster = const_cast<Cluster *>(_elem_cluster_map[n]);
    _cluster_map.insert(std::make_pair(cluster->id, cluster));
  }

  _clusters.clear();
  std::map<unsigned int, Cluster *>::iterator  cluster_map_it = _cluster_map.begin();
  for( ; cluster_map_it != _cluster_map.end(); ++cluster_map_it)
    _clusters.push_back(cluster_map_it->second);

  // assign new id to cluster
  for(unsigned int n=0; n<_clusters.size(); ++n)
    _clusters[n]->id = n;

  STOP_LOG("merge_elem_to_cluster()", "Partitioner");
}




void Partitioner::_clear_cluster()
{
  for(unsigned int n=0; n<_clusters.size(); ++n)
    delete _clusters[n];
  _clusters.clear();

  _elem_cluster_map.clear();
}



#if 0
std::vector<const Elem *> Partitioner::cluster_neighbor_elem(const Cluster * cluster) const
{
  std::set<const Elem *> elements;

  for(unsigned int m=0; m<cluster->elems.size(); ++m)
  {
    const Elem * elem = cluster->elems[m];
    // Loop over the element's neighbors.  An element
    // adjacency corresponds to a face neighbor
    for (unsigned int ms=0; ms<elem->n_neighbors(); ms++)
    {
      const Elem* neighbor = elem->neighbor(ms);

      if (neighbor != NULL)
      {
        // If the neighbor is active treat it
        // as a connection
        if (neighbor->active())
        {
          if( elements.find(neighbor) != elements.end() )
            elements.erase(neighbor);
          else
            elements.insert(neighbor);
        }

#ifdef ENABLE_AMR

        // Otherwise we need to find all of the
        // neighbor's children that are connected to
        // us and add them
        else
        {
          // The side of the neighbor to which
          // we are connected
          const unsigned int ns = neighbor->which_neighbor_am_i (elem);

          // Get all the active children (& grandchildren, etc...)
          // of the neighbor.
          std::vector<const Elem*> neighbors_offspring;
          neighbor->active_family_tree (neighbors_offspring);

          // Get all the neighbor's children that
          // live on that side and are thus connected
          // to us
          for (unsigned int nc=0; nc<neighbors_offspring.size(); nc++)
          {
            const Elem* child = neighbors_offspring[nc];

            // This does not assume a level-1 mesh.
            // Note that since children have sides numbered
            // coincident with the parent then this is a sufficient test.
            if (child->neighbor(ns) == elem)
            {
              if( elements.find(child) != elements.end() )
                elements.erase(child);
              else
                elements.insert(child);
            }
          }
        }

#endif /* ifdef ENABLE_AMR */

      }
    } // end for neighbor
  }

  // set neighbor info of this cluster
  //NOTE, cluster neighbor elem may belongs to the same cluster, we need to eliminate the duplicate neighbor
  std::set<const Cluster *> cluster_neighbor_set;
  std::vector<const Elem *> elems;
  std::set<const Elem *>::const_iterator it = elements.begin();
  for( ; it != elements.end(); ++it)
  {
    const Elem * elem = *it;
    const Cluster * cluster_neighbor = _elem_cluster_map[elem->id()];
    if( cluster_neighbor_set.find(cluster_neighbor) == cluster_neighbor_set.end() )
    {
      cluster_neighbor_set.insert(cluster_neighbor);
      elems.push_back(elem);
    }
  }


  // sort the nodes by ID
  DofObject::Less less;
  std::sort( elems.begin(), elems.end(), less );

  assert(elems.size() == cluster->neighbors.size());


  return elems;
}
#endif


std::vector<const Elem *> Partitioner::cluster_neighbor_elem(const Cluster * cluster) const
{

  // the elems belongs to this cluster
  std::set<const Elem *> cluster_elem;
  cluster_elem.insert(cluster->elems.begin(), cluster->elems.end());

  // the neighbor of this cluster, should ordered by id, then each processor has the same order!
  std::set<const Elem *> cluster_neighbor;

  // build cluster_neighbor
  for(unsigned int m=0; m<cluster->elems.size(); ++m)
  {
    const Elem * elem = cluster->elems[m];
    for (unsigned int ms=0; ms<elem->n_neighbors(); ms++)
    {
      const Elem* neighbor = elem->neighbor(ms);
      // neighbor should not be NULL and not belongs to this cluster
      if (neighbor != NULL && cluster_elem.find(neighbor)==cluster_elem.end())
      {
        // If the neighbor is active treat it
        // as a connection
        if (neighbor->active())
        {
          cluster_neighbor.insert(neighbor);
        }

#ifdef ENABLE_AMR

        // Otherwise we need to find all of the
        // neighbor's children that are connected to
        // us and add them
        else
        {
          // The side of the neighbor to which
          // we are connected
          const unsigned int ns = neighbor->which_neighbor_am_i (elem);

          // Get all the active children (& grandchildren, etc...)
          // of the neighbor.
          std::vector<const Elem*> neighbors_offspring;
          neighbor->active_family_tree (neighbors_offspring);

          // Get all the neighbor's children that
          // live on that side and are thus connected
          // to us
          for (unsigned int nc=0; nc<neighbors_offspring.size(); nc++)
          {
            const Elem* child = neighbors_offspring[nc];

            // This does not assume a level-1 mesh.
            // Note that since children have sides numbered
            // coincident with the parent then this is a sufficient test.
            if (child->neighbor(ns) == elem)
            {
              cluster_neighbor.insert(child);
            }
          }
        }

#endif /* ifdef ENABLE_AMR */

      }
    } // end for neighbor
  }

  // set neighbor info of this cluster
  std::vector<const Elem *> elems;

  //NOTE, cluster neighbor elem may belongs to the same cluster, we need to eliminate the duplicate neighbor
  std::set<const Cluster *> neighbor_set;
  std::set<const Elem *>::iterator set_it = cluster_neighbor.begin();
  for(; set_it != cluster_neighbor.end(); ++set_it)
  {
    const Elem * neighbor = (*set_it);
    const Cluster * neighbor_cluster = _elem_cluster_map[neighbor->id()];
    if(neighbor_set.find(neighbor_cluster) == neighbor_set.end())
      elems.push_back(neighbor);
    neighbor_set.insert(neighbor_cluster);
  }

  // sort the nodes by ID
  DofObject::Less less;
  std::sort( elems.begin(), elems.end(), less );

  return elems;

}


