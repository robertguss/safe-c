#ifndef SAFEC_RESULT_H
#define SAFEC_RESULT_H

#include <stdbool.h>
#include <stddef.h>

/* A tagged value. Read `.value` only after the tag says it is there: `.some`
 * for an option, `.ok` for a result (lint rule R13). The accessors below do
 * the check for you, and their contracts make an unchecked read a proof
 * failure. A failed result leaves `.value` zeroed. */
#define SC_OPTION(T) struct { bool some; T value; }
#define SC_RESULT(T, E) struct { bool ok; T value; E err; }

/* Name an instantiation once with typedef, as below; two uses of the macro
 * are two distinct types. The standard library ships the size_t pair. */
typedef SC_OPTION(size_t) sc_option_size;
typedef SC_RESULT(size_t, int) sc_result_size;

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.some != 0 && \result.value == v;
*/
sc_option_size sc_some_size(size_t v);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.some == 0 && \result.value == 0;
*/
sc_option_size sc_none_size(void);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> o.some != 0;
*/
bool sc_option_size_is_some(sc_option_size o);

/* The caller proves o.some first; there is no value to return for none. */
/*@ requires o.some != 0;
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == o.value;
*/
size_t sc_option_size_unwrap(sc_option_size o);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    behavior some:
      assumes o.some != 0;
      ensures \result == o.value;
    behavior none:
      assumes o.some == 0;
      ensures \result == fallback;
    complete behaviors;
    disjoint behaviors;
*/
size_t sc_option_size_or(sc_option_size o, size_t fallback);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.ok != 0 && \result.value == v && \result.err == 0;
*/
sc_result_size sc_ok_size(size_t v);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.ok == 0 && \result.value == 0 && \result.err == e;
*/
sc_result_size sc_err_size(int e);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> r.ok != 0;
*/
bool sc_result_size_is_ok(sc_result_size r);

/* The caller proves r.ok first. */
/*@ requires r.ok != 0;
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == r.value;
*/
size_t sc_result_size_unwrap(sc_result_size r);

/* The caller proves !r.ok first. */
/*@ requires r.ok == 0;
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == r.err;
*/
int sc_result_size_unwrap_err(sc_result_size r);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    behavior ok:
      assumes r.ok != 0;
      ensures \result == r.value;
    behavior err:
      assumes r.ok == 0;
      ensures \result == fallback;
    complete behaviors;
    disjoint behaviors;
*/
size_t sc_result_size_or(sc_result_size r, size_t fallback);

#endif
