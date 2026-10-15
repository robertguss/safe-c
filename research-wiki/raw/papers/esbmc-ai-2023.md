---
source_url: https://arxiv.org/abs/2305.14752
ingested: 2026-10-14
sha256: dfc7fad287ec835e1a6e7b969b50f37a434de9fed83000fcae507606328b18b1
fetched_via: exa-contents
---
# [2305.14752] A New Era in Software Security: Towards Self-Healing Software via Large Language Models and Formal Verification

[2305.14752] A New Era in Software Security: Towards Self-Healing Software via Large Language Models and Formal Verification

 -->

# Computer Science > Software Engineering

arXiv:2305.14752 (cs)

[Submitted on 24 May 2023 (v1), last revised 27 Jun 2024 (this version, v2)]

# Title:A New Era in Software Security: Towards Self-Healing Software via Large Language Models and Formal Verification

Authors: Norbert Tihanyi, Ridhi Jain, Yiannis Charalambous, Mohamed Amine Ferrag, Youcheng Sun, Lucas C. Cordeiro

View PDF HTML (experimental)

> Abstract:This paper introduces an innovative approach that combines Large Language Models (LLMs) with Formal Verification strategies for automatic software vulnerability repair. Initially, we employ Bounded Model Checking (BMC) to identify vulnerabilities and extract counterexamples. These counterexamples are supported by mathematical proofs and the stack trace of the vulnerabilities. Using a specially designed prompt, we combine the original source code with the identified vulnerability, including its stack trace and counterexample that specifies the line number and error type. This combined information is then fed into an LLM, which is instructed to attempt to fix the code. The new code is subsequently verified again using BMC to ensure the fix succeeded. We present the ESBMC-AI framework as a proof of concept, leveraging the well-recognized and industry-adopted Efficient SMT-based Context-Bounded Model Checker (ESBMC) and a pre-trained transformer model to detect and fix errors in C programs, particularly in critical software components. We evaluated our approach on 50,000 C programs randomly selected from the FormAI dataset with their respective vulnerability classifications. Our results demonstrate ESBMC-AI's capability to automate the detection and repair of issues such as buffer overflow, arithmetic overflow, and pointer dereference failures with high accuracy. ESBMC-AI is a pioneering initiative, integrating LLMs with BMC techniques, offering potential integration into the continuous integration and deployment (CI/CD) process within the software development lifecycle.

 

https://doi.org/10.48550/arXiv.2305.14752

 

arXiv-issued DOI via DataCite

| Subjects: | Software Engineering (cs.SE); Artificial Intelligence (cs.AI); Formal Languages and Automata Theory (cs.FL); Machine Learning (cs.LG) |
| --- | --- |
| Cite as: | arXiv:2305.14752 [cs.SE] |
| (or arXiv:2305.14752v2 [cs.SE] for this version) |

## Submission history

From: Norbert Tihanyi Dr. [view email] [v1] Wed, 24 May 2023 05:54:10 UTC (524 KB) [v2] Thu, 27 Jun 2024 18:40:19 UTC (594 KB)

 

Full-text links:

## Access Paper:

- View PDF
- HTML (experimental)
- TeX Source

view license

 

Current browse context:

cs.SE

< prev| next >

new| recent| 2023-05

Change to browse by:

cs cs.AI cs.FL cs.LG

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

Links to Code Toggle

Papers with Code (What is Papers with Code?)

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

 

Which authors of this paper are endorsers?| Disable MathJax(What is MathJax?)
