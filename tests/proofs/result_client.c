#include "safec/result.h"

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == (r.ok != 0 ? r.value : 0);
*/
size_t checked_result_client(sc_result_size r) {
    if (!sc_result_size_is_ok(r))
        return 0;
    return sc_result_size_unwrap(r);
}

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == (o.some != 0 ? o.value : 0);
*/
size_t checked_option_client(sc_option_size o) {
    if (!sc_option_size_is_some(o))
        return 0;
    return sc_option_size_unwrap(o);
}

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == (r.ok != 0 ? 0 : r.err);
*/
int checked_err_client(sc_result_size r) {
    if (sc_result_size_is_ok(r))
        return 0;
    return sc_result_size_unwrap_err(r);
}
