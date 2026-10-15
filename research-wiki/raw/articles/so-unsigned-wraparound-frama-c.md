---
source_url: https://stackoverflow.com/questions/61583652/verifying-no-unsigned-integer-wrap-around
ingested: 2026-10-15
sha256: 75ecd80f1eece31298bb8cc930aa73ff7f6d751cf61b16f713877fdb1226af3a
fetched_via: exa-contents (text truncated at 40000 chars)
---
# Verifying no unsigned integer wrap-around

# Verifying no unsigned integer wrap-around

Tags: frama-c

- Score: 1
- Views: 313
- Answers: 1
- Answered: yes
- Asked by: Greenbeard (509 rep)
- Asked: 2020-05-04
- Edited: 2020-05-04
- Site: stackoverflow

## Question

Frama-c appears to allow unsigned integer math to wrap-around while it assumes that signed integer math won't overflow, and this is required to later be proven with the -wp-rte flag. (I am personally using Frama-C 20.0 Calcium.) I would like to somehow create an assurance/requirement that some calculation/property doesn't overflow. A simplified version of the issue I am trying to handle is:

#include <stdint.h>

struct example_struct {
 unsigned int a;
 unsigned int b;
};

/*@
 predicate valid_example_struct_one{L}(struct example_struct ex_struct) =
 ex_struct.a * ex_struct.b <= UINT_MAX;
*/

Obviously, the above predicate is always true due to wrap-around on unsigned integer math. I would like to be able to cast a, and b to be integer type that won't overflow, or somehow specify it can't overflow using another method. The following seems to somewhat work:

/*@
 predicate valid_example_struct_two{L}(struct example_struct ex_struct) =
 1 <= UINT_MAX / ex_struct.a / ex_struct.b;
*/

However, I'm not sure the above is really equivalent as it struggles to prove (notice the swapped a, and b):

 1 <= UINT_MAX / ex_struct.b / ex_struct.a;

if I ask it to with the input being told up hold up to the predicate valid_example_struct_two. Does anyone have suggestions for saying that a * b is less than UINT_MAX (without doing wrap-around)?

## Answers

### Answer by Virgile (score: 3 [ACCEPTED])

Your predicate

/*@
 predicate valid_example_struct_one{L}(struct example_struct ex_struct) =
 ex_struct.a * ex_struct.b <= UINT_MAX;
*/

is not "always true due to wrap-around on unsigned integer math". As mentioned in the ACSL manual, all arithmetic operations in ACSL are done in the integer (or real) type, i.e. without wrap-around. It is thus the canonical way to express the absence of arithmetic overflow, and the one that is used by RTE and Eva when emitting related alarms.

That said, RTE is perfectly capable to emit assertion for unsigned overflow, but this has to be activated explicitly with the kernel option -warn-unsigned-overflow (see Frama-C user manual, section 6.3. The reason this option is not activated by default is that, contrarily to e.g. signed overflows, unsigned overflows have a perfectly defined semantics in C.
