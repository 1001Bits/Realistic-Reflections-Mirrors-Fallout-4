#pragma once

#include <cstddef>
#include <vector>
#include <unordered_set>

namespace MirrorForeignRig
{
	template<class Node, class Children, class Parent, class IsAuxiliary>
	std::vector<Node*> Branches(Node* root, Node* keepA, Node* keepB,
		Children&& children, Parent&& parent, IsAuxiliary&& isAuxiliary, std::size_t limit = 16384)
	{
		std::vector<Node*> result;
		if (!root) return result;
		const auto descends = [&](Node* node, Node* ancestor) {
			for (std::size_t depth = 0; node && depth < 512; ++depth, node = parent(node))
				if (node == ancestor) return true;
			return node != nullptr; 
		};
		std::vector<Node*> queue{ root };
		std::unordered_set<Node*> visited{root};
		for (std::size_t index = 0; index < queue.size(); ++index) {
			auto* node = queue[index];
			if (node != root && isAuxiliary(node) && !descends(keepA, node) && !descends(keepB, node))
				result.push_back(node);
			bool overflow = false;
			children(node, [&](Node* child) {
				if (!child || visited.contains(child)) return;
				if (queue.size() >= limit) { overflow = true; return; }
				visited.insert(child);
				queue.push_back(child);
			});
			if (overflow) return {};
		}
		return result;
	}
}
