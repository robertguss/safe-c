---
source_url: https://arxiv.org/abs/2404.00762
ingested: 2026-10-14
sha256: b2eea50268a9a531cff9bc59d864f526a22ba4ea47b44d7915b547bdf065f029
fetched_via: exa-contents
---
# Enchanting Program Specification Synthesis by Large Language Models using Static Analysis and Program Verification

Enchanting Program Specification Synthesis by Large Language Models using Static Analysis and Program Verification

arXiv is now an independent nonprofit! Learn more×

# Enchanting Program Specification Synthesis by Large Language Models using Static Analysis and Program Verification

Cheng Wen Affiliation: Guangzhou Institute of Technology, Xidian University, Guangzhou, China Jialun Cao Thanks: Corresponding authors: Jialun Cao and Shengchao Qin. Affiliation: The Hong Kong University of Science and Technology, Hong Kong, China Jie Su Affiliation: Guangzhou Institute of Technology, Xidian University, Guangzhou, China Zhiwu Xu Affiliation: College of Computer Science and Software Engineering, Shenzhen University, China Shengchao Qin Affiliation: Fermat Labs, Huawei, Hong Kong, China Mengda He Affiliation: Fermat Labs, Huawei, Hong Kong, China Haokun Li Affiliation: Fermat Labs, Huawei, Hong Kong, China Shing-Chi Cheung Affiliation: The Hong Kong University of Science and Technology, Hong Kong, China Cong Tian Affiliation: ICTT and ISN Laboratory, Xidian University, Xi’an, China

###### Abstract

Formal verification provides a rigorous and systematic approach to ensure the correctness and reliability of software systems. Yet, constructing specifications for the full proof relies on domain expertise and non-trivial manpower. In view of such needs, an automated approach for specification synthesis is desired. While existing automated approaches are limited in their versatility, i.e., they either focus only on synthesizing loop invariants for numerical programs, or are tailored for specific types of programs or invariants. Programs involving multiple complicated data types (e.g., arrays, pointers) and code structures (e.g., nested loops, function calls) are often beyond their capabilities. To help bridge this gap, we present AutoSpec, an automated approach to synthesize specifications for automated program verification. It overcomes the shortcomings of existing work in specification versatility, synthesizing satisfiable and adequate specifications for full proof. It is driven by static analysis and program verification, and is empowered by large language models (LLMs). AutoSpec addresses the practical challenges in three ways: (1) driving AutoSpec by static analysis and program verification, LLMs serve as generators to generate candidate specifications, (2) programs are decomposed to direct the attention of LLMs, and (3) candidate specifications are validated in each round to avoid error accumulation during the interaction with LLMs. In this way, AutoSpec can incrementally and iteratively generate satisfiable and adequate specifications. The evaluation shows its effectiveness and usefulness, as it outperforms existing works by successfully verifying 79% of programs through automatic specification synthesis, a significant improvement of 1.592x. It can also be successfully applied to verify the programs in a real-world X509-parser project.

## 1 Introduction

Program verification offers a rigorous way to assuring the important properties of a program. Its automation, however, needs to address the challenge of proof construction [1, 2]. Domain expertise is required for non-trivial proof construction, where human experts identify important program properties, write the specifications (e.g., the pre/post-conditions, invariants, and contracts written in certain specification languages), and then use these specifications to prove the properties.

Despite the immense demand for software verification in the industry [3, 4, 5, 6, 7], manual verification by experts remains the primary approach in practice. To reduce human effort, automated specification synthesis is desired. Ideally, given a program and a property to be verified, we expect the specifications that are sufficient for a full proof could be synthesized automatically.

Research gap – Prior works are limited in versatility, i.e., the ability to simultaneously handle different types of specifications (e.g., invariants, preconditions, postconditions), code structures (e.g., multiple function calls, multiple/nested loops), and data structures (e.g., arrays, pointers), leaving room for improvement towards achieving full automation in proof construction. Existing works focus only on loop invariants [8, 9, 10], preconditions [11, 12], or postconditions [13, 14, 15]. Moreover, most works on loop invariant synthesis can only handle numerical programs [16, 2, 17, 18] or are tailored for specific types of programs or invariants [19, 20, 21, 22, 23, 24]. To handle various types of specifications simultaneously and to process programs with various code and data structures, a versatile approach is required.

Challenges – Although the use of large language models (LLMs) such as ChatGPT may provide a straightforward solution to program specification generation, it is not a panacea. The generated specifications are mostly incorrect due to three intrinsic weaknesses of LLMs. First, LLMs can make mistakes. Even for the well-trained programming language Python, ChatGPT-4 and ChatGPT-3.5 only achieve 67.0% and 48.1% accuracy in program synthesis [25]. In comparison with programming languages, LLMs are much less trained in specification languages. Therefore, LLMs generally perform worse in synthesizing specifications than programs. Since the generated specifications are error-prone, we need an effective technique to detect incorrect specifications, which are meaningless to verify. Second, LLMs may not attend to the tokens we want them to. Self-attention may pay no, less, or wrong attention to the tokens that we want it to. Recent research even pointed out a phenomenon called “lost in the middle” [26], observing that LLMs pay little attention to the middle if the context goes extra long. In our case, the synthesized specifications are desired to capture and describe as many program behaviors as possible. Directly adopting the holistic synthesis (i.e., synthesizing all specifications at once) may yield unsatisfactory outcomes. Third, errors accumulate in the output of LLMs. LLMs are auto-regressive. If they make mistakes, these wrong outputs get added to their inputs in the next round, leading to way more wrong outputs. It lays a hidden risk when taking advantage of LLMs’ dialogue features, especially in an incremental manner (i.e., incrementally synthesizing specifications based on previously generated ones).

Insight – To address the above challenges, our key insight is to let static analysis and program verification take the lead, while hiring LLMs to synthesize candidate specifications. Static analysis parses a given program into pieces, and passes each program piece in turn to LLMs by inserting a placeholder in it. Paying attention to the spotted part, LLMs generate a list of specifications as candidates. Subsequently, a theorem prover validates the generated specifications and keeps the validated ones in the next round of synthesis. The iteration process terminates when the property under verification has been proved, or the iteration reaches a predefined limit.

Solution – Bearing the insight, we present AutoSpec, an LLM-empowered framework for generating specifications. It tackles the three above-mentioned limitations of directly adopting LLM in three perspectives. First, it decomposes the program hierarchically and employs LLMs to generate specifications incrementally in a bottom-up manner. This allows LLMs to focus on a selected part of the program and generate specifications only for the selected context. Thus, the limitation of context fragmentation could be largely alleviated. Second, it validates the generated specifications using theorem provers. Specifications that are inconsistent with programs’ behaviors and contradict the properties under verification will be discarded. This post-process ensures that the generated specifications are satisfiable by the source code and the properties under verification. Third, it iteratively enhances the specifications by employing LLMs to generate more specifications until they are adequate to verify the properties under verification or the number of iterations reaches the predefined upper bound.

We evaluate the effectiveness of AutoSpec by conducting experiments on 251 C programs across four benchmarks, each with specific properties to be verified. We compare AutoSpec with three state-of-the-art approaches: Pilat, Code2inv, and CLN2Inv. The result shows AutoSpec can successfully handle 79% (= 199 / 251) programs with various structures (e.g., linear/multiple/nested loops, arrays, pointers), while existing approaches can only handle programs with linear loops. As a result, 59.2% (= (199 - 125) / 125) more programs can be successfully handled by AutoSpec. The result also shows that AutoSpec outperforms these approaches regarding effectiveness and expressiveness when accurately inferring program specifications. To further indicate its usefulness, we apply AutoSpec to a real-world X509-parser project, demonstrating its ability to automatically generate satisfiable and adequate specifications for six functions within a few minutes. In addition, the ablation study reveals that the program decomposition and the hierarchical specification generation components contribute most to performance improvement.

In summary, this paper makes the following contributions:

•

Significance. We present an automated specification synthesis approach, AutoSpec, for program verification. AutoSpec is driven by static analysis and program verification, and empowered by LLMs. It can synthesize different types of specifications (e.g., invariants, preconditions, postconditions) for programs with various structures (e.g., linear/multiple/nested loops, arrays, pointers).

•

Originality. AutoSpec tackles the practical challenges for applying LLMs to specification synthesis: It decomposes the programs hierarchically to lead LLMs’ attention, and validates the specifications at each round to avoid error accumulation. By doing so, AutoSpec can incrementally and iteratively generate satisfiable and adequate specifications to verify the desired properties.

•

Usefulness. We evaluate AutoSpec on four benchmarks and a real-world X509-parser. The four benchmarks include 251 programs with linear/multiple/nested loops, array structures, pointers, etc.. AutoSpec can successfully handle 79% of them, 1.592x outperforming existing works. The experiment result shows the effectiveness, expressiveness, and generalizability of AutoSpec.

- References

## 2 Background and Motivation

Figure 1: ACSL Annotations to Functional Proof of Bubble Sort

1 #include <limits.h>

2

3 /*@

4 requires \valid(a);

5 requires \valid(b);

6 ensures *a == \old(*b);

7 ensures *b == \old(*a);

8 assigns *a,*b;

9 */

10 void swap(int *a, int *b) { 1. Swap

11 int temp = *a;

12 *a = *b;

13 *b = temp;

14 }

15

16 /*@

17 requires \valid(array+(0..n-1));

18 requires 0 < n < INT_MAX;

19 ensures \forall integer i; 0 < i < n ==> array[i-1] <= array[i];

20 */

21 void bubbleSort(int *array, int n) { 4. bubbleSort

22 if (n <= 0) return;

23 int i, j;

24 /*@

25 loop invariant 0 <= i < n;

26 loop invariant \​forall integer k; i <= k < n-1 ==> array[k] <= array[k+1];

27 loop invariant \​forall integer k; 0 <= k < i+1 <= n-1 ==> array[k] <= array[​i+1​];

28 loop assigns i, j, array[0..n-1];

29 */

30 for(i = n - 1; i > 0; i–) { 3. Outer loop

31 /*@

32 loop invariant 0 <= j <= i < n;

33 loop invariant \​forall integer k; 0 <= k <= j ==> array[k] <= array[j];

34 loop invariant \​forall integer k; 0 <= k < i+1 <= n-1 ==> array[k] <= array[​i+1​];

35 loop assigns j, array[0..i];

36 */

37 for(j = 0; j < i; j++) { 2. Inner loop

38 if (array[j] > array[j+1]) {

39 swap(&array[j], &array[j+1]);

40 }

41 }

42 }

43 }

44

45 void main() {

46 int array[5000] = {…, 5, 4, 3, 2, 1};

47 bubbleSort(array, 5000);

48 //@ assert \forall int i; 0 < i < 5000 ==> arra
