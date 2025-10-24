#ifndef CER_DS_ROUTE_H
#define CER_DS_ROUTE_H

#include <stdio.h>

typedef struct Route Route;
struct Route {
	Slice label;
	Route **children;
	size_t nchildren;
	size_t capacity;
	void *callback;
};

Route *create_route(Slice slice) {
	Route *n = calloc(1, sizeof(Route));
	if (slice.len > 0) {
		n->label.ptr = slice_strndup(slice, slice.len);
		n->label.len = slice.len;
	}
	return n;
}

Route *add_route(Route *root, const char *route, void *callback) {
	bool is_init = false;
	if (root == NULL) {
		root = create_route((Slice) {0});
		is_init = true;
	}

	Route *iter = root;
	while(*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		Slice slice = (Slice) { .ptr = route, .len = slash_idx };
		bool found = false;
		for (size_t i = 0; i < iter->nchildren; i++) {
			if (slice_equal(iter->children[i]->label, slice)) {
				found = true;
				iter = iter->children[i];
				break;
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

				Route **new_children = realloc(iter->children, new_cap*sizeof(Route*));
				if (new_children == NULL) {
					return NULL;
				}
				iter->children = new_children;
				iter->capacity = new_cap;
			}
			Route *new_child = create_route(slice);
			iter->children[iter->nchildren++] = new_child;
			iter = iter->children[iter->nchildren - 1];
		}
		route += slash_idx;
		while (*route == '/') {
			route += 1;
		}
	}

	iter->callback = callback;
	return is_init ? root : iter;
}

void free_routes(Route *root) {
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

Route *find_route(Route *root, const char *route) {
	if (root == NULL || root->nchildren == 0) {
		return NULL;
	}

	Route *iter = root;
	while(*route != '\0') {
		size_t slash_idx = strcspn(route, "/");
		Slice slice = (Slice) { .ptr = route, .len = slash_idx };
		bool found = false;
		for (size_t i = 0; i < iter->nchildren; i++) {
			if (slice_equal(iter->children[i]->label, slice)) {
				found = true;
				iter = iter->children[i];
				break;
			}
		}

		if (!found) {
			return NULL;
		}
		route += slash_idx;
		while (*route == '/') {
			route += 1;
		}
	}

	return iter;
}

#define TAB_WIDTH 2
void print_routes(Route *root, int depth) {
    if (root == NULL) {
        return;
    }

    if (root->label.len > 0) {
        printf("%*s%.*s", depth*TAB_WIDTH, "", (int) root->label.len, root->label.ptr);
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
