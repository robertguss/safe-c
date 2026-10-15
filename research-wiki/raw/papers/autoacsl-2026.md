---
source_url: https://arxiv.org/abs/2606.20969
ingested: 2026-10-14
sha256: a5ff21155b1ee68066be191f246bb44184349d683c79afb80f371b1718ce987a
fetched_via: exa-contents
---
# AutoACSL: Synthesizing ACSL Specifications by Integrating LLMs with CPG-Based Static Analysis

AutoACSL: Synthesizing ACSL Specifications by Integrating LLMs with CPG-Based Static Analysis

arXiv is now an independent nonprofit! Learn more×

# AutoACSL: Synthesizing ACSL Specifications by Integrating LLMs with CPG-Based Static Analysis

Han Zhou1, Yu Luo2, and Dianxiang Xu1 1University of Missouri–Kansas City, Kansas City, USA 2University of Central Missouri, Warrensburg, USA hzb4f@umsystem.edu, yuluo@ucmo.edu, dxu@umkc.edu

###### Abstract

Generating formal specifications for C programs remains a challenge in formal verification due to the manual effort, expertise, and semantic precision required. While recent advancements in large language models (LLMs) offer promise in automating specification synthesis, current approaches often lack semantic depth and produce unverifiable or incomplete contracts. To address these limitations, we introduce AutoACSL, a novel framework that integrates LLM prompting with semantic features extracted from Code Property Graphs (CPGs). AutoACSL performs static analyses to extract key semantic elements, including arithmetic operations, loop and recursion structures, and return value propagation, which are encoded into structured prompts. These prompts enable the LLM not only to generate normal behavioral specifications but also to include constraints that prevent inputs leading to runtime errors. AutoACSL employs a feedback-driven synthesis loop, where candidate specifications are verified using Frama-C/WP and refined iteratively until verification succeeds or a termination condition is met. Evaluated on 604 programs drawn from diverse datasets, AutoACSL achieves a 98% specification generation success ratio and a 96% full proof ratio when paired with Gemini-3. Compared to a code-only baseline, AutoACSL improves the full proof ratio by 24.7% to 51.7% across four LLMs (GPT-o4 Mini, GPT-5.2, Grok-4.1, and Gemini-3), demonstrating that integrating large language models with CPG-based static analysis substantially enhances both generation robustness and verification effectiveness for automated ACSL specification synthesis.

###### Index Terms:

ACSL, Formal verification, Large language models, Specification synthesis, Static analysis

## 1 Introduction

Formal specification plays a central role in the rigorous verification of software. By precisely defining a program’s expected behavior using preconditions, postconditions, and invariants, specifications serve as the foundation for correctness proofs. However, the widespread adoption of formal verification remains limited in practice—largely due to the manual effort and expertise required to write correct, complete, and verifiable specifications. For instance, authoring reliable ACSL (ANSI/ISO C Specification Language) [26] contracts for C programs demands both deep semantic understanding and familiarity with formal logic and tool support (e.g., Frama-C).

Large Language Models (LLMs) have recently shown promise in automating aspects of specification synthesis [11, 12, 15, 16]. Studies have demonstrated that prompting models to generate specifications from source code can yield surprisingly plausible outputs. Nevertheless, these LLM-generated specifications often suffer from semantic gaps and verification failures, as models lack access to structured semantic context. Treating source code as unstructured text causes generated specifications to be incorrect or unverifiable — even when they appear syntactically valid. Moreover, LLMs tend to overfit to surface patterns, struggle with semantic reasoning, and fail to enforce safety constraints such as memory validity, integer overflows, or termination conditions in recursive functions.

To address these limitations, we present AutoACSL, a novel framework that combines the generative power of LLMs with semantics-aware static analysis to produce verifiable ACSL specifications, including preconditions, postconditions, frame conditions, loop contracts, and termination clauses. AutoACSL prompts the LLM using semantic features extracted from the Code Property Graph (CPG) [31] of the given C program. CPG is a unified program representation combining syntax, control flow, data dependencies, and control dependencies. The CPG analysis identifies such critical information as arithmetic computations, loop and recursion structures, and output propagation paths. Prompts constructed with these features enable the LLM not only to generate normal behavioral specifications, but also to include constraints that prevent inputs leading to runtime errors, such as integer overflows and out-of-bounds accesses. AutoACSL adopts a feedback-driven loop: If the verification of a generated specification fails due to syntax errors or unproven verification conditions, it may reconstruct the prompt using the verification feedback and re-engage the LLM, if necessary, to generate an improved specification.

Evaluation results on a total of 604 C programs drawn from diverse datasets show that AutoACSL, when coupled with Gemini 3.0, successfully generates verifiable specifications for 98% of the programs, with 96% of the generated specifications fully proved against the corresponding C code. Our ablation study further demonstrates that CPG-based static analysis and the guided prompting strategy play a critical role in achieving these gains, improving specification generation robustness by up to 64.4% and increasing the full proof ratio by up to 51.7% when applied to Grok-4.1, compared to a code-only prompting baseline.

The remainder of this paper is organized as follows. Section 2 reviews related work. To make the paper self-contained, Section 3 provides background information on ACSL, Frama-C, and CPG. Section 4 presents the AutoACSL framework. Section 5 elaborates on feature extraction from the CPG, and Section 6 discusses prompt construction with the extracted features. Section 7 presents our experiment results. Finally, Section 8 concludes the paper.

## 2 Related Work

### 2-A Traditional Specification Synthesis from C Programs

Traditional approaches to specification synthesis from C programs fall into two main categories: dynamic and static. Dynamic methods infer likely pre/postconditions or invariants based on observed execution behavior. Daikon [10] instruments programs to trace variable values at key locations (e.g., function boundaries) and infers candidate specifications from recurring patterns across executions. However, the resulting specifications are only guaranteed for the observed inputs, which may lead to overfitting. SymInfer [20] addresses this limitation by combining concrete traces with symbolic states, improving the generalization of inferred properties. DySy [7] further integrates symbolic execution with concrete runs, using counterexamples to iteratively refine candidate invariants. SPEEDY [6] blends lightweight runtime monitoring with Frama-C’s static analyzers to suggest specifications on-the-fly as developers write code, offering a balance between automation and user involvement. CLIPER [18] mines recurring behavioral patterns from execution traces to infer temporal specifications.

Static methods analyze code structure and semantics without executing it, aiming for soundness across all possible program paths. Kindspec [1, 2] uses symbolic execution over programs written in the KernelC subset of the C language to synthesize abstract pre/postconditions expressed as logical contracts. Araujo et al.[8] exemplify a manual approach, translating avionics requirements into ACSL annotations and verifying them with Frama-C. AutoDeduct[3] automates the contract generation process through symbolic execution and Horn clause reasoning. TriCera [4] follows a similar route, translating C code into constrained Horn clauses and using SMT solvers to infer ACSL function contracts.

### 2-B ML/LLM-Assisted Specification Synthesis

LLM-based generation for C and ACSL. Several studies have used LLMs to synthesize ACSL specifications for C programs. George et al.[11] prompt GPT-4 to generate ACSL contracts directly from C source code, and demonstrate that the model can often distinguish between specifications that capture intended behavior versus those that mirror implementation quirks. Building on this, George et al. [12] integrate outputs from Frama-C’s Eva [23] and PathCrawler [24] into the LLM prompt, showing that symbolic error information and test-based traces help the model generate more correct and non-trivial contracts. AutoSpec [29] leverages static analysis to parse the program and build a call graph, then enters an iterative neuro-symbolic loop in which LLM-generated specification candidates are continually refined based on feedback from formal verification tools. VeCoGen [21] takes an ACSL specification, a natural language description, and unit tests as input, then uses an LLM to generate candidate C programs.

Janßen et al.[15] evaluate ChatGPT on 106 small C programs, showing that it can generate correct and useful loop invariants that enable Frama-C and CPAchecker to verify code that would otherwise fail. They identify key failure patterns—such as overly weak or invalid invariants—and suggest feedback-driven refinement. Guangyuan et al.[30] propose a neuro-symbolic pipeline where GPT-3.5 generates candidate invariants, which are then verified by CBMC; counterexamples are fed back to improve subsequent proposals. ACInv [16] extracts loop context through static analysis and prompts an LLM to generate loop invariants, then iteratively refines those invariants via an LLM-based evaluator that strengthens, weakens, or rejects them until they pass verification.

LLM-based generation for other languages. Beyond C, several researchers have explored LLM-based specification synthesis in Java, C++, Rust, and Solidity. Teuber and Beckert [27] use GPT to complete partial JML annotations for Java methods. Their framework uses the KeY verifier in a loop to repair invalid contracts, enabling automated annotation of real-world codebases. Greiner et al. [13] use CodeT5-based models to generate JML pre/postconditions for Java methods, producing syntactically and semantically valid specifications from over 14,000 training examples. SpecGen [19] employs a two-phase strategy: first, using dialogue-style prompting to help an LLM draft pre/postconditions and invariants; second, applying mutation and heuristic selection to repair and verify those contracts. Wang et al. [28] propose Preguss with a stronger focus on large-scale programs, potential-runtime-error guidance, and interprocedural refinement. SAFE [5] generates function specifications by prompting LLMs with Rust function signatures and doc comments to produce candidate preconditions and postconditions in the Verus specification language. These specifications are iteratively refined through self-evolution. In the object-oriented domain, ClassInvGen [22] co-generates C++ class invariants and test inputs, using the LLM to propose candidate invariants that are validated through execution. In the smart contract space, Liu et al. [17] build PropertyGPT to prompt GPT-4 with retrieved examples from existing Certora reports, generating new formal properties for Solidity programs. These properties are then checked and refined using custom provers, achieving both recall and generalization.

## 3 Preliminaries

### 3-A C Program Specifications in ACSL

The main constructs of ACSL specifications are as follow:

•

Preconditions (requires): A precondition specifies a set of conditions that must hold true before a function is invoked. In ACSL, preconditions are specified using the requires keyword within the annotation block.

•

Postconditions (ensures): Postconditions describe properties to be hold upon function return. They may refer to the return value (\result) and to the values of expressions in the pre-state (\old(e)).

•

Frame Conditions (assigns): The assigns clause solves the frame problem by listing exactly which memory location
