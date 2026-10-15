---
source_url: https://github.com/fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Nonmutating/find.h
ingested: 2026-10-15
sha256: e24da5744650119005b9389264f8943798df22158407cda03bff830d8582dc18
fetched_via: curl raw.githubusercontent.com
---
# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Nonmutating/find.h

```c

#ifndef FIND_H_INCLUDED
#define FIND_H_INCLUDED

#include "typedefs.h"

/*@
  requires   \valid_read(a + (0..n-1));

  terminates \true;
  exits      \false;
  assigns    \nothing;

  ensures    0 <= \result <= n;

  behavior some:
    assumes  \exists integer i; 0 <= i < n && a[i] == v;
    assigns  \nothing;
    ensures  0 <= \result < n;
    ensures  a[\result] == v;
    ensures  \forall integer i; 0 <= i < \result ==> a[i] != v;

  behavior none:
    assumes  \forall integer i; 0 <= i < n ==> a[i] != v;
    assigns  \nothing;
    ensures  \result == n;

  complete behaviors;
  disjoint behaviors;
*/
size_type find(const value_type* a, size_type n, value_type v);

#endif /* FIND_H_INCLUDED */


```

# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Nonmutating/find.c

```c

#include "find.h"

size_type find(const value_type* a, size_type n, value_type v)
{
  /*@
    loop invariant 0 <= i <= n;
    loop invariant \forall integer k; 0 <= k < i ==> a[k] != v;
    loop assigns i;
    loop variant n-i;
   */
  for (size_type i = 0u; i < n; i++) {
    if (a[i] == v) {
      return i;
    }
  }

  return n;
}


```

