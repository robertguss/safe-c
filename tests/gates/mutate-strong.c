/* Control for `make mutate` tests: the contract pins every mutation of the body. */
#include <stddef.h>

/*@ requires 0 <= n <= 1000;
    assigns \nothing;
    ensures n > 100 ==> \result == 100;
    ensures n <= 100 ==> \result == n + 1;
*/
int strong_clamp(int n) {
    if (n > 100)
        return 100;
    return n + 1;
}
