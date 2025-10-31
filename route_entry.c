#include <linux/if.h>
#include <linux/if_tun.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "fib.h"

// static struct rib_tree *rib_tree_master = NULL;
struct rib_tree *rib_tree_master[ROUTE_TREE_SIZE];
static int rib_tree_size;

uint32_t
jenkins_hash (uint8_t *key, int key_len)
{
  int i;
  uint32_t hash = 0;

  hash = 0;
  for (i = 0; i < key_len; i++)
    {
      hash += key[i];
      hash += hash << 10;
      hash ^= hash >> 6;
    }
  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;

  return hash;
}

uint32_t
route_table_jenkins_hash (uint8_t *nexthop, uint32_t oif)
{
  uint8_t data[sizeof (nexthop) + sizeof (oif)];
  memset (data, 0, sizeof (data));

  memcpy (data, nexthop, sizeof (nexthop));
  uint32_t oif_be = rte_cpu_to_be_32 (oif);
  memcpy (data + (sizeof (nexthop)), &oif_be, sizeof (oif_be));

  return jenkins_hash (data, sizeof (data)) & ROUTE_TABLE_HASH_MASK;
}

int
route_table_add_entry (struct route_entry *route_table,
                       int family, uint8_t *nexthop, uint32_t oif)
{
  uint32_t hash, offset;
  int match;

  hash = route_table_jenkins_hash (nexthop, oif);
  offset = hash;

  while (route_table[offset].family != 0)
    {
      /* check if entry already exists */
      if (route_table[offset].family == family &&
          route_table[offset].oif == oif &&
          memcmp (route_table[offset].nexthop, nexthop,
                  sizeof (nexthop)) == 0)
        return offset;

      /* linear probing for collision resolution */
      ++offset;
      if (offset >= ROUTE_TABLE_SIZE)
        offset = 0;
      if (offset == hash)
        return -1;
    }

  /* add new entry */
  route_table[offset].family = family;
  route_table[offset].oif = oif;
  memcpy (route_table[offset].nexthop, nexthop,
          sizeof (nexthop));

  uint8_t nexthop_str[INET6_ADDRSTRLEN];
  inet_ntop (family, &nexthop, nexthop_str,
             sizeof (nexthop_str));
  return offset;
}

int
route_table_lookup_entry (const struct route_entry *route_table,
                          int family, uint8_t *nexthop, uint32_t oif)
{
  uint32_t hash, offset;
  int match;

  hash = route_table_jenkins_hash (nexthop, oif);
  offset = hash;

  while (route_table[offset].family != 0)
    {
      if (route_table[offset].family == family &&
          route_table[offset].oif == oif &&
          memcmp (route_table[offset].nexthop, nexthop,
                  sizeof (nexthop)) == 0)
        return offset;

      ++offset;
      if (offset >= ROUTE_TABLE_SIZE)
        offset = 0;
      if (offset == hash)
        break;
    }

  return -1;
}