#ifndef ROUTE_ENTRY_H
#define ROUTE_ENTRY_H

#include "fib.h"

uint32_t jenkins_hash (uint8_t *key, int key_len);
uint32_t route_table_jenkins_hash (uint8_t *nexthop, uint32_t oif);
int route_table_add_entry (struct route_entry *entry,
                           int family, uint8_t *nexthop, uint32_t oif);
int route_table_lookup_entry (const struct route_entry *entry,
                              int family, uint8_t *nexthop, uint32_t oif);

#endif /* ROUT_ENTRY_H */