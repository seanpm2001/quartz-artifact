#include "substitution.h"
#include "assert.h"

namespace TASOGraph {

OpX::OpX(const OpX &_op)
    : type(_op.type), mapOp(_op.mapOp), inputs(_op.inputs),
      outputs(_op.outputs) {}

OpX::OpX(::GateType _type) : type(_type) {}

void OpX::add_input(const TensorX &input) { inputs.push_back(input); }

void OpX::add_output(const TensorX &output) { outputs.push_back(output); }

GraphXfer::GraphXfer(::Context *_context) : context(_context), tensorId(10) {}

GraphXfer::GraphXfer(::Context *_context, const ::DAG *src_graph,
                     const ::DAG *dst_graph)
    : context(_context), tensorId(10) {
  assert(src_graph->get_num_qubits() == dst_graph->get_num_qubits());
  assert(src_graph->get_num_input_parameters() ==
         dst_graph->get_num_input_parameters());
  std::unordered_map<DAGNode *, TensorX> src_to_tx, dst_to_tx;
  int cnt = 0;
  for (int i = 0; i < src_graph->get_num_qubits(); i++) {
	::DAGNode *src_node = src_graph->nodes[cnt].get();
	::DAGNode *dst_node = dst_graph->nodes[cnt++].get();
	assert(src_node->is_qubit());
	assert(dst_node->is_qubit());
	assert(src_node->index == i);
	assert(dst_node->index == i);
	TensorX qubit_tensor = new_tensor();
	src_to_tx[src_node] = qubit_tensor;
	dst_to_tx[dst_node] = qubit_tensor;
  }
  for (int i = 0; i < src_graph->get_num_input_parameters(); i++) {
	::DAGNode *src_node = src_graph->nodes[cnt].get();
	::DAGNode *dst_node = dst_graph->nodes[cnt++].get();
	assert(src_node->is_parameter());
	assert(dst_node->is_parameter());
	assert(src_node->index == i);
	assert(dst_node->index == i);
	TensorX parameter_tensor = new_tensor();
	src_to_tx[src_node] = parameter_tensor;
	dst_to_tx[dst_node] = parameter_tensor;
  }
  for (size_t i = 0; i < src_graph->edges.size(); i++) {
	::DAGHyperEdge *e = src_graph->edges[i].get();
	OpX *op = new OpX(e->gate->tp);
	for (size_t j = 0; j < e->input_nodes.size(); j++) {
	  assert(src_to_tx.find(e->input_nodes[j]) != src_to_tx.end());
	  TensorX input = src_to_tx[e->input_nodes[j]];
	  op->add_input(input);
	}
	for (size_t j = 0; j < e->output_nodes.size(); j++) {
	  //   if (e->output_nodes[j]->is_qubit()) {
	  //     TensorX output(op, j);
	  //     op->add_output(output);
	  //     src_to_tx[e->output_nodes[j]] = output;
	  //   }
	  TensorX output(op, j);
	  op->add_output(output);
	  src_to_tx[e->output_nodes[j]] = output;
	}
	srcOps.push_back(op);
  }
  for (size_t i = 0; i < dst_graph->edges.size(); i++) {
	::DAGHyperEdge *e = dst_graph->edges[i].get();
	OpX *op = new OpX(e->gate->tp);
	for (size_t j = 0; j < e->input_nodes.size(); j++) {
	  TensorX input = dst_to_tx[e->input_nodes[j]];
	  op->add_input(input);
	}
	for (size_t j = 0; j < e->output_nodes.size(); j++) {
	  //   if (e->output_nodes[j]->is_qubit()) {
	  TensorX output(op, j);
	  op->add_output(output);
	  dst_to_tx[e->output_nodes[j]] = output;
	  //   }
	}
	dstOps.push_back(op);
  }
  for (int i = 0; i < src_graph->get_num_qubits(); i++) {
	assert(src_to_tx.find(src_graph->outputs[i]) != src_to_tx.end());
	assert(dst_to_tx.find(dst_graph->outputs[i]) != dst_to_tx.end());
	map_output(src_to_tx[src_graph->outputs[i]],
	           dst_to_tx[dst_graph->outputs[i]]);
  }
}

TensorX GraphXfer::new_tensor(void) {
  TensorX t;
  t.op = NULL;
  t.idx = tensorId++;
  return t;
}

bool GraphXfer::map_output(const TensorX &src, const TensorX &dst) {
  mappedOutputs[src] = dst;
  return true;
}

bool GraphXfer::can_match(OpX *srcOp, Op op, Graph *graph) {
  if (srcOp->type != op.ptr->tp)
	return false;
  // check num input tensors
  if ((int)srcOp->inputs.size() !=
      op.ptr->get_num_qubits() + op.ptr->get_num_parameters())
	return false;
  // check inputs
  std::map<int, std::pair<Op, int>> newMapInputs;
  for (size_t i = 0; i < srcOp->inputs.size(); i++) {
	TensorX in = srcOp->inputs[i];
	if (in.op == NULL) {
	  // input tensor
	  std::multimap<int, std::pair<Op, int>>::const_iterator it;
	  it = mappedInputs.find(in.idx);
	  if (it != mappedInputs.end()) {
		Op mappedOp = it->second.first;
		int mappedIdx = it->second.second;
		if (!(graph->has_edge(mappedOp, op, mappedIdx, i)))
		  return false;
	  }
	  else {
		std::map<int, std::pair<Op, int>>::const_iterator newit;
		newit = newMapInputs.find(in.idx);
		if (newit != newMapInputs.end()) {
		  Op mappedOp = newit->second.first;
		  int mappedIdx = newit->second.second;
		  if (!(graph->has_edge(mappedOp, op, mappedIdx, i)))
			return false;
		}
		else {
		  std::set<Edge, EdgeCompare> list = graph->inEdges.find(op)->second;
		  std::set<Edge, EdgeCompare>::const_iterator it2;
		  for (it2 = list.begin(); it2 != list.end(); it2++) {
			Edge e = *it2;
			if (e.dstIdx == (int)i) {
			  newMapInputs.insert(
			      std::make_pair(in.idx, std::make_pair(e.srcOp, e.srcIdx)));
			}
		  }
		}
		// Do nothing when we check the match
		/* mapped in.idx to an op
		std::set<Edge, EdgeCompare> list = graph->inEdges.find(op)->second;
		std::set<Edge, EdgeCompare>::const_iterator it2;
		for (it2 = list.begin(); it2 != list.end(); it2++) {
		  Edge e = *it2;
		  if (e.dstIdx == i)
		    mappedInputs[in.idx] = std::make_pair(e.srcOp, e.srcIdx);
		}*/
	  }
	}
	else {
	  // intermediate tensor
	  assert(in.op->mapOp.ptr != NULL);
	  if (!(graph->has_edge(in.op->mapOp, op, in.idx, i)))
		return false;
	}
  }
  return true;
}

void GraphXfer::match(OpX *srcOp, Op op, Graph *graph) {
  for (size_t i = 0; i < srcOp->inputs.size(); i++) {
	TensorX in = srcOp->inputs[i];
	if (in.op == NULL) {
	  // Update mappedInputs
	  std::set<Edge, EdgeCompare> list = graph->inEdges.find(op)->second;
	  std::set<Edge, EdgeCompare>::const_iterator it2;
	  for (it2 = list.begin(); it2 != list.end(); it2++) {
		Edge e = *it2;
		if (e.dstIdx == (int)i) {
		  mappedInputs.insert(
		      std::make_pair(in.idx, std::make_pair(e.srcOp, e.srcIdx)));
		}
	  }
	}
  }
  // Map srcOp to Op
  srcOp->mapOp = op;
  mappedOps[op] = srcOp;
}

void GraphXfer::unmatch(OpX *srcOp, Op op, Graph *graph) {
  for (size_t i = 0; i < srcOp->inputs.size(); i++) {
	TensorX in = srcOp->inputs[i];
	if (in.op == NULL) {
	  // Update mappedInputsa
	  std::multimap<int, std::pair<Op, int>>::iterator it;
	  it = mappedInputs.find(in.idx);
	  mappedInputs.erase(it);
	}
  }
  // Unmap op
  mappedOps.erase(op);
  srcOp->mapOp.guid = 0;
  srcOp->mapOp.ptr = NULL;
}

void GraphXfer::run(int depth, Graph *graph,
                    std::priority_queue<Graph *, std::vector<Graph *>,
                                        GraphCompare> &candidates,
                    std::set<size_t> &hashmap, float threshold, int maxNumOps) {
  // printf("run: depth(%d) srcOps.size(%zu) graph.size(%zu) candidates(%zu)\n",
  // depth, srcOps.size(), graph->inEdges.size(), candidates.size());
  if (depth >= (int)srcOps.size()) {
	// Create dst operators
	bool pass = true;
	std::vector<OpX *>::const_iterator dstIt;
	for (dstIt = dstOps.begin(); dstIt != dstOps.end(); dstIt++)
	  if (pass) {
		OpX *dstOp = *dstIt;
		pass = (pass & create_new_operator(dstOp, dstOp->mapOp));
	  }
	if (!pass)
	  return;
	// Check that output tensors with external edges are mapped
	std::map<Op, OpX *, OpCompare>::const_iterator opIt;
	for (opIt = mappedOps.begin(); opIt != mappedOps.end(); opIt++) {
	  const std::set<Edge, EdgeCompare> &list = graph->outEdges[opIt->first];
	  std::set<Edge, EdgeCompare>::const_iterator it;
	  for (it = list.begin(); it != list.end(); it++)
		if (mappedOps.find(it->dstOp) == mappedOps.end()) {
		  // dstOp is external, (srcOp, srcIdx) must be in mappedOutputs
		  TensorX srcTen;
		  srcTen.op = opIt->second;
		  srcTen.idx = it->srcIdx;
		  if (mappedOutputs.find(srcTen) == mappedOutputs.end()) {
			pass = false;
			return;
		  }
		}
	}
	// Generate a new graph by applying xfer rule
	Graph *newGraph = create_new_graph(graph);
	// Check that the new graph should not have any loop
	if (newGraph->has_loop()) {
	  // printf("Found a new graph with LOOP!!!!\n");
	  delete newGraph;
	  return;
	}
	// TODO: remove me for better performance
	assert(newGraph->check_correctness());
	if (newGraph->total_cost() < threshold &&
	    (int)newGraph->inEdges.size() < maxNumOps) {
	  if (hashmap.find(newGraph->hash()) == hashmap.end()) {
		hashmap.insert(newGraph->hash());
		candidates.push(newGraph);
	  }
	}
	else {
	  delete newGraph;
	}
  }
  else {
	OpX *srcOp = srcOps[depth];
	std::map<Op, std::set<Edge, EdgeCompare>, OpCompare>::const_iterator it;
	for (it = graph->inEdges.begin(); it != graph->inEdges.end(); it++) {
	  // printf("can_match(%d)\n", can_match(srcOp, it->first, graph));
	  if (can_match(srcOp, it->first, graph) &&
	      (mappedOps.find(it->first) == mappedOps.end())) {
		Op op = it->first;
		// Check mapOutput
		match(srcOp, op, graph);
		run(depth + 1, graph, candidates, hashmap, threshold, maxNumOps);
		unmatch(srcOp, op, graph);
	  }
	}
  }
}

bool GraphXfer::create_new_operator(const OpX *opx, Op &op) {
  Gate *gate = context->get_gate(opx->type);
  op.ptr = gate;
  op.guid = context->next_global_unique_id();
  if (op == Op::INVALID_OP)
	return false;
  return true;
}

Graph *GraphXfer::create_new_graph(Graph *graph) {
  Graph *newGraph = new Graph();
  // Step 1: map dst ops
  std::map<Op, std::set<Edge, EdgeCompare>, OpCompare>::const_iterator opIt;
  std::vector<OpX *>::const_iterator dstIt;
  // Step 2: add edges to the graph
  for (opIt = graph->inEdges.begin(); opIt != graph->inEdges.end(); opIt++)
	if (mappedOps.find(opIt->first) == mappedOps.end()) {
	  // Unmapped ops
	  const std::set<Edge, EdgeCompare> &list = opIt->second;
	  std::set<Edge, EdgeCompare>::const_iterator it;
	  for (it = list.begin(); it != list.end(); it++)
		if (mappedOps.find(it->srcOp) != mappedOps.end()) {
		  // mapped src -> unmapped dst
		  TensorX srcTen;
		  srcTen.op = mappedOps[it->srcOp];
		  srcTen.idx = it->srcIdx;
		  assert(mappedOutputs.find(srcTen) != mappedOutputs.end());
		  TensorX dstTen = mappedOutputs[srcTen];
		  newGraph->add_edge(dstTen.op->mapOp, it->dstOp, dstTen.idx,
		                     it->dstIdx);
		}
		else {
		  // unmapped src -> unmmaped dst
		  newGraph->add_edge(it->srcOp, it->dstOp, it->srcIdx, it->dstIdx);
		}
	}
  // Step 3: add edges for mapped ops
  for (dstIt = dstOps.begin(); dstIt != dstOps.end(); dstIt++) {
	OpX *dstOp = *dstIt;
	for (size_t i = 0; i < dstOp->inputs.size(); i++)
	  if (dstOp->inputs[i].op == NULL) {
		// unmapped src -> mapped dst
		std::multimap<int, std::pair<Op, int>>::const_iterator it =
		    mappedInputs.find(dstOp->inputs[i].idx);
		assert(it != mappedInputs.end());
		std::pair<Op, int> srcEdge = it->second;
		newGraph->add_edge(srcEdge.first, dstOp->mapOp, srcEdge.second, i);
	  }
	  else {
		// mapped src -> mapped dst
		OpX *srcOp = dstOp->inputs[i].op;
		int srcIdx = dstOp->inputs[i].idx;
		newGraph->add_edge(srcOp->mapOp, dstOp->mapOp, srcIdx, i);
	  }
  }
  return newGraph;
}

}; // namespace TASOGraph