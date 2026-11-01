#include "sc_db.h"

void sc_db_control_init(sc_db_control *control) {
    atomic_init(&control->request, 0);
    atomic_init(&control->cancelled, 0);
    atomic_init(&control->closing, false);
}

void sc_db_control_arm(sc_db_control *control, uint64_t request) {
    atomic_store(&control->cancelled, 0);
    atomic_store(&control->request, request);
}

void sc_db_control_cancel(sc_db_control *control, uint64_t request) {
    if (request != 0 && atomic_load(&control->request) == request)
        atomic_store(&control->cancelled, request);
}

void sc_db_control_close(sc_db_control *control) {
    atomic_store(&control->closing, true);
}

bool sc_db_control_cancelled(sc_db_control *control) {
    uint64_t request = atomic_load(&control->request);
    return atomic_load(&control->closing) ||
           (request != 0 && atomic_load(&control->cancelled) == request);
}
