---
source_url: https://github.com/fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/fill.h
ingested: 2026-10-15
sha256: 503de19dc6a320e1933ee0eb32d55e3bab77371d5d79a313ec2cb2a192b0f75e
fetched_via: curl raw.githubusercontent.com
---
# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/fill.h

```c

#ifndef FILL_H_INCLUDED
#define FILL_H_INCLUDED

#include "AllSomeNot.acsl"

/*@
  requires   valid:   \valid(a + (0..n-1));

  terminates           \true;
  exits                \false;
  assigns              a[0..n-1];

  ensures    constant: AllEqual(a, n, v);
*/
void fill(value_type* a, size_type n, value_type v);

#endif /* FILL_H_INCLUDED */


```

# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/fill.c

```c

#include "fill.h"

void fill(value_type* a, size_type n,  value_type v)
{
  /*@
    loop invariant bound:    0 <= i <= n;
    loop invariant constant: AllEqual(a, i, v);
    loop assigns i, a[0..n-1];
    loop variant n-i;
  */
  for (size_type i = 0u; i < n; ++i) {
    a[i] = v;
  }
}


```

