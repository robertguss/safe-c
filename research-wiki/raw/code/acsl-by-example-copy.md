---
source_url: https://github.com/fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/copy.h
ingested: 2026-10-15
sha256: 7713c40c44bf1afb4b110f81ad6ed57399c5d8447eb888892107a1df4858839d
fetched_via: curl raw.githubusercontent.com
---
# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/copy.h

```c

#ifndef COPY_H_INCLUDED
#define COPY_H_INCLUDED

#include "Equal.acsl"

/*@
  requires   valid:  \valid_read(a + (0..n-1));
  requires   valid:  \valid(b + (0..n-1));
  requires   sep:    \separated(a + (0..n-1), b);

  terminates         \true;
  exits              \false;
  assigns            b[0..n-1];

  ensures    equal:  Equal{Old,Here}(a, n, b);
*/
void copy(const value_type* a, size_type n, value_type* b);

#endif /* COPY_H_INCLUDED */


```

# fraunhoferfokus/acsl-by-example/blob/master/StandardAlgorithms/Mutating/copy.c

```c

#include "copy.h"
#include "Unchanged.acsl"

void copy(const value_type* a, size_type n, value_type* b)
{
  /*@
    loop invariant bound:     0 <= i <= n;
    loop invariant equal:     Equal{Pre,Here}(a, i, b);
    loop invariant unchanged: Unchanged{Pre,Here}(a, i, n);
    loop assigns   i, b[0..n-1];
    loop variant n-i;
  */
  for (size_type i = 0u; i < n; ++i) {
    b[i] = a[i];
  }
}


```

