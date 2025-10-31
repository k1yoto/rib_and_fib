#ifndef FIB_H
#define FIB_H

#include <stdint.h>

#define ROUTE_TABLE_SIZE        1048576 // 2^20
#define ROUTE_TABLE_HASH_MASK   0xFFFFF
#define MAX_ECMP_ENTRY          1
#define ROUTE_TREE_SIZE         2 // IPv4 and IPv6
#define K                       2
#define BRANCH_SZ               (1 << K)

#define KEY_SIZE(len) (((len) + 7) / 8)

struct route_entry
{
  int family;
  int ref_count;
  uint32_t oif; // output interface index
  uint8_t nexthop[16];
};

struct fib_node
{
  int leaf; // 0: non-leaf, 1: leaf
  uint8_t key[16];
  int keylen;
  int num_routes;
  int route_idx[MAX_ECMP_ENTRY];
  struct fib_node *child[BRANCH_SZ];
};
struct fib_tree
{
  int family;
  int table_id;
  struct fib_node *root;
};

struct rib_node
{
  int valid;
  uint8_t key[16];
  int keylen;
  int num_routes;
  int route_idx[MAX_ECMP_ENTRY];
  struct rib_node *left;
  struct rib_node *right;
};
struct rib_tree
{
  int family;
  int table_id;
  struct rib_node *root;
};

// struct show_route_arg
// {
//   struct shell *shell;
//   struct rib_info *rib_info;
//   int family;
// };

struct fib_tree *fib_new (struct fib_tree *t);
void fib_free (struct fib_tree *t);

/* IPv4/v6 */
int fib_route_add (struct fib_tree *t, const uint8_t *key, int keylen,
                    int *route_idx);
struct fib_node *fib_route_lookup (struct fib_tree *t, const uint8_t *key);

// typedef int (*fib_traverse_callback) (struct fib_node *n, void *arg);
// int fib_traverse (struct fib_tree *t, fib_traverse_callback callback,
//                   void *arg);
// int fib_show_route (struct fib_node *n, void *arg);

#endif /* FIB_H */
