#ifndef TEST_H
#define TEST_H

#include "fib.h"
#include "ptree.h"

int test_load_routes(const char *routes_filename, int family,
                     struct rib_tree **rib_tree, struct ptree **ptree);
int test_performance (struct fib_tree *t, int family);
int test_lookup (struct fib_tree *t, const char *lookup_addrs_filename, int family);
int test_lookup_all (struct fib_tree *fib_tree, struct ptree *ptree, int family);
void test_count_fib_nodes (struct fib_tree *t);

#endif /* TEST_H */