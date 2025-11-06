#include <arpa/inet.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "radix.h"
#include "fib.h"
#include "route_entry.h"
#include "main.h"
#include "ptree.h"

#define LINE_BUF_SIZE 4096
#define IP_BUF_SIZE 64

/* -------------------------------------------
 * Utilities
 * ------------------------------------------- */

/* Xorshift32 RNG */
/* see: https://github.com/drpnd/radix-tree/blob/master/tests/basic.c */
static inline uint32_t
xorshift32 (void)
{
  static uint32_t s = 0x9E3779B9u;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

/* Elapsed time in seconds (double) */
/* see: https://www.cc.u-tokyo.ac.jp/public/VOL8/No5/data_no2_0609.pdf */
static double
now_seconds (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static inline void
uint32_to_ipv4_bytes_hton (uint32_t host_ip, uint8_t out[4])
{
  uint32_t net_ip = htonl (host_ip);
  memcpy (out, &net_ip, 4);
}

/* -------------------------------------------
 * Route loading
 * ファイル形式: "<cidr> <next-hop-ip>"
 * 例: "10.0.0.0/8 192.0.2.1"
 * ------------------------------------------- */
static int
_load_routes (const char *path, int family, struct rib_tree **rib_tree,
              struct ptree **ptree)
{
  FILE *fp = NULL;

  char line[LINE_BUF_SIZE];
  char cidr_buf[IP_BUF_SIZE];
  char nh_buf[IP_BUF_SIZE];

  int plen, added, route_idx;

  uint8_t cidr_net_u8[16] = { 0 }; /* CIDR(ネットワークオーダ) */
  uint8_t nh_net_u8[16] = { 0 };   /* ネクストホップ(ネットワークオーダ) */

  printf ("Loading routes from file: %s\n", path);
  fp = fopen (path, "r");
  if (!fp)
    {
      fprintf (stderr, "ERROR: cannot open route file: %s\n", path);
      return -1;
    }

  *rib_tree = rib_new (*rib_tree);
  if (! *rib_tree)
    {
      fprintf (stderr, "ERROR: rib_new failed\n");
      fclose (fp);
      return -1;
    }

  *ptree = ptree_create ();
  if (! *ptree)
    {
      fprintf (stderr, "ERROR: ptree_create failed\n");
      fclose (fp);
      rib_free (*rib_tree);
      return -1;
    }

  added = 0;
  while (fgets (line, sizeof (line), fp))
    {
      if (sscanf (line, "%63s %63s", cidr_buf, nh_buf) != 2)
        {
          fprintf (stderr,
                   "WARN: skip invalid line (need: \"<cidr> <nexthop>\"): %s",
                   line);
          continue;
        }

      plen = inet_net_pton (family, cidr_buf, &cidr_net_u8,
                            sizeof (cidr_net_u8));

      if (plen < 0)
        {
          fprintf (stderr, "WARN: invalid CIDR \"%s\" (skip)\n", cidr_buf);
          continue;
        }

      if (! inet_pton (family, nh_buf, &nh_net_u8))
        {
          fprintf (stderr, "WARN: invalid next-hop \"%s\" (skip)\n", nh_buf);
          continue;
        }

      route_idx = route_table_add_entry (route_table, family, nh_net_u8, 0);
        if (route_idx < 0)
          break;

      if (rib_route_add (*rib_tree, cidr_net_u8, plen, route_idx) < 0)
        {
          fprintf (stderr, "ERROR: rib_route_add failed for %s %s\n", cidr_buf,
                   nh_buf);
          fclose (fp);
          return -1;
        }

      /* Use route_table entry address as ptree data (not stack variable!) */
      if (! ptree_add ((char *)cidr_net_u8, plen,
                       route_table[route_idx].nexthop, *ptree))
        {
          fprintf (stderr, "ERROR: ptree_add failed for %s %s\n", cidr_buf,
                   nh_buf);
          fclose (fp);
          return -1;
        }

      added++;
    }

  printf ("Total %d routes added\n", added);
  fclose (fp);
  return 0;
}

/* -------------------------------------------
 * Performance benchmark
 * ランダム IPv4 を大量に引いてルックアップ（正否は不問）
 * ------------------------------------------- */
int
_benchmark_lookup_performance (struct fib_tree *t, uint64_t trials)
{
  struct fib_node *n;

  double t1, t2;
  double elapsed, qps;

  uint8_t rand_net_u8[4]; /* CIDR(ネットワークオーダ) */
  uint32_t rand_host_u32; /* CIDR(ホストオーダ) */

  if (!t || trials == 0)
    return -1;

  t1 = now_seconds ();

  /* 最適化回避用の集計変数 */
  uintptr_t sink = 0;

  for (uint64_t i = 0; i < trials; i++)
    {
      rand_host_u32 = xorshift32 (); /* ホストオーダの乱数 */
      uint32_to_ipv4_bytes_hton (rand_host_u32, rand_net_u8);

      n = fib_route_lookup (t, rand_net_u8);
      sink ^= (uintptr_t)n;
    }

  t2 = now_seconds ();
  elapsed = t2 - t1;
  qps = (elapsed > 0.0) ? (double)trials / elapsed : 0.0;

  printf ("Elapsed time: %.6f sec for %" PRIu64 " lookups\n", elapsed, trials);
  printf ("Lookup per second: %.6fM lookups/sec\n", qps / 1e6);

  (void)sink; /* 未使用警告抑止 */

  return 0;
}

/* -------------------------------------------
 * Basic lookup test
 * ファイル形式: "<ip>"
 * 例: "203.0.113.5"
 * ------------------------------------------- */
int
_run_lookup (struct fib_tree *tree, const char *path, int family)
{
  printf ("============================================\n");

  FILE *fp;
  struct fib_node *node;

  char line[LINE_BUF_SIZE];
  char ip_addr_buf[IP_BUF_SIZE];
  char nh_buf[IP_BUF_SIZE];

  uint8_t ip_addr_net_u8[16]; /* CIDR(ネットワークオーダ) */

  if (!tree || !path)
    return -1;

  printf ("Lookup test with file: %s\n", path);
  fp = fopen (path, "r");
  if (!fp)
    {
      fprintf (stderr, "ERROR: cannot open lookup file: %s\n", path);
      return -1;
    }

  while (fgets (line, sizeof (line), fp))
    {
      if (sscanf (line, "%63s", ip_addr_buf) != 1)
        {
          fprintf (stderr, "WARN: skip invalid line: %s", line);
          continue;
        }

      if (! inet_pton (family, ip_addr_buf, ip_addr_net_u8))
        {
          fprintf (stderr, "WARN: invalid IP address \"%s\" (skip)\n",
                   ip_addr_buf);
          continue;
        }

      node = fib_route_lookup (tree, ip_addr_net_u8);
      if (node)
        {
          inet_ntop (family, route_table[node->route_idx[0]].nexthop, nh_buf, sizeof (nh_buf));
          printf ("+ Found route for %-16s: %s\n", ip_addr_buf, nh_buf);
        }
      else
        printf ("- No route for %s\n", ip_addr_buf);
    }

  fclose (fp);
  return 0;
}

/* -------------------------------------------
 * Full IPv4 test using ptree as ground truth
 * ------------------------------------------- */
int
_run_lookup_all (struct fib_tree *fib_tree, struct ptree *ptree)
{
  struct fib_node *fib_node;
  struct ptree_node *ptree_node;
  double t1, t2;
  double elapsed, qps;

  uint64_t fib_found = 0;
  uint64_t error_nexthop_mismatch = 0;
  uint64_t error_missing_route = 0;
  uint64_t error_false_positive = 0;
  uint64_t errors = 0;

  uint64_t total_lookups = 0;
  uint64_t total_ptree_found = 0;
  uint64_t total_fib_found = 0;
  uint64_t total_error_nexthop_mismatch = 0;
  uint64_t total_error_missing_route = 0;
  uint64_t total_error_false_positive = 0;
  uint64_t total_errors = 0;

  /* progress tracking */
  double last_progress;

  uint8_t ip_net_u8[4];
  uint32_t ip_host_u32;

  if (! ptree || ! fib_tree)
    return -1;

  /* full IPv4 lookup test */
  printf ("============================================\n");
  printf ("starting full IPv4 address space lookup test with ptree as ground truth\n");
  printf ("testing 2^32 = 4,294,967,296 addresses\n");
  printf ("progress will be shown every 16M lookups (256 updates total)\n\n");

  t1 = now_seconds ();
  last_progress = t1;

  /* iterate through all possible IPv4 addresses: 0.0.0.0 to 255.255.255.255 */
  for (ip_host_u32 = 0; ; ip_host_u32++)
    {
      uint32_to_ipv4_bytes_hton (ip_host_u32, ip_net_u8);

      /* lookup in both ptree and FIB */
      ptree_node = ptree_search ((char *)ip_net_u8, 32, ptree);
      fib_node = fib_route_lookup (fib_tree, ip_net_u8);

      /* verify FIB result against ptree - handle all 4 cases */
      if (ptree_node && fib_node)
        {
          /* both found - compare nexthops */
          total_ptree_found++;
          fib_found++;
          if (memcmp (ptree_node->data,
                      route_table[fib_node->route_idx[0]].nexthop, 4) != 0)
            {
              error_nexthop_mismatch++;
              /* print first few mismatches for debugging */
              if (error_nexthop_mismatch <= 10)
                {
                  char ip_str[INET_ADDRSTRLEN];
                  char expected_str[INET_ADDRSTRLEN];
                  char correct_str[INET_ADDRSTRLEN];
                  inet_ntop (AF_INET, ip_net_u8, ip_str, sizeof (ip_str));
                  inet_ntop (AF_INET, ptree_node->data, expected_str, sizeof (expected_str));
                  inet_ntop (AF_INET, route_table[fib_node->route_idx[0]].nexthop,
                             correct_str, sizeof (correct_str));
                  printf ("ERROR [NEXTHOP MISMATCH] at %s: expected %s, got %s\n",
                          ip_str, expected_str, correct_str);
                }
            }
        }
      else if (ptree_node && !fib_node)
        {
          /* ptree found but FIB didn't - FIB error */
          total_ptree_found++;
          error_missing_route++;
          if (error_missing_route <= 10)
            {
              char ip_str[INET_ADDRSTRLEN];
              char expected_str[INET_ADDRSTRLEN];
              inet_ntop (AF_INET, ip_net_u8, ip_str, sizeof (ip_str));
              inet_ntop (AF_INET, ptree_node->data, expected_str, sizeof (expected_str));
              printf ("ERROR [MISSING ROUTE] at %s: expected %s, got NULL\n",
                      ip_str, expected_str);
            }
        }
      else if (! ptree_node && fib_node)
        {
          /* FIB found but ptree didn't - FIB error (false positive) */
          fib_found++;
          error_false_positive++;
          if (error_false_positive <= 10)
            {
              char ip_str[INET_ADDRSTRLEN];
              char correct_str[INET_ADDRSTRLEN];
              inet_ntop (AF_INET, ip_net_u8, ip_str, sizeof (ip_str));
              inet_ntop (AF_INET, route_table[fib_node->route_idx[0]].nexthop,
                         correct_str, sizeof (correct_str));
              printf ("ERROR [FALSE POSITIVE] at %s: expected NULL, got %s\n",
                      ip_str, correct_str);
            }
        }
      /* else: both NULL - no route, which is correct */

      total_lookups++;

      /* progress indicator every 16M lookups */
      if ((ip_host_u32 & 0xFFFFFF) == 0xFFFFFF)
        {
          double progress = (double)total_lookups / 4294967296.0 * 100.0;
          double now = now_seconds ();
          double elapsed_since_last = now - last_progress;

          errors = error_nexthop_mismatch + error_missing_route +
                   error_false_positive;

          printf ("[progress] %5.2f%% (completed %3u.x.x.x) | found: %" PRIu64
                  " | errors: %" PRIu64 " (nh:%" PRIu64 " miss:%" PRIu64 " fp:%" PRIu64 ")"
                  " | time: %.3fs\n",
                  progress, ip_host_u32 >> 24, fib_found, errors,
                  error_nexthop_mismatch, error_missing_route,
                  error_false_positive, elapsed_since_last);

          last_progress = now;
          total_fib_found += fib_found;
          total_error_nexthop_mismatch += error_nexthop_mismatch;
          total_error_missing_route += error_missing_route;
          total_error_false_positive += error_false_positive;
          total_errors += errors;

          fib_found = 0;
          error_nexthop_mismatch = 0;
          error_missing_route = 0;
          error_false_positive = 0;
          errors = 0;
        }

      /* break at the last address (255.255.255.255) */
      if (ip_host_u32 == 0xFFFFFFFFu)
        break;
    }

  t2 = now_seconds ();
  elapsed = t2 - t1;
  qps = (elapsed > 0.0) ? (double)total_lookups / elapsed : 0.0;
  total_errors = total_error_nexthop_mismatch + total_error_missing_route +
                 total_error_false_positive;

  printf ("\n============================================\n");
  printf ("full IPv4 address space lookup test completed\n");
  printf ("============================================\n");
  printf ("total lookups: %" PRIu64 "\n", total_lookups);
  printf ("ptree routes found: %" PRIu64 " (%.2f%%)\n", total_ptree_found,
          (double)total_ptree_found / (double)total_lookups * 100.0);
  printf ("FIB routes found: %" PRIu64 " (%.2f%%)\n", total_fib_found,
          (double)total_fib_found / (double)total_lookups * 100.0);
  printf ("\n");
  printf ("total errors: %" PRIu64 " (%.6f%%)\n", total_errors,
          (double)total_errors / (double)total_lookups * 100.0);
  printf ("error breakdown:\n");
  printf ("  nexthop mismatch: %" PRIu64 " (%.6f%%)\n", total_error_nexthop_mismatch,
          (double)total_error_nexthop_mismatch / (double)total_lookups * 100.0);
  printf ("  missing routes:   %" PRIu64 " (%.6f%%)\n", total_error_missing_route,
          (double)total_error_missing_route / (double)total_lookups * 100.0);
  printf ("  false positives:  %" PRIu64 " (%.6f%%)\n", total_error_false_positive,
          (double)total_error_false_positive / (double)total_lookups * 100.0);
  printf ("\n");
  printf ("elapsed time: %.6f sec\n", elapsed);
  printf ("lookup per second: %.6fM lookups/sec\n", qps / 1e6);
  printf ("============================================\n");

  /* determine success/failure */
  if (total_errors == 0)
    {
      printf ("\n*** SUCCESS: FIB lookups are correct! ***\n");
      return 0;
    }
  else
    {
      printf ("*** FAILURE: FIB has %" PRIu64 " errors ***\n", total_errors);
      return -1;
    }
}

/* -------------------------------------------
 * FIB node counte
 * ------------------------------------------- */
struct node_count_arg
{
  uint64_t total_nodes;
  uint64_t leaf_nodes;
  uint64_t internal_nodes;
};

static int
_count_node_callback (struct fib_node *n, void *arg)
{
  struct node_count_arg *count = (struct node_count_arg *)arg;

  count->total_nodes++;

  if (n->leaf)
    count->leaf_nodes++;
  else
    count->internal_nodes++;

  return 0;
}

void
test_count_fib_nodes (struct fib_tree *t)
{
  struct node_count_arg count = { 0, 0, 0 };

  if (! t || ! t->root)
    {
      printf ("FIB tree is empty\n");
      return;
    }

  fib_traverse (t, _count_node_callback, &count);

  printf ("============================================\n");
  printf ("FIB node statistics:\n");
  printf ("  Total nodes:    %'" PRIu64 "\n", count.total_nodes);
  printf ("  Leaf nodes:     %'" PRIu64 " (%.2f%%)\n",
          count.leaf_nodes,
          (double)count.leaf_nodes / (double)count.total_nodes * 100.0);
  printf ("  Internal nodes: %'" PRIu64 " (%.2f%%)\n",
          count.internal_nodes,
          (double)count.internal_nodes / (double)count.total_nodes * 100.0);
  printf ("============================================\n");
}

/* -------------------------------------------
 * Wrapper functions for test.h
 * ------------------------------------------- */
int
test_load_routes (const char *routes_filename, int family, struct rib_tree **rib_tree,
                  struct ptree **ptree)
{
  return _load_routes (routes_filename, family, rib_tree, ptree);
}

int
test_performance (struct fib_tree *t, int family)
{
  const uint64_t trials = 0x10000000ULL;

  if (family == AF_INET)
    return _benchmark_lookup_performance (t, trials);
  else
    return -1; // IPv4 only
}

int
test_lookup (struct fib_tree *t, const char *lookup_addrs_filename, int family)
{
  return _run_lookup (t, lookup_addrs_filename, family);
}

int
test_lookup_all (struct fib_tree *fib_tree, struct ptree *ptree, int family)
{
  if (family == AF_INET)
    return _run_lookup_all (fib_tree, ptree);
  else
    return -1; // IPv4 only
}
