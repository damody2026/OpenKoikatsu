"""Shared semantic SVG traversal: decoration is not a geometry ancestor."""
def ancestor_ids(root,predicate):
    result=set()
    def mark(node):
        contains=bool(predicate(node))
        for child in node:contains=mark(child) or contains
        if contains:result.add(id(node))
        return contains
    mark(root)
    return result
