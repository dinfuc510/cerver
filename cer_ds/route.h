#ifndef CER_DS_ROUTE_H
#define CER_DS_ROUTE_H

#include <stdio.h>
#include "pair.h"

#define INVALID_INDEX ((size_t)(-1))
typedef enum RouteNodeType {
	ROUTENODE_NORMAL = 0,
	ROUTENODE_NAMED,
	ROUTENODE_WILDCARD,
} RouteNodeType;

typedef struct RouteNode RouteNode;
struct RouteNode {
	Slice label;
	RouteNode **children;
	size_t nchildren;
	size_t nnormal;
	size_t nnamed;
	size_t capacity;
	void *callback;
	RouteNodeType type;
};

RouteNode *create_route(Slice slice, RouteNodeType type) {
	RouteNode *n = calloc(1, sizeof(RouteNode));
	if (slice.len > 0) {
		n->label.ptr = slice_strndup(slice, slice.len);
		n->label.len = slice.len;
		n->type = type;
	}
	return n;
}

RouteNode *find_dynamic_route(RouteNode *root, const char *route, Pairs *matches) {
	if (root == NULL || root->nchildren == 0) {
		return NULL;
	}

	RouteNode *iter = root;
	while(*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		Slice slice = (Slice) { .ptr = route, .len = slash_idx };
		bool found = false;
		for (size_t i = 0; i < iter->nnormal; i++) {
			if (slice_equal(iter->children[i]->label, slice)) {
				found = true;
				iter = iter->children[i];
				break;
			}
		}

		if (!found) {
			if (iter->nchildren == 0) {
				return NULL;
			}

			RouteNode *last_child = iter->children[iter->nchildren - 1];
			bool is_contains_wildcard = last_child != NULL && last_child->type == ROUTENODE_WILDCARD;

			if (iter->nnamed == 0 && !is_contains_wildcard) {
				return NULL;
			}

			const char *peek = route + slash_idx;
			while (*peek == '/') {
				peek += 1;
			}

			if (matches != NULL) {
				append_pair(matches, (Slice) {}, slice);
			}

			for (size_t i = iter->nnormal; i < iter->nchildren; i++) {
				if (matches != NULL && matches->len > 0) {
					matches->keys[matches->len - 1] = iter->children[i]->label;
				}
				if (*peek == '\0' && iter->children[i]->nchildren == 0) {
					return iter->children[i];
				}
				else {
					RouteNode *rn = find_dynamic_route(iter->children[i], peek, matches);
					if (rn != NULL) {
						return rn;
					}
				}
			}

			return NULL;
		}

		route += slash_idx;
		while (*route == '/') {
			route += 1;
		}
	}

	return iter;
}

RouteNode *find_route(RouteNode *root, const char *route) {
	return find_dynamic_route(root, route, NULL);
}

RouteNode *find_child_node(RouteNode *root, size_t start, size_t len, Slice label) {
	size_t end = start + len;
	if (end > root->nchildren) {
		end = root->nchildren;
	}
	for (size_t i = start; i < end; i++) {
		if (slice_equal(root->children[i]->label, label)) {
			return root->children[i];
		}
	}

	return NULL;
}

bool contains_dynamic_node(const char *route) {
	while (*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		if (slash_idx > 0) {
			if (*route == ':' || (slash_idx == 1 && *route == '*')) {
				return true;
			}
			route += slash_idx;
		}
		route += strspn(route, "/");
	}

	return false;
}

size_t count_dynamic_nodes(const char *route) {
	size_t cnt = 0;
	while (*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		if (slash_idx > 0) {
			if (*route == ':' || (slash_idx == 1 && *route == '*')) {
				cnt++;
			}
			route += slash_idx;
		}
		route += strspn(route, "/");
	}

	return cnt;
}

RouteNode *add_route(RouteNode *root, const char *route, void *callback) {
	if (contains_dynamic_node(route) && find_route(root, route) != NULL) {
		return NULL;
	}

	bool is_init = false;
	if (root == NULL) {
		root = create_route((Slice) {0}, ROUTENODE_NORMAL);
		is_init = true;
	}

	RouteNode *iter = root;
	while(*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		RouteNodeType type = ROUTENODE_NORMAL;
		Slice slice = (Slice) { .ptr = route, .len = slash_idx };
		if (slice.len > 0) {
			if (slice.ptr[0] == ':') {
				slice = slice_advanced(slice, 1);
				type = ROUTENODE_NAMED;
			}
			else if (slice.len == 1 && slice.ptr[0] == '*') {
				type = ROUTENODE_WILDCARD;
			}
		}
		bool found = false;
		if (type == ROUTENODE_NORMAL || type == ROUTENODE_NAMED) {
			size_t start = 0, len = iter->nnormal;
			if (type == ROUTENODE_NAMED) {
				start = iter->nnormal;
				len = iter->nnamed;
			}
			RouteNode *child = find_child_node(iter, start, len, slice);
			if (child != NULL) {
				found = true;
				iter = child;
			}
		}
		else if (type == ROUTENODE_WILDCARD) {
			if (iter->nnormal + iter->nnamed == iter->nchildren - 1 && iter->children[iter->nchildren - 1]->type == ROUTENODE_WILDCARD) {
				found = true;
				iter = iter->children[iter->nchildren - 1];
			}
		}

		if (!found) {
			if (iter->nchildren >= iter->capacity) {
				size_t new_cap = iter->nchildren*2;
				if (iter->nchildren >= new_cap) {
					new_cap = iter->nchildren + 1;
				}
				if (iter->nchildren >= new_cap) {
					return NULL;
				}

				RouteNode **new_children = realloc(iter->children, new_cap*sizeof(RouteNode*));
				if (new_children == NULL) {
					return NULL;
				}
				for (size_t i = iter->nchildren; i < new_cap; i++) {
					new_children[i] = NULL;
				}
				iter->children = new_children;
				iter->capacity = new_cap;
			}
			RouteNode *new_child = create_route(slice, type);
			size_t new_child_idx = 0;
			if (iter->nchildren > 0) {
				RouteNode *last_child = iter->children[iter->nchildren - 1];
				bool is_contains_wildcard = last_child != NULL && last_child->type == ROUTENODE_WILDCARD;
				if (is_contains_wildcard) {
					iter->children[iter->nchildren] = iter->children[iter->nchildren - 1];
				}
			}
			if (type == ROUTENODE_NORMAL) {
				new_child_idx = iter->nnormal;
				if (iter->nnamed > 0) {
					iter->children[iter->nnormal + iter->nnamed] = iter->children[iter->nnormal];
				}
			}
			else if (type == ROUTENODE_NAMED) {
				new_child_idx = iter->nnormal + iter->nnamed;
			}
			else if (type == ROUTENODE_WILDCARD) {
				new_child_idx = iter->nchildren;
			}

			iter->nnormal += type == ROUTENODE_NORMAL;
			iter->nnamed += type == ROUTENODE_NAMED;
			iter->children[new_child_idx] = new_child;
			iter->nchildren++;
			iter = iter->children[new_child_idx];
		}
		route += slash_idx;
		route += strspn(route, "/");
	}

	iter->callback = callback;
	return is_init ? root : iter;
}

void free_routes(RouteNode *root) {
	if (root == NULL) {
		return;
	}

	for (size_t i = 0; i < root->nchildren; i++) {
		free_routes(root->children[i]);
	}

	free((char*) root->label.ptr);
	free(root->children);
	free(root);
}

#define TAB_WIDTH 2
void print_routes(RouteNode *root, int depth) {
    if (root == NULL) {
        return;
    }

    if (root->label.len > 0) {
        printf("%*s%.*s (%d) (%ld, %ld, %ld)", depth*TAB_WIDTH, "", (int) root->label.len, root->label.ptr, root->type, root->nchildren, root->nnormal, root->nnamed);
        if (root->callback != NULL) {
            printf("(%p)", root->callback);
        }
        printf("\n");
        depth += TAB_WIDTH;
    }

    for (size_t i = 0; i < root->nchildren; i++) {
        print_routes(root->children[i], depth);
    }
}

#endif // CER_DS_ROUTE_H
