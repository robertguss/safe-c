---
source_url: https://arxiv.org/abs/2504.15254
ingested: 2026-10-14
sha256: 5f4edc9efa238b5f9f703ff3f7529cf202d6a8839d39cb1323a759fbea36f2ac
fetched_via: exa-contents
---
# [2504.15254] CRUST-Bench: A Comprehensive Benchmark for C-to-safe-Rust Transpilation

[2504.15254] CRUST-Bench: A Comprehensive Benchmark for C-to-safe-Rust Transpilation

# Computer Science > Software Engineering

arXiv:2504.15254 (cs)

[Submitted on 21 Apr 2025 (v1), last revised 1 Oct 2025 (this version, v3)]

# Title:CRUST-Bench: A Comprehensive Benchmark for C-to-safe-Rust Transpilation

View PDF HTML (experimental)

> Abstract:C-to-Rust transpilation is essential for modernizing legacy C code while enhancing safety and interoperability with modern Rust ecosystems. However, no dataset currently exists for evaluating whether a system can transpile C into safe Rust that passes a set of test cases. We introduce CRUST-Bench, a dataset of 100 C repositories, each paired with manually-written interfaces in safe Rust as well as test cases that can be used to validate correctness of the transpilation. By considering entire repositories rather than isolated functions, CRUST-Bench captures the challenges of translating complex projects with dependencies across multiple files. The provided Rust interfaces provide explicit specifications that ensure adherence to idiomatic, memory-safe Rust patterns, while the accompanying test cases enforce functional correctness. We evaluate state-of-the-art large language models (LLMs) on this task and find that safe and idiomatic Rust generation is still a challenging problem for various state-of-the-art methods and techniques. We also provide insights into the errors LLMs usually make in transpiling code from C to safe Rust. The best performing model, OpenAI o1, is able to solve only 15 tasks in a single-shot setting. Improvements on CRUST-Bench would lead to improved transpilation systems that can reason about complex scenarios and help in migrating legacy codebases from C into languages like Rust that ensure memory safety. You can find the dataset and code at this https URL.

arXiv-issued DOI via DataCite

| Comments: |
| --- |
| Subjects: | Software Engineering (cs.SE); Computation and Language (cs.CL); Machine Learning (cs.LG) |
| Cite as: | arXiv:2504.15254 [cs.SE] |
| (or arXiv:2504.15254v3 [cs.SE] for this version) |

## Submission history

From: Anirudh Khatry [view email] [v1] Mon, 21 Apr 2025 17:33:33 UTC (887 KB) [v2] Fri, 8 Aug 2025 16:45:47 UTC (500 KB) [v3] Wed, 1 Oct 2025 21:43:08 UTC (232 KB)

Full-text links:

## Access Paper:

Current browse context:

cs.SE

< prev| next >

Change to browse by:

### References & Citations

- Google Scholar
- Semantic Scholar

export BibTeX citation

### Bookmark

Bibliographic Tools

# Bibliographic and Citation Tools

Bibliographic Explorer Toggle

Bibliographic Explorer (What is the Explorer?)

Connected Papers Toggle

Connected Papers (What is Connected Papers?)

Litmaps Toggle

Litmaps (What is Litmaps?)

scite.ai Toggle

scite Smart Citations (What are Smart Citations?)

Code, Data, Media

# Code, Data and Media Associated with this Article

alphaXiv Toggle

alphaXiv (What is alphaXiv?)

Links to Code Toggle

CatalyzeX Code Finder for Papers (What is CatalyzeX?)

DagsHub Toggle

DagsHub (What is DagsHub?)

GotitPub Toggle

Gotit.pub (What is GotitPub?)

Huggingface Toggle

Hugging Face (What is Huggingface?)

ScienceCast Toggle

ScienceCast (What is ScienceCast?)

Demos

# Demos

Replicate Toggle

Replicate (What is Replicate?)

Spaces Toggle

Hugging Face Spaces (What is Spaces?)

Spaces Toggle

TXYZ.AI (What is TXYZ.AI?)

Related Papers

# Recommenders and Search Tools

Link to Influence Flower

Influence Flower (What are Influence Flowers?)

Core recommender toggle

CORE Recommender (What is CORE?)

- Author
- Venue
- Institution
- Topic

About arXivLabs

# arXivLabs: experimental projects with community collaborators

arXivLabs is a framework that allows collaborators to develop and share new arXiv features directly on our website.

Both individuals and organizations that work with arXivLabs have embraced and accepted our values of openness, community, excellence, and user data privacy. arXiv is committed to these values and only works with partners that adhere to them.

Have an idea for a project that will add value for arXiv's community? Learn more about arXivLabs.
