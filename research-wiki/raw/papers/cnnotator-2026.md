---
source_url: https://arxiv.org/abs/2606.21822
ingested: 2026-10-14
sha256: f279aedf6bfc3fbc3e67087367391f0fa0b1b5c79d52a22e67e820bd2aa7e5a1
fetched_via: exa-contents
---
# [2606.21822] CNnotator: LLM-Guided Memory Safety Annotation Synthesis

[2606.21822] CNnotator: LLM-Guided Memory Safety Annotation Synthesis

# CNnotator: LLM-Guided Memory Safety Annotation SynthesisConference: 1st Workshop on Code Translation, Transformation, and Modernization; April 12–18, 2026; Rio de Janeiro, Brazil1st Workshop on Code Translation, Transformation, and Modernization (ReCode ’26), April 12–18, 2026, Rio de Janeiro, BrazilDOI: 10.1145/3786180.3788313ISBN: 979-8-4007-2411-4CCS: Software and its engineering Software verification and validationCCS: Software and its engineering Formal software verificationCCS: Theory of computation Program specificationsCCS: Computing methodologies Artificial intelligence

Twain Byrnes Note: Work completed while a Galois intern. Affiliation: Carnegie Mellon University , Pittsburgh , PA , USA email: binarynewts@cmu.edu and Mike Dodds Affiliation: Galois, Inc. , Portland , OR , USA email: miked@galois.com

(2026; © cc)

###### Abstract.

Memory safety errors account for a large proportion of security bugs in systems written in C; modern languages such as Java and Rust prevent such bugs because they are memory-safe by design. To migrate systems to safer languages or identify memory errors, we must first determine how legacy code manipulates memory. This information is only represented implicitly in such code.

In many cases, memory usage patterns are merely tedious for humans to figure out, rather than truly difficult. In this work, we ask if large language models (LLMs) can perform this task by having them synthesize annotations representing memory usage as specifications in CN, a hybrid testing/verification tool. Our tool, CNnotator, uses LLMs to automatically generate and test CN specifications. We find that current models are able to generate CN specifications for small-to-medium C programs, with the OpenAI o3 reasoning model achieving a 90% success rate on first attempts and 97% overall success, while the chat model GPT-4o correctly annotates 65% of first attempts. These results suggest AI-assisted annotation is becoming practical for real-world C codebases.

###### Keywords:

memory safety, formal verification, separation logic, large language models, C programming, program annotation

††cc-license: by

## 1. Introduction

Formal methods and AI complement each other well: neural AIs are good at guessing while formal methods tools are good at checking. In fact, many formal methods problems can be phrased as guess-and-check loops. For example:

| Guess: | | Check: |
| --- | --- | --- |
| program | $\rightarrow$ | specification |
| type signature | $\rightarrow$ | type checker |
| variable assignment | $\rightarrow$ | SAT solver |
| proof script | $\rightarrow$ | ITP tool |

Using a trustworthy formal checker, we don’t need to trust the guesser. This approach neatly sidesteps the problems of AI unreliability and lack of interpretability. This paper describes CNnotator, a tool which targets one such guess-and-check problem: figuring out how C programs use memory.11 1 Portions of this paper previously appeared on the Galois blog (5).

### C and CN

Memory safety issues cause a large proportion of security bugs. For example, Google estimated in 2020 that 70% of Chromium security bugs arose from memory safety errors (7). In older languages such as C and C++, memory management is tedious to get right, easy to get subtly wrong, and can cause critical security errors that are nearly impossible to detect via unit testing.

Modern languages, like Java and Rust, have built-in mechanisms that prevent nearly all memory safety bugs, with limited escape hatches for complex memory usage patterns. For users, these mechanisms are mostly automated, and many kinds of memory use can be handled by simple patterns. In C and C++, memory usage patterns cannot be explicitly stated or tested, and therefore they are easy to get wrong. But if we have ways of adding information that can cover simple usage patterns, perhaps we can verify a large amount of C and C++ code memory safe as well?

Rather than annotate the code as is, we might transpile it to a memory-safe language. There are several approaches to this translation problem. Symbolic translation methods, like C2Rust (11), Corrode (21), and Citrus (8), translate C to Rust, but produce unsafe, unidiomatic Rust with the expectation that guaranteed-safe, idiomatic code would require additional manual intervention. On the other hand, AI models can perform transpilation; the CRUST-bench benchmark shows that LLMs can generate idiomatic Rust code they claim to be valid and equivalent (13; 12), but without formal guarantees. C2SaferRust combines symbolic and AI approaches (17): idiomatic code from the LLM, correctness from the formal methods. All approaches must first understand memory patterns to verify correct translation.

This is one of the biggest challenges in modernizing or translating C code: figuring out how a given program manipulates memory. C programs do many complex things with memory, and this information is only implicit in the code itself. While tedious for humans to extract, these patterns are often simple enough for AI models to infer.

In this project, we built CNnotator, a tool which uses an LLM to determine what individual C functions do with memory. We represent memory usage information using CN (18), a contract language similar to Frama-C (14), but built on separation logic rather than standard Hoare logic. A contract in CN specifies a precondition and postcondition, and the CN tool then checks whether the contract holds. This testing can be performed by either an SMT solver or by automated fuzzing/property-based testing over the contract.

CN has two features that make it particularly useful for our purposes, compared to verification systems such as Frama-C:

(1)

Memory representation. CN specifications explicitly represent the structure of memory, meaning that specifications represent similar memory usage information to Rust’s borrow checker (it inherits these ideas from separation logic (20)). In particular, CN enforces memory safety through strict ownership requirements for pointers that are passed around, allocated, and freed. These properties are nearly identical to those that safe Rust enforces by default.

(2)

Automated testing. A CN specification can be exercised as a runnable test with no further human effort. The Fulminate backend (3) instruments the annotated C code, compiling the specification into runtime checks of internal assertions and postconditions and tracking memory footprints, so testing detects memory-safety violations as well as input-output errors. Building on Fulminate, the Bennet framework (1) derives random input generators directly from the precondition, producing concrete heap states that satisfy it—including for heap-manipulating functions with non-trivial ownership preconditions, where hand-written generators would otherwise be required.

For example, the CN annotations below specify that the function takes ownership of the integer at`p`(via`RW (p)`), increments it by one, and returns ownership upon completion. CN can automatically convert this specification into test cases that allocate memory, execute the function, and check that the postconditions hold.

void incr(int *p)

/*@ requires take x = RW (p);

ensures take y = RW (p);

y == x + 1i32; @*/

{

(*p)++;

}

There are broadly two kinds of CN annotations: those asserting functional correctness, and those asserting memory safety. In the above example, the first two requirements (those involving read-write permissions) are memory safety annotations. Without the memory safety annotations, CN cannot establish that the code executes without undefined behavior. The last condition asserts functional correctness, describing behavior during normal operation. CNnotator focuses only on memory safety annotations, not functional correctness, which is more subjective.

CN also offers SMT-backed verification, but we used only testing. Each test demonstrates memory safety for that specific run, giving concrete counterexamples on failure while avoiding internal annotations like loop invariants.

### CNnotator experiments

We built our prototype tool, CNnotator, to test whether LLMs could generate CN specifications representing memory usage. At a high level, CNnotator follows the guess-and-check pattern:

(1)

The AI model guesses a possible CN specification for a given C function.

(2)

CN’s testing backend checks that the code passes one hundred automatically generated tests, with Bennet generating concrete heap states from the precondition and Fulminate checking the specification at runtime.

We experimented with several versions of CNnotator. Even the simplest version of this architecture was able to generate specifications for small-to-medium programs, and simple optimization techniques were effective at improving success rates. It seems plausible that, if AI capabilities continue to increase, we will be able to use similar techniques on larger and more complex programs in future.

### Paper structure

The remainder of this paper is organized as follows. Section 2 describes the design of CNnotator. Section 3 describes our benchmark suite. Section 4 presents experimental results across five OpenAI models. Section 5 discusses tradeoffs and concludes.

Figure 1. CNnotator workflow. The tool preprocesses C files (merging and adding CN macros), then iterates through each function. For each function, the LLM first checks for obvious memory safety bugs (e.g., use-after-free). If not obviously unsafe, it generates a CN annotation which is tested against 100 generated memory states. Failure tests trigger refinement with error feedback for up to six iterations.††: CNnotator workflow diagram

## 2. CNnotator Tool Design

CNnotator is structured as a loop (Figure 1 shows a simplified workflow):

(1)

Pick a C function dealing with memory ownership.

(2)

Ask the LLM to generate a CN annotation for that function.

(3)

Test the annotation using the CN testing backend.

(4)

Depending on the result of testing, either refine the annotation, or go back to step 1 and choose another function.

We implement CNnotator as a command line tool written in Python, with agent control flow and interactions handled by the LangChain and LangGraph libraries (6; 15). We use the Treesitter library (22) to parse the target C code and extract an abstract syntax tree (AST).

In preprocessing, we collate all C files in the project into one, so the LLM has full context for interdependent functions. We also add the necessary CN libraries and define macros so that allocation and deallocation work correctly with CN.

In step 1, we iterate over the functions in the AST one at a time. We start from the bottom of the file to avoid invalidating AST positions.

In step 2, we construct a prompt for the LLM based on the following template:

| CNnotator prompt template: |
| --- |
| CN annotation guide. (400 lines approx) Short guide for how to write CN annotations. Since LLMs have not been trained on CN syntax by default, we provide a tutorial to reference. The guide’s structure and much of its text is scraped from the CN Tutorial (19), with small additions to cover common failure cases we observed. |
| Project code. The source code for the whole C project. |
| Target function. The specific function to annotate. |
| Formatting instructions. A request to generate JSON output. |

In step 3, we inject the generated annotation into the target C code. We then test the annotation using CN’s test synthesis capability. This runs the specification against many generated memory states. We test for 100 valid inputs, which is the default number of runs CN uses for its property-based testing feature.

In step 4, we process the result of running the tests, and choose one of two actions:

(1)

If the test passes, we have completed specification generation for this function. We then return to the start of the loop and pick another fun
