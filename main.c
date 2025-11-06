#include <arpa/inet.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "test.h"
#include "radix.h"
#include "fib.h"

struct route_entry route_table[ROUTE_TABLE_SIZE];

static void
usage (const char *prog)
{
  fprintf (stderr,
           "usage: %s <route_file> [(lookup_file|all)]\n"
           "  <route_file>        : prefixes & nexthops input\n"
           "  [(lookup_file|all)] : run lookups test; if omitted, run "
           "performance test\n",
           prog);
}

int
main (int argc, const char *const argv[])
{
  int ret, family;
  const char *route_file = NULL;
  const char *lookup_file = NULL;
  int arg_idx = 1;

  struct rib_tree *rib_tree = NULL;
  struct fib_tree *fib_tree = NULL;
  struct ptree *ptree = NULL;

  /* parse arguments */
  if (argc < 1)
    {
      usage (argv[0]);
      return -1;
    }

  /* -6 option (optional) */
  if (arg_idx < argc && strcmp (argv[arg_idx], "-6") == 0)
    {
      family = AF_INET6;
      arg_idx++;
    }
  else
    family = AF_INET;

  /* route file (required) */
  if (arg_idx >= argc)
    {
      fprintf (stderr, "ERROR: missing route_file argument\n");
      usage (argv[0]);
      return -1;
    }
  route_file = argv[arg_idx];
  arg_idx++;

  /* lookup file (optional) */
  if (arg_idx < argc)
    {
      lookup_file = argv[arg_idx];
      arg_idx++;
    }

  /* show configuration */
  fprintf (stdout, "configuration:\n");
  fprintf (stdout, "  IP version: %s\n", family == AF_INET ? "IPv4" : "IPv6");
  fprintf (stdout, "  route file: %s\n", route_file);
  if (lookup_file)
    fprintf (stdout, "  lookup file: %s\n", lookup_file);
  else
    fprintf (stdout, "  mode: performance test\n");
  fprintf (stdout, "\n");

  /* load routes */
  if (test_load_routes (route_file, family, &rib_tree, &ptree) != 0)
    {
      fprintf (stderr, "failed to load routes from %s\n", route_file);
      if (rib_tree)
        rib_free (rib_tree);
      if (ptree)
        ptree_delete (ptree);
      return -1;
    }

  /* build FIB from RIB */
  fib_tree = fib_new (fib_tree);
  if (rebuild_fib_from_rib (rib_tree, fib_tree) != 0)
    {
      fprintf (stderr, "failed to build FIB from RIB\n");
      fib_free (fib_tree);
      rib_free (rib_tree);
      ptree_delete (ptree);
      return -1;
    }

  /* show FIB node statistics */
  test_count_fib_nodes (fib_tree);

  /* run tests */
  if (! lookup_file)
    {
      /* performance test */
      fprintf (stdout, "running performance test...\n");
      ret = test_performance (fib_tree, family);
    }
  else if (strcmp (lookup_file, "all") == 0)
    {
      /*  full inspection lookup test */
      fprintf (stdout, "full inspection lookup test ...\n");
      ret = test_lookup_all (fib_tree, ptree, family);
    }
  else
    {
      /* basic lookup test */
      fprintf (stdout, "running basic test with lookup file %s...\n",
               lookup_file);
      ret = test_lookup (fib_tree, lookup_file, family);
    }

  if (ret < 0)
    {
      fprintf (stderr, "test failed\n");
      if (fib_tree)
        fib_free (fib_tree);
      if (rib_tree)
        rib_free (rib_tree);
      if (ptree)
        ptree_delete (ptree);
      return -1;
    }

  /* cleanup */
  if (fib_tree)
    fib_free (fib_tree);
  if (rib_tree)
    rib_free (rib_tree);
  if (ptree)
    ptree_delete (ptree);

  return 0;
}