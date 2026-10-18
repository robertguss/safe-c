/* Deliberately weak contract for `make mutate` tests: the body proves, but
 * `ensures \true` pins nothing, so mutating the clamp must survive. */
#include <stddef.h>

/*@ requires 0 <= n <= 1000;
    assigns \nothing;
    ensures \true;
*/
int weak_clamp(int n) {
    if (n > 100)
        return 100;
    return n + 1;
}
