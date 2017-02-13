#include "partition/graph_view.hpp"
#include <iterator>

namespace osrm
{
namespace partition
{

HasSamePartitionID::HasSamePartitionID(const RecursiveBisectionState::BisectionID bisection_id_,
                                       const BisectionGraph &bisection_graph_,
                                       const RecursiveBisectionState &recursive_bisection_state_)
    : bisection_id(bisection_id_), bisection_graph(bisection_graph_),
      recursive_bisection_state(recursive_bisection_state_)
{
}

bool HasSamePartitionID::operator()(const EdgeID eid) const
{
    return recursive_bisection_state.GetBisectionID(bisection_graph.GetTarget(eid)) == bisection_id;
}

GraphView::GraphView(const BisectionGraph &bisection_graph_,
                     const RecursiveBisectionState &bisection_state_,
                     const RecursiveBisectionState::IDIterator begin_,
                     const RecursiveBisectionState::IDIterator end_)
    : bisection_graph(bisection_graph_), bisection_state(bisection_state_), begin(begin_), end(end_)
{
    // print graph
    std::cout << "Graph\n";
    for( auto itr = begin_; itr != end_; ++itr )
    {
        std::cout << "Node: " << *itr << std::endl;
        for( auto eitr = EdgeBegin(*itr); eitr != EdgeEnd(*itr); ++eitr )
        {
            std::cout << "\t" << *eitr << " -> " << bisection_graph.GetTarget(*eitr) << std::endl;
        }
    }
}

RecursiveBisectionState::IDIterator GraphView::Begin() const { return begin; }

RecursiveBisectionState::IDIterator GraphView::End() const { return end; }

std::size_t GraphView::NumberOfNodes() const { return std::distance(begin, end); }

GraphView::EdgeIterator GraphView::EdgeBegin(const NodeID nid) const
{
    return boost::make_filter_iterator(
        HasSamePartitionID(bisection_state.GetBisectionID(nid),
                           bisection_graph,
                           bisection_state),
        EdgeIDIterator(bisection_graph.BeginEdges(nid)),
        EdgeIDIterator(bisection_graph.EndEdges(nid)));
}

GraphView::EdgeIterator GraphView::EdgeEnd(const NodeID nid) const
{
    return boost::make_filter_iterator(
        HasSamePartitionID(bisection_state.GetBisectionID(nid), bisection_graph, bisection_state),
        EdgeIDIterator(bisection_graph.EndEdges(nid)),
        EdgeIDIterator(bisection_graph.EndEdges(nid)));
}

} // namespace partition
} // namespace osrm
