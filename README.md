# rib_and_fib
```
usage: ./main <route_file> [(lookup_file|all)]
  <route_file>        : prefixes & nexthops input
  [(lookup_file|all)] : run lookups test; if omitted, run performance test
```

## 全数テスト (IPv4)

```
./main tests/edited.rib.20251001.0000.ipv4.txt all
```
[result](https://github.com/k1yoto/rib_and_fib/blob/main/doc/full_lookup_test.txt)
