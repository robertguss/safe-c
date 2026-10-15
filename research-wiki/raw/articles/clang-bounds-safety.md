---
source_url: https://clang.llvm.org/docs/BoundsSafety.html
ingested: 2026-10-14
sha256: 6808a6386249ff7b27518c779a3611ac5b0181988acfa5ea0adc11c292ca42a3
fetched_via: exa-contents
---
# -fbounds-safety: Enforcing bounds safety for C - Clang

-fbounds-safety: Enforcing bounds safety for C - Clang

## Overview#

NOTE: This is a design document and the feature is not available for users yet. Please see Implementation plans for -fbounds-safety for more details.

`-fbounds-safety` is a C extension to enforce bounds safety to prevent out-of-bounds (OOB) memory accesses, which remain a major source of security vulnerabilities in C. `-fbounds-safety` aims to eliminate this class of bugs by turning OOB accesses into deterministic traps.

The `-fbounds-safety` extension offers bounds annotations that programmers can use to attach bounds to pointers. For example, programmers can add the `__counted_by(N)` annotation to parameter `ptr`, indicating that the pointer has `N` valid elements:

```
void foo(int *__counted_by(N) ptr, size_t N);

```

Using this bounds information, the compiler inserts bounds checks on every pointer dereference, ensuring that the program does not access memory outside the specified bounds. The compiler requires programmers to provide enough bounds information so that the accesses can be checked at either run time or compile time — and it rejects code if it cannot.

The most important contribution of `-fbounds-safety` is how it reduces the programmer’s annotation burden by reconciling bounds annotations at ABI boundaries with the use of implicit wide pointers (a.k.a. “fat” pointers) that carry bounds information on local variables without the need for annotations. We designed this model so that it preserves ABI compatibility with C while minimizing adoption effort.

The `-fbounds-safety` extension has been adopted on millions of lines of production C code and proven to work in a consumer operating system setting. The extension was designed to enable incremental adoption — a key requirement in real-world settings where modifying an entire project and its dependencies all at once is often not possible. It also addresses multiple of other practical challenges that have made existing approaches to safer C dialects difficult to adopt, offering these properties that make it widely adoptable in practice:

- It is designed to preserve the Application Binary Interface (ABI).
- It interoperates well with plain C code.
- It can be adopted partially and incrementally while still providing safety benefits.
- It is a conforming extension to C.
- Consequently, source code that adopts the extension can continue to be compiled by toolchains that do not support the extension (CAVEAT: this still requires inclusion of a header file macro-defining bounds annotations to empty).
- It has a relatively low adoption cost.

This document discusses the key designs of `-fbounds-safety`. The document is subject to active updates with a more detailed specification.

### Overview#

`-fbounds-safety` ensures that pointers are not used to access memory beyond their bounds by performing bounds checking. If a bounds check fails, the program will deterministically trap before out-of-bounds memory is accessed.

In our model, every pointer has an explicit or implicit bounds attribute that determines its bounds and ensures guaranteed bounds checking. Consider the example below where the `__counted_by(count)` annotation indicates that parameter `p` points to a buffer of integers containing `count` elements. An off-by-one error is present in the loop condition, leading to `p[i]` being out-of-bounds access during the loop’s final iteration. The compiler inserts a bounds check before `p` is dereferenced to ensure that the access remains within the specified bounds.

```
void fill_array_with_indices(int *__counted_by(count) p, unsigned count) {
   // off-by-one error (i < count)
   for (unsigned i = 0; i <= count; ++i) {
      // bounds check inserted:
      //   if (i >= count) trap();
      p[i] = i;
   }
}

```

A bounds annotation defines an invariant for the pointer type, and the model ensures that this invariant remains true. In the example below, pointer `p` annotated with `__counted_by(count)` must always point to a memory buffer containing at least `count` elements of the pointee type. Changing the value of `count`, like in the example below, may violate this invariant and permit out-of-bounds access to the pointer. To avoid this, the compiler employs compile-time restrictions and emits run-time checks as necessary to ensure the new count value doesn’t exceed the actual length of the buffer. Section Maintaining correctness of bounds annotations provides more details about this programming model.

```
int g;

void foo(int *__counted_by(count) p, size_t count) {
   count++; // may violate the invariant of __counted_by
   count--; // may violate the invariant of __counted_by if count was 0.
   count = g; // may violate the invariant of __counted_by
              // depending on the value of `g`.
}

```

The requirement to annotate all pointers with explicit bounds information could present a significant adoption burden. To tackle this issue, the model incorporates the concept of a “wide pointer” (a.k.a. fat pointer) – a larger pointer that carries bounds information alongside the pointer value. Utilizing wide pointers can potentially reduce the adoption burden, as it contains bounds information internally and eliminates the need for explicit bounds annotations. However, wide pointers differ from standard C pointers in their data layout, which may result in incompatibilities with the application binary interface (ABI). Breaking the ABI complicates interoperability with external code that has not adopted the same programming model.

`-fbounds-safety` harmonizes the wide pointer and the bounds annotation approaches to reduce the adoption burden while maintaining the ABI. In this model, local variables of pointer type are implicitly treated as wide pointers, allowing them to carry bounds information without requiring explicit bounds annotations. Please note that this approach doesn’t apply to function parameters which are considered ABI-visible. As local variables are typically hidden from the ABI, this approach has a marginal impact on it. In addition, `-fbounds-safety` employs compile-time restrictions to prevent implicit wide pointers from silently breaking the ABI (see ABI implications of default bounds annotations). Pointers associated with any other variables, including function parameters, are treated as single object pointers (i.e., `__single`), ensuring that they always have the tightest bounds by default and offering a strong bounds safety guarantee.

By implementing default bounds annotations based on ABI visibility, a considerable portion of C code can operate without modifications within this programming model, reducing the adoption burden.

The rest of the section will discuss individual bounds annotations and the programming model in more detail.

#### Annotation for pointers to a single object#

The C language allows pointer arithmetic on arbitrary pointers and this has been a source of many bounds safety issues. In practice, many pointers are merely pointing to a single object and incrementing or decrementing such a pointer immediately makes the pointer go out-of-bounds. To prevent this unsafety, `-fbounds-safety` provides the annotation `__single` that causes pointer arithmetic on annotated pointers to be a compile time error.

- `__single` : indicates that the pointer is either pointing to a single object or null. Hence, pointers with `__single` do not permit pointer arithmetic nor being subscripted with a non-zero index. Dereferencing a `__single` pointer is allowed but it requires a null check. Upper and lower bounds checks are not required because the `__single` pointer should point to a valid object unless it’s null.

`__single` is the default annotation for ABI-visible pointers. This gives strong security guarantees in that these pointers cannot be incremented or decremented unless they have an explicit, overriding bounds annotation that can be used to verify the safety of the operation. The compiler issues an error when a `__single` pointer is utilized for pointer arithmetic or array access, as these operations would immediately cause the pointer to exceed its bounds. Consequently, this prompts programmers to provide sufficient bounds information to pointers. In the following example, the pointer on parameter p is single-by-default, and is employed for array access. As a result, the compiler generates an error suggesting to add `__counted_by` to the pointer.

```
void fill_array_with_indices(int *p, unsigned count) {
   for (unsigned i = 0; i < count; ++i) {
      p[i] = i; // error
   }
}

```

#### External bounds annotations#

“External” bounds annotations provide a way to express a relationship between a pointer variable and another variable (or expression) containing the bounds information of the pointer. In the following example, `__counted_by(count)` annotation expresses the bounds of parameter p using another parameter count. This model works naturally with many C interfaces and structs because the bounds of a pointer is often available adjacent to the pointer itself, e.g., at another parameter of the same function prototype, or at another field of the same struct declaration.

```
void fill_array_with_indices(int *__counted_by(count) p, size_t count) {
   // off-by-one error
   for (size_t i = 0; i <= count; ++i)
      p[i] = i;
}

```

External bounds annotations include `__counted_by`, `__sized_by`, and `__ended_by`. These annotations do not change the pointer representation, meaning they do not have ABI implications.

- `__counted_by(N)` : The pointer points to memory that contains `N` elements of pointee type. `N` is an expression of integer type which can be a simple reference to declaration, a constant including calls to constant functions, or an arithmetic expression that does not have side effect. The `__counted_by` annotation cannot apply to pointers to incomplete types or types without size such as `void *`. Instead, `__sized_by` can be used to describe the byte count.
- `__sized_by(N)` : The pointer points to memory that contains `N` bytes. Just like the argument of `__counted_by`, `N` is an expression of integer type which can be a constant, a simple reference to a declaration, or an arithmetic expression that does not have side effects. This is mainly used for pointers to incomplete types or types without size such as `void *`.
- `__ended_by(P)` : The pointer has the upper bound of value `P`, which is one past the last element of the pointer. In other words, this annotation describes a range that starts with the pointer that has this annotation and ends with `P` which is the argument of the annotation. `P` itself may be annotated with `__ended_by(Q)`. In this case, the end of the range extends to the pointer `Q`. This is used for “iterator” support in C where you’re iterating from one pointer value to another until a final pointer value is reached (and the final pointer value is not dereferenceable).

Accessing a pointer outside the specified bounds causes a run-time trap or a compile-time error. Also, the model maintains correctness of bounds annotations when the pointer and/or the related value containing the bounds information are updated or passed as arguments. This is done by compile-time restrictions or run-time checks (see Maintaining correctness of bounds annotations for more detail). For instance, initializing `buf` with `null` while assigning non-zero value to `count`, as shown in the following example, would violate the `__counted_by` annotation because a null pointer does not point to any valid memory location. To avoid this, the compiler produces either a compile-time error or run-time trap.

```
void null_with_count_10(int *__counted_by(count) buf, unsigned count) {
   buf = 0;
   // This is not allowed as it creates a null pointer with non-zero length
   count = 10;
}

```

However, there are use cases where a pointer is either a null pointer or is pointing to memory of the sp
