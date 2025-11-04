#ifndef TEST_H
#define TEST_H

#include "fib.h"
#include "ptree.h"

int test_load_routes(const char *routes_filename, int use_ipv6,
                     struct rib_tree **rib_tree, struct ptree **ptree);
int test_basic (struct fib_tree *t, const char *lookup_addrs_filename, int use_ipv6);
int test_performance (struct fib_tree *t, int use_ipv6);
int test_all (struct fib_tree *fib_tree, struct ptree *ptree, int use_ipv6);
int test_all_with_ptree (const char *routes_filename, int use_ipv6);

#endif /* TEST_H */